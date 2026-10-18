#include "ProcesadoAudio.h"

#include "Guion.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QtEndian>

#include <cmath>
#include <limits>

namespace video {

namespace {

struct Salida {
    int codigo = -1;
    QByteArray out;
    QByteArray err;
};

Salida ejecutar(const QString& programa, const QStringList& args)
{
    QProcess p;
    p.start(programa, args);
    Salida s;
    if (!p.waitForStarted(10000) || !p.waitForFinished(300000)) {
        p.kill();
        p.waitForFinished(5000);
        return s;
    }
    s.codigo = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    s.out = p.readAllStandardOutput();
    s.err = p.readAllStandardError();
    return s;
}

QStringList entradaPcm(const QString& pcm)
{
    return {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-f"), QStringLiteral("s16le"),
            QStringLiteral("-ar"), QStringLiteral("48000"), QStringLiteral("-ac"), QStringLiteral("2"),
            QStringLiteral("-i"), pcm};
}

QString segundos(qint64 muestras)
{
    return QString::number(double(muestras) / kMuestrasPorSegundo, 'f', 2);
}

} // namespace

QString versionFfmpeg(const Herramientas& h)
{
    const Salida s = ejecutar(h.ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-version")});
    return QString::fromUtf8(s.out).section(QLatin1Char('\n'), 0, 0).trimmed();
}

qint64 MapaCompresion::aplicar(qint64 t) const
{
    qint64 quitadas = 0;
    for (const auto& [inicio, muestras] : cortes) {
        if (t <= inicio) {
            break;
        }
        if (t < inicio + muestras) {
            return inicio - quitadas; // dentro de lo quitado: el punto del corte
        }
        quitadas += muestras;
    }
    return t - quitadas;
}

QJsonArray MapaCompresion::aJson() const
{
    QJsonArray a;
    for (const auto& [inicio, muestras] : cortes) {
        a.append(QJsonArray{double(inicio), double(muestras)});
    }
    return a;
}

MapaCompresion MapaCompresion::desdeJson(const QJsonArray& a)
{
    MapaCompresion m;
    for (const QJsonValue& v : a) {
        m.cortes.append({qint64(v.toArray().at(0).toDouble()), qint64(v.toArray().at(1).toDouble())});
    }
    return m;
}

namespace {

// Silencios (-45 dB, >= duracionMs) completos del PCM: [inicio, fin) en muestras.
QList<QPair<qint64, qint64>> silencios(const Herramientas& h, const QString& pcm, int duracionMs, bool& ok);

} // namespace

QString prepararBloque(const Herramientas& h, const QString& wav, double tempo, int pausaMaximaMs, const QString& pcm,
                       const QString& pcmSinComprimir, QJsonObject& meta, MapaCompresion& mapa)
{
    // 1) Formato real del audio recibido (no se supone la frecuencia).
    const Salida sonda = ejecutar(h.ffprobe, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"),
                                              QStringLiteral("a:0"), QStringLiteral("-show_entries"),
                                              QStringLiteral("stream=sample_rate,channels,duration_ts"),
                                              QStringLiteral("-of"), QStringLiteral("json"), wav});
    const QJsonArray streams = QJsonDocument::fromJson(sonda.out).object().value(QStringLiteral("streams")).toArray();
    if (sonda.codigo != 0 || streams.isEmpty()) {
        return QStringLiteral("el audio recibido no es un WAV legible");
    }
    const QJsonObject st = streams.first().toObject();
    meta.insert(QStringLiteral("frecuenciaOriginal"), st.value(QStringLiteral("sample_rate")).toString().toInt());
    meta.insert(QStringLiteral("canalesOriginal"), st.value(QStringLiteral("channels")).toInt());
    meta.insert(QStringLiteral("muestrasOriginal"), st.value(QStringLiteral("duration_ts")).toDouble());

    // 2) Recorte de extremos (D19): todo lo que queda bajo -50 dBFS antes de
    //    la primera voz y despues de la ultima, salvo 65 ms antes y 150 ms
    //    despues; fundidos de 5 ms. Las pausas internas no se tocan.
    // El tempo va antes del recorte: asi los 65/150 ms conservados y las
    // pausas que mide la alineacion ya son los del audio montado.
    const QString filtroTempo = tempo != 1.0 ? QStringLiteral("atempo=%1,").arg(tempo, 0, 'f', 4) : QString();
    const QString recorte = filtroTempo + QStringLiteral(
                                "silenceremove=start_periods=1:start_threshold=-50dB:start_silence=%1:"
                                "detection=peak,areverse,"
                                "silenceremove=start_periods=1:start_threshold=-50dB:start_silence=%2:"
                                "detection=peak,"
                                "afade=t=in:d=0.005,areverse,afade=t=in:d=0.005,"
                                "aresample=48000,aformat=sample_fmts=s16:channel_layouts=stereo")
                                .arg(double(kCabezaMs) / 1000.0)
                                .arg(double(kColaMs) / 1000.0);
    Salida s = ejecutar(h.ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
                                   QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-i"), wav,
                                   QStringLiteral("-af"), recorte, QStringLiteral("-f"), QStringLiteral("s16le"),
                                   QStringLiteral("-ar"), QStringLiteral("48000"), QStringLiteral("-ac"), QStringLiteral("2"),
                                   pcmSinComprimir});
    if (s.codigo != 0) {
        return QStringLiteral("ffmpeg no pudo recortar el bloque");
    }
    const qint64 sinComprimir = QFileInfo(pcmSinComprimir).size() / 4;
    if (sinComprimir <= 0) {
        return QStringLiteral("el bloque quedo vacio tras el recorte");
    }

    // 3) Compresion de pausas internas (R1d), despues de atempo y antes de
    //    medir el loudness y de alinear. Los extremos (ya recortados) no se tocan.
    mapa = {};
    QFile::remove(pcm);
    if (pausaMaximaMs <= 0) {
        if (!QFile::copy(pcmSinComprimir, pcm)) {
            return QStringLiteral("no se pudo copiar el bloque montado");
        }
    } else {
        bool ok = false;
        const QList<QPair<qint64, qint64>> pausas = silencios(h, pcmSinComprimir, pausaMaximaMs, ok);
        if (!ok) {
            return QStringLiteral("ffmpeg no pudo detectar las pausas a comprimir");
        }
        QFile entrada(pcmSinComprimir);
        if (!entrada.open(QIODevice::ReadOnly)) {
            return QStringLiteral("no se pudo leer el bloque para comprimir sus pausas");
        }
        const QByteArray audio = entrada.readAll();
        QByteArray salida;
        salida.reserve(audio.size());
        const qint64 maxima = muestrasDeMs(pausaMaximaMs);
        const qint64 fundido = muestrasDeMs(5);
        // Copia [desde, hasta) de muestras estereo, con fundidos opcionales.
        const auto copiar = [&](qint64 desde, qint64 hasta, bool fundidoEntrada, bool fundidoSalida) {
            for (qint64 k = desde; k < hasta; ++k) {
                double g = 1.0;
                if (fundidoEntrada && k - desde < fundido) {
                    g = double(k - desde) / double(fundido);
                }
                if (fundidoSalida && hasta - k <= fundido) {
                    g = std::min(g, double(hasta - k - 1) / double(fundido));
                }
                for (int canal = 0; canal < 2; ++canal) {
                    const qint16 v = qFromLittleEndian<qint16>(audio.constData() + (k * 2 + canal) * 2);
                    char bytes[2];
                    qToLittleEndian<qint16>(qint16(std::lround(v * g)), bytes);
                    salida.append(bytes, 2);
                }
            }
        };
        qint64 posicion = 0;
        bool entrarConFundido = false;
        for (const auto& [a, b] : pausas) {
            // Solo silencios internos: no tocan los extremos del bloque.
            if (a <= muestrasDeMs(5) || b >= sinComprimir - muestrasDeMs(5) || b - a <= maxima) {
                continue;
            }
            const qint64 corte = a + maxima / 2;
            const qint64 quitadas = (b - a) - maxima;
            copiar(posicion, corte, entrarConFundido, true);
            mapa.cortes.append({corte, quitadas});
            posicion = corte + quitadas;
            entrarConFundido = true;
        }
        copiar(posicion, sinComprimir, entrarConFundido, false);
        QFile f(pcm);
        if (!f.open(QIODevice::WriteOnly) || f.write(salida) != salida.size()) {
            return QStringLiteral("no se pudo escribir el bloque comprimido");
        }
    }
    const qint64 muestras = QFileInfo(pcm).size() / 4;

    // 4) Loudness integrado del bloque (para el ajuste moderado al mezclar).
    s = ejecutar(h.ffmpeg, entradaPcm(pcm) + QStringList{QStringLiteral("-af"), QStringLiteral("ebur128"),
                                                         QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});
    static const QRegularExpression kIntegrado(QStringLiteral("I:\\s+(-?[0-9.]+) LUFS"));
    QRegularExpressionMatch ultimo;
    for (auto it = kIntegrado.globalMatch(QString::fromUtf8(s.err)); it.hasNext();) {
        ultimo = it.next();
    }
    if (s.codigo != 0 || !ultimo.hasMatch()) {
        return QStringLiteral("ffmpeg no pudo medir el loudness del bloque");
    }
    meta.insert(QStringLiteral("muestras"), double(muestras));
    meta.insert(QStringLiteral("muestrasSinComprimir"), double(sinComprimir));
    meta.insert(QStringLiteral("tempo"), tempo);
    meta.insert(QStringLiteral("pausaMaxima"), pausaMaximaMs);
    meta.insert(QStringLiteral("pausasAcortadas"), int(mapa.cortes.size()));
    meta.insert(QStringLiteral("mapa"), mapa.aJson());
    meta.insert(QStringLiteral("lufs"), ultimo.captured(1).toDouble());
    meta.insert(QStringLiteral("recorte"), QStringLiteral("-50 dBFS; conserva %1 ms antes y %2 ms "
                                                          "despues; fundidos de 5 ms")
                                               .arg(kCabezaMs)
                                               .arg(kColaMs));
    return {};
}

namespace {

QList<QPair<qint64, qint64>> silencios(const Herramientas& h, const QString& pcm, int duracionMs, bool& ok)
{
    QList<QPair<qint64, qint64>> r;
    const Salida s = ejecutar(h.ffmpeg, entradaPcm(pcm) + QStringList{QStringLiteral("-af"),
                                                                       QStringLiteral("silencedetect=n=-45dB:d=%1")
                                                                           .arg(double(duracionMs) / 1000.0),
                                                                       QStringLiteral("-f"), QStringLiteral("null"),
                                                                       QStringLiteral("-")});
    ok = s.codigo == 0;
    static const QRegularExpression kPausa(QStringLiteral("silence_start: ([0-9.]+)[\\s\\S]*?silence_end: ([0-9.]+)"));
    for (auto it = kPausa.globalMatch(QString::fromUtf8(s.err)); it.hasNext();) {
        const auto m = it.next();
        r.append({qint64(m.captured(1).toDouble() * kMuestrasPorSegundo), qint64(m.captured(2).toDouble() * kMuestrasPorSegundo)});
    }
    return r;
}

} // namespace

QList<int> largosFrases(const QString& texto)
{
    QList<int> largos;
    const qsizetype n = texto.size();
    qsizetype inicio = 0;
    for (qsizetype i = 0; i < n; ++i) {
        const QChar c = texto.at(i);
        bool fin = false;
        qsizetype ultimo = i; // ultimo caracter del signo
        if (c == QLatin1Char('.') || c == QChar(0x2026)) {
            while (ultimo + 1 < n && (texto.at(ultimo + 1) == QLatin1Char('.') || texto.at(ultimo + 1) == QChar(0x2026))) {
                ++ultimo; // "..." o "…": un solo fin de frase
            }
            fin = true;
        } else if (c == QLatin1Char('?') || c == QLatin1Char('!')) {
            fin = true;
        } else if (c == QLatin1Char(':')) {
            qsizetype k = ultimo + 1;
            while (k < n && texto.at(k).isSpace()) {
                ++k;
            }
            fin = k >= n || texto.at(k).isUpper(); // ":" corta si sigue mayuscula o es el final
        }
        i = ultimo;
        // Solo corta si sigue espacio o fin (no en "e.firma" ni "1.5").
        if (!fin || (ultimo + 1 < n && !texto.at(ultimo + 1).isSpace())) {
            continue;
        }
        largos.append(int(ultimo + 1 - inicio));
        inicio = ultimo + 1;
    }
    if (!texto.mid(inicio).trimmed().isEmpty() || largos.isEmpty()) {
        largos.append(int(std::max<qsizetype>(1, n - inicio))); // texto sin signo final: una frase
    }
    return largos;
}

int contarFrases(const QString& texto)
{
    return int(largosFrases(texto).size());
}

QString alinearBloque(const Herramientas& h, const QString& pcm, qint64 muestras, const QStringList& textos,
                      const QStringList& ids, const QList<std::optional<qint64>>& correcciones, int pausaMinimaMs,
                      const MapaCompresion& mapa, qint64 muestrasFinal, Alineacion& salida)
{
    const int n = int(textos.size());
    salida = {};
    salida.marcas.append(0);

    // 1) Pausas del bloque recortado (>= pausaMinimaMs, bajo -45 dB).
    const Salida s = ejecutar(h.ffmpeg, entradaPcm(pcm) + QStringList{QStringLiteral("-af"),
                                                                       QStringLiteral("silencedetect=n=-45dB:d=%1")
                                                                           .arg(double(pausaMinimaMs) / 1000.0),
                                                                       QStringLiteral("-f"), QStringLiteral("null"),
                                                                       QStringLiteral("-")});
    if (s.codigo != 0) {
        return QStringLiteral("ffmpeg no pudo detectar las pausas");
    }
    static const QRegularExpression kPausa(QStringLiteral("silence_start: ([0-9.]+)[\\s\\S]*?silence_end: ([0-9.]+)"));
    for (auto it = kPausa.globalMatch(QString::fromUtf8(s.err)); it.hasNext();) {
        const auto m = it.next();
        const qint64 a = qint64(m.captured(1).toDouble() * kMuestrasPorSegundo);
        const qint64 b = qint64(m.captured(2).toDouble() * kMuestrasPorSegundo);
        if (a > muestrasDeMs(kCabezaMs) && b < muestras - muestrasDeMs(kColaMs)) {
            salida.pausas.append({a, b});
        }
    }

    // 2) Frases por linea: las fronteras entre frases son las pausas esperadas.
    QList<int> frases;
    int totalFrases = 0;
    for (const QString& t : textos) {
        frases.append(contarFrases(t));
        totalFrases += frases.last();
    }
    salida.pausasEsperadas = totalFrases - 1;
    if (n == 1) {
        salida.metodo = QStringLiteral("una-linea");
    }

    bool faltanCorreccion = false;
    for (int j = 1; j < n; ++j) {
        faltanCorreccion = faltanCorreccion || !correcciones.value(j).has_value();
    }
    const int m = int(salida.pausas.size());
    const int esperadas = salida.pausasEsperadas;
    QList<int> elegida(n, -1); // pausa que abre la linea j (j >= 1)
    const auto duracion = [&](int k) { return salida.pausas.at(k).second - salida.pausas.at(k).first; };

    // Posicion esperada (inicio de voz) de cada frase por caracteres, para
    // los respaldos.
    const qint64 cabeza = muestrasDeMs(kCabezaMs);
    const qint64 voz = std::max<qint64>(1, muestras - cabeza - muestrasDeMs(kColaMs));
    QList<int> largos; // de todas las frases del bloque, en orden
    for (const QString& t : textos) {
        largos += largosFrases(t);
    }
    qsizetype totalCaracteres = 0;
    for (const int l : largos) {
        totalCaracteres += l;
    }
    QList<qint64> inicioFrase; // esperado, por frase
    qsizetype acumuladoCar = 0;
    for (const int l : largos) {
        inicioFrase.append(cabeza + voz * acumuladoCar / std::max<qsizetype>(1, totalCaracteres));
        acumuladoCar += l;
    }
    // Frase con que empieza cada linea.
    QList<int> primeraFrase(n, 0);
    for (int j = 1; j < n; ++j) {
        primeraFrase[j] = primeraFrase[j - 1] + frases.at(j - 1);
    }

    // Asignacion ordenada de `objetivos` (posiciones esperadas) a pausas
    // distintas, minimizando la distancia total (programacion dinamica).
    const auto asignar = [&](const QList<qint64>& objetivos) {
        const int f = int(objetivos.size());
        QList<int> r(f, -1);
        const double inf = std::numeric_limits<double>::infinity();
        QList<QList<double>> costo(f, QList<double>(m, inf));
        QList<QList<int>> previo(f, QList<int>(m, -1));
        for (int a = 0; a < f; ++a) {
            for (int k = a; k < m; ++k) {
                const double c = std::abs(double(salida.pausas.at(k).second - objetivos.at(a)));
                if (a == 0) {
                    costo[a][k] = c;
                    continue;
                }
                for (int q = a - 1; q < k; ++q) {
                    if (costo[a - 1][q] + c < costo[a][k]) {
                        costo[a][k] = costo[a - 1][q] + c;
                        previo[a][k] = q;
                    }
                }
            }
        }
        int mejor = -1;
        for (int k = f - 1; k < m; ++k) {
            if (mejor < 0 || costo[f - 1][k] < costo[f - 1][mejor]) {
                mejor = k;
            }
        }
        for (int a = f - 1; a >= 0; --a) {
            r[a] = mejor;
            mejor = previo[a][mejor];
        }
        return r;
    };

    if (n > 1 && faltanCorreccion && m >= esperadas) {
        // Pausas significativas: las (frases-1) mas largas, si se distinguen
        // de las demas (la siguiente es al menos un 20 % y 40 ms mas corta).
        QList<int> orden(m);
        for (int k = 0; k < m; ++k) {
            orden[k] = k;
        }
        std::stable_sort(orden.begin(), orden.end(), [&](int a, int b) { return duracion(a) > duracion(b); });
        const bool distinguibles =
            m == esperadas
            || (esperadas > 0 && duracion(orden.at(esperadas - 1)) >= duracion(orden.at(esperadas)) * 6 / 5
                && duracion(orden.at(esperadas - 1)) - duracion(orden.at(esperadas)) >= muestrasDeMs(40));
        QList<int> fronteras;
        if (distinguibles) {
            // 3a) Exacto por indice de frase.
            fronteras = orden.mid(0, esperadas);
            std::sort(fronteras.begin(), fronteras.end());
            salida.metodo = QStringLiteral("frases");
        } else {
            // 3b) Respaldo: cada frontera entre frases se asigna a la pausa mas
            //     cercana a su posicion estimada por caracteres (en orden).
            fronteras = asignar(inicioFrase.mid(1));
            salida.metodo = QStringLiteral("respaldo-frases");
        }
        for (int j = 1; j < n; ++j) {
            elegida[j] = fronteras.at(primeraFrase.at(j) - 1);
        }
    } else if (n > 1 && faltanCorreccion) {
        // 3c) Respaldo con menos pausas que frases: solo las fronteras entre
        //     lineas, por su posicion estimada.
        salida.metodo = QStringLiteral("respaldo-proporcional");
        if (m < n - 1) {
            return QStringLiteral("se esperaban %1 pausas entre frases (al menos %2 entre lineas) y se detectaron %3 "
                                  "(corrige las marcas en alineacion/correcciones.json)")
                .arg(esperadas)
                .arg(n - 1)
                .arg(m);
        }
        QList<qint64> objetivos;
        for (int j = 1; j < n; ++j) {
            objetivos.append(inicioFrase.at(primeraFrase.at(j)));
        }
        const QList<int> r = asignar(objetivos);
        for (int j = 1; j < n; ++j) {
            elegida[j] = r.at(j - 1);
            const qint64 tolerancia = std::max(muestrasDeMs(1500), voz / (2 * qint64(largos.size())));
            if (!correcciones.value(j) && std::abs(salida.pausas.at(elegida[j]).second - objetivos.at(j - 1)) > tolerancia) {
                return QStringLiteral("la linea '%1' no coincide con ninguna pausa (esperada en %2 s, pausa en %3 s; "
                                      "corrige su marca en alineacion/correcciones.json)")
                    .arg(ids.at(j), segundos(objetivos.at(j - 1)), segundos(salida.pausas.at(elegida[j]).second));
            }
        }
    } else if (n > 1) {
        salida.metodo = QStringLiteral("correcciones");
    }
    for (int j = 1; j < n; ++j) {
        if (correcciones.value(j)) {
            salida.marcas.append(*correcciones.value(j));
            continue;
        }
        // Pausa detectada en el audio sin comprimir -> audio montado.
        const auto& pausa = salida.pausas.at(elegida.at(j));
        salida.marcas.append(std::max(mapa.aplicar(pausa.first), mapa.aplicar(pausa.second) - muestrasDeMs(kAdelantoMarcaMs)));
    }

    // 4) Validacion: orden, cobertura y duracion minima de cada cue.
    for (int i = 0; i < n; ++i) {
        const qint64 inicio = salida.marcas.at(i);
        const qint64 fin = i + 1 < n ? salida.marcas.at(i + 1) : muestrasFinal;
        if (inicio < 0 || fin > muestrasFinal || fin <= inicio) {
            return QStringLiteral("marcas fuera de orden o del audio en la linea '%1'").arg(ids.at(i));
        }
        if (fin - inicio < muestrasDeMs(kCueMinimoMs)) {
            return QStringLiteral("el cue de la linea '%1' dura %2 s (minimo %3 s)")
                .arg(ids.at(i), segundos(fin - inicio))
                .arg(double(kCueMinimoMs) / 1000.0);
        }
    }
    return {};
}

} // namespace video
