#include "Musica.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>

namespace video {

namespace {

constexpr double kPi = std::numbers::pi;

// --- Preset pad-fiscal-v1 -------------------------------------------------
// Dmaj9 -> Bm7 -> Gmaj7 -> Asus2 (notas MIDI; A4 = 69 = 440 Hz).
constexpr std::array<std::array<int, 4>, 4> kAcordes{{
    {50, 54, 57, 64}, // D3 F#3 A3 E4
    {47, 50, 54, 57}, // B2 D3 F#3 A3
    {43, 47, 50, 54}, // G2 B2 D3 F#3
    {45, 52, 59, 64}, // A2 E3 B3 E4
}};
constexpr int kNotaMinima = 43;
constexpr int kNotaMaxima = 64;
// Seno fundamental y armonicos 2.o y 3.o (1 / 0.18 / 0.04), fases fijas en 0.
constexpr double kArmonico2 = 0.18;
constexpr double kArmonico3 = 0.04;

double frecuencia(int nota)
{
    return 440.0 * std::pow(2.0, double(nota - 69) / 12.0);
}

// Biquad RBJ (forma directa I, doble precision). Continuo en toda la pista.
class Biquad {
public:
    static Biquad pasaAltos(double f, double q) { return Biquad(1, f, q, 0); }
    static Biquad pasaBajos(double f, double q) { return Biquad(2, f, q, 0); }
    static Biquad campana(double f, double q, double db) { return Biquad(3, f, q, db); }
    double operator()(double x)
    {
        const double y = m_b0 * x + m_b1 * m_x1 + m_b2 * m_x2 - m_a1 * m_y1 - m_a2 * m_y2;
        m_x2 = m_x1;
        m_x1 = x;
        m_y2 = m_y1;
        m_y1 = y;
        return y;
    }

private:
    Biquad(int tipo, double f, double q, double db)
    {
        const double w = 2.0 * kPi * f / double(kMuestrasPorSegundo);
        const double alfa = std::sin(w) / (2.0 * q);
        const double c = std::cos(w);
        double b0 = 0;
        double b1 = 0;
        double b2 = 0;
        double a0 = 0;
        double a1 = 0;
        double a2 = 0;
        if (tipo == 1) {
            b0 = (1 + c) / 2;
            b1 = -(1 + c);
            b2 = (1 + c) / 2;
            a0 = 1 + alfa;
            a1 = -2 * c;
            a2 = 1 - alfa;
        } else if (tipo == 2) {
            b0 = (1 - c) / 2;
            b1 = 1 - c;
            b2 = (1 - c) / 2;
            a0 = 1 + alfa;
            a1 = -2 * c;
            a2 = 1 - alfa;
        } else {
            const double a = std::pow(10.0, db / 40.0);
            b0 = 1 + alfa * a;
            b1 = -2 * c;
            b2 = 1 - alfa * a;
            a0 = 1 + alfa / a;
            a1 = -2 * c;
            a2 = 1 - alfa / a;
        }
        m_b0 = b0 / a0;
        m_b1 = b1 / a0;
        m_b2 = b2 / a0;
        m_a1 = a1 / a0;
        m_a2 = a2 / a0;
    }
    double m_b0 = 0, m_b1 = 0, m_b2 = 0, m_a1 = 0, m_a2 = 0;
    double m_x1 = 0, m_x2 = 0, m_y1 = 0, m_y2 = 0;
};

// EQ de la musica: pasaaltos 120 Hz 12 dB/oct; campana 250 Hz -4 dB Q 0.8;
// pasabajos 900 Hz 24 dB/oct (Butterworth de 4.o orden); campana 2 kHz -9 dB Q 0.7.
class EqMusica {
public:
    double operator()(double x)
    {
        for (Biquad& b : m_etapas) {
            x = b(x);
        }
        return x;
    }

private:
    std::array<Biquad, 5> m_etapas{Biquad::pasaAltos(120, std::numbers::sqrt2 / 2), Biquad::campana(250, 0.8, -4),
                                   Biquad::pasaBajos(900, 0.54119610), Biquad::pasaBajos(900, 1.30656296),
                                   Biquad::campana(2000, 0.7, -9)};
};

// Envolvente del acorde k (medio coseno de 1.5 s centrado en cada frontera;
// el primero entra entero: lo cubre el fundido global de entrada).
double envolvente(qint64 k, qint64 n)
{
    const qint64 h = muestrasDeMs(kFundidoAcordeMs) / 2;
    const qint64 inicio = k * kMuestrasPorAcorde;
    const qint64 fin = inicio + kMuestrasPorAcorde;
    double e = 1.0;
    if (k > 0) {
        if (n < inicio - h) {
            return 0.0;
        }
        if (n < inicio + h) {
            e = (1.0 - std::cos(kPi * double(n - (inicio - h)) / double(2 * h))) / 2.0;
        }
    }
    if (n >= fin + h) {
        return 0.0;
    }
    if (n >= fin - h) {
        e *= (1.0 + std::cos(kPi * double(n - (fin - h)) / double(2 * h))) / 2.0;
    }
    return e;
}

QString sha256Archivo(const QString& ruta)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

struct Proceso {
    int codigo = -1;
    QByteArray salida;
    QByteArray err;
};

Proceso ejecutar(const QString& programa, const QStringList& args)
{
    QProcess p;
    p.start(programa, args);
    Proceso r;
    if (!p.waitForStarted(10000) || !p.waitForFinished(600000)) {
        p.kill();
        p.waitForFinished(5000);
        return r;
    }
    r.codigo = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.salida = p.readAllStandardOutput();
    r.err = p.readAllStandardError();
    return r;
}

// Loudness integrado y pico verdadero (loudnorm, solo analisis) de un PCM
// estereo 48 kHz (`formato`: f32le o s16le).
bool medir(const QString& ffmpeg, const QString& ruta, const QString& formato, double& lufs, double& picoVerdadero)
{
    const Proceso s = ejecutar(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-f"),
                                        formato, QStringLiteral("-ar"), QStringLiteral("48000"), QStringLiteral("-ac"),
                                        QStringLiteral("2"), QStringLiteral("-i"), ruta, QStringLiteral("-af"),
                                        QStringLiteral("loudnorm=I=-16:TP=-2:LRA=11:print_format=json"),
                                        QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});
    const QString texto = QString::fromUtf8(s.err);
    const qsizetype i = texto.lastIndexOf(QLatin1Char('{'));
    const qsizetype j = i >= 0 ? texto.indexOf(QLatin1Char('}'), i) : -1;
    const QJsonObject m = j > i ? QJsonDocument::fromJson(texto.mid(i, j - i + 1).toUtf8()).object() : QJsonObject();
    bool okI = false;
    bool okTp = false;
    lufs = m.value(QStringLiteral("input_i")).toString().toDouble(&okI);
    picoVerdadero = m.value(QStringLiteral("input_tp")).toString().toDouble(&okTp);
    return s.codigo == 0 && okI && okTp && std::isfinite(lufs);
}

QString versionFfmpeg(const QString& ffmpeg)
{
    const Proceso s = ejecutar(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-version")});
    return QString::fromUtf8(s.salida).section(QLatin1Char('\n'), 0, 0).trimmed();
}

double dbfs(double potencia)
{
    return potencia > 0 ? 10.0 * std::log10(potencia) : -std::numeric_limits<double>::infinity();
}

} // namespace

Intervalos intervalosVoz(const Guion& planificado)
{
    Intervalos r;
    for (const Capitulo& c : planificado.capitulos) {
        for (const Escena& e : c.escenas) {
            for (const Linea& l : e.lineas) {
                const qint64 inicio = qint64(e.inicio) * kMuestrasPorCuadro + l.inicioMuestra;
                const qint64 n = l.origenFin - l.origenInicio;
                if (n > 0) {
                    r.append({inicio, inicio + n});
                }
            }
        }
    }
    return r;
}

Intervalos unirIntervalos(Intervalos intervalos, qint64 hueco)
{
    std::sort(intervalos.begin(), intervalos.end());
    Intervalos r;
    for (const auto& i : intervalos) {
        if (!r.isEmpty() && i.first - r.last().second < hueco) {
            r.last().second = std::max(r.last().second, i.second);
        } else {
            r.append(i);
        }
    }
    return r;
}

std::vector<float> generarCama(qint64 muestras, const ConfigMusica& config)
{
    std::vector<float> cama(size_t(std::max<qint64>(muestras, 0)), 0.0f);
    std::array<double, kNotaMaxima + 1> frecuencias{};
    for (int nota = kNotaMinima; nota <= kNotaMaxima; ++nota) {
        frecuencias[size_t(nota)] = frecuencia(nota);
    }
    const qint64 entrada = muestrasDeMs(config.entradaMs);
    const qint64 salida = muestrasDeMs(config.salidaMs);
    const double normal = 1.0 / (4.0 * (1.0 + kArmonico2 + kArmonico3));
    EqMusica eq;
    std::array<double, kNotaMaxima + 1> pesos{};
    for (qint64 n = 0; n < muestras; ++n) {
        // Peso de cada nota: suma de las envolventes de los acordes que la
        // contienen. La fase de cada oscilador depende solo del tiempo
        // absoluto (fase fija 0 en la muestra 0): una nota comun a dos acordes
        // consecutivos sigue continua durante el fundido, sin cancelaciones.
        pesos.fill(0.0);
        const qint64 k0 = n / kMuestrasPorAcorde;
        for (qint64 k = std::max<qint64>(0, k0 - 1); k <= k0 + 1; ++k) {
            const double e = envolvente(k, n);
            if (e > 0) {
                for (const int nota : kAcordes[size_t(k % 4)]) {
                    pesos[size_t(nota)] += e;
                }
            }
        }
        double v = 0;
        for (int nota = kNotaMinima; nota <= kNotaMaxima; ++nota) {
            const double w = pesos[size_t(nota)];
            if (w <= 0) {
                continue;
            }
            const double ciclos = frecuencias[size_t(nota)] * double(n) / double(kMuestrasPorSegundo);
            const double fase = 2.0 * kPi * (ciclos - std::floor(ciclos));
            const double s1 = std::sin(fase);
            const double c1 = std::cos(fase);
            const double s2 = 2.0 * s1 * c1;
            const double s3 = 3.0 * s1 - 4.0 * s1 * s1 * s1;
            v += w * (s1 + kArmonico2 * s2 + kArmonico3 * s3);
        }
        v *= normal;
        // Fundidos globales (medio coseno), solo sobre la musica.
        if (n < entrada) {
            v *= (1.0 - std::cos(kPi * double(n) / double(entrada))) / 2.0;
        }
        if (n >= muestras - salida) {
            v *= (1.0 - std::cos(kPi * double(muestras - 1 - n) / double(salida))) / 2.0;
        }
        cama[size_t(n)] = float(eq(v));
    }
    return cama;
}

Ducking::Ducking(const Intervalos& unidos, const ConfigMusica& config)
    : m_intervalos(unidos), m_ataque(muestrasDeMs(config.ataqueMs)), m_anticipacion(muestrasDeMs(kAnticipacionDuckMs)),
      m_retencion(muestrasDeMs(config.retencionMs)), m_liberacion(muestrasDeMs(config.liberacionMs)),
      m_duckDb(config.duckDb)
{
}

double Ducking::db(qint64 n) const
{
    double minimo = 0.0;
    for (qsizetype i = m_actual; i < m_intervalos.size(); ++i) {
        const auto [a, b] = m_intervalos.at(i);
        const qint64 a0 = a - m_anticipacion - m_ataque;
        const qint64 a1 = a - m_anticipacion;
        if (n < a0) {
            break; // los siguientes empiezan despues
        }
        double d = m_duckDb;
        const bool ultimo = i + 1 == m_intervalos.size();
        if (n < a1) {
            d = m_duckDb * (1.0 - std::cos(kPi * double(n - a0) / double(m_ataque))) / 2.0;
        } else if (!ultimo && n >= b + m_retencion) {
            const qint64 r = n - (b + m_retencion);
            d = r >= m_liberacion ? 0.0 : m_duckDb * (1.0 + std::cos(kPi * double(r) / double(m_liberacion))) / 2.0;
        }
        minimo = std::min(minimo, d);
    }
    return minimo;
}

double Ducking::ganancia(qint64 n)
{
    // Intervalos cuya liberacion ya termino: no vuelven a contar.
    while (m_actual + 1 < m_intervalos.size() && n >= m_intervalos.at(m_actual).second + m_retencion + m_liberacion) {
        ++m_actual;
    }
    return std::pow(10.0, db(n) / 20.0);
}

QString mezclarMusica(const Guion& planificado, const QString& pistaVoz, const QString& dirCache, const QString& ffmpeg,
                      const QString& dirTrabajo, const QString& salida, InformeMusica& informe)
{
    const ConfigMusica& config = planificado.musica;
    const qint64 total = qint64(planificado.totalCuadros) * kMuestrasPorCuadro; // por canal
    informe = {};
    if (muestrasDeMs(config.entradaMs + config.salidaMs) > total) {
        return QStringLiteral("musica: los fundidos (%1 + %2 ms) no caben en la pista de %3 s")
            .arg(config.entradaMs)
            .arg(config.salidaMs)
            .arg(double(total) / kMuestrasPorSegundo, 0, 'f', 2);
    }
    QFile fv(pistaVoz);
    if (!fv.open(QIODevice::ReadOnly) || fv.size() != total * 4) {
        return QStringLiteral("musica: la pista de voz no tiene la longitud del video");
    }
    const QByteArray voz = fv.readAll();
    fv.close();

    const Intervalos unidos = unirIntervalos(intervalosVoz(planificado), muestrasDeMs(kHuecoUnionMs));

    // 1) Cama calibrada y con ducking, del cache o generada.
    QJsonArray intervalosJson;
    for (const auto& [a, b] : unidos) {
        intervalosJson.append(QJsonArray{double(a), double(b)});
    }
    const QJsonObject canonica{{QStringLiteral("version"), kVersionMusica},
                               {QStringLiteral("preset"), config.preset},
                               {QStringLiteral("nivelLufs"), config.nivelLufs},
                               {QStringLiteral("duckDb"), config.duckDb},
                               {QStringLiteral("ataqueMs"), config.ataqueMs},
                               {QStringLiteral("retencionMs"), config.retencionMs},
                               {QStringLiteral("liberacionMs"), config.liberacionMs},
                               {QStringLiteral("entradaMs"), config.entradaMs},
                               {QStringLiteral("salidaMs"), config.salidaMs},
                               {QStringLiteral("muestras"), double(total)},
                               {QStringLiteral("intervalos"), intervalosJson},
                               {QStringLiteral("ffmpeg"), versionFfmpeg(ffmpeg)}};
    informe.clave = QString::fromLatin1(
        QCryptographicHash::hash(QJsonDocument(canonica).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
            .toHex());
    QDir().mkpath(dirCache);
    const QDir cache(dirCache);
    const QString rutaMusica = cache.filePath(informe.clave + QStringLiteral(".f32"));
    const QString rutaMeta = cache.filePath(informe.clave + QStringLiteral(".json"));
    std::vector<float> musica;
    {
        QFile fm(rutaMeta);
        if (fm.open(QIODevice::ReadOnly)) {
            const QJsonObject meta = QJsonDocument::fromJson(fm.readAll()).object();
            if (meta.value(QStringLiteral("sha256")).toString() == sha256Archivo(rutaMusica)
                && QFileInfo(rutaMusica).size() == total * qint64(sizeof(float))) {
                QFile f(rutaMusica);
                if (f.open(QIODevice::ReadOnly)) {
                    musica.resize(size_t(total));
                    f.read(reinterpret_cast<char*>(musica.data()), total * qint64(sizeof(float)));
                    informe.desdeCache = true;
                    informe.sha256Musica = meta.value(QStringLiteral("sha256")).toString();
                    informe.lufsCama = meta.value(QStringLiteral("lufsCama")).toDouble();
                    informe.gananciaCamaDb = meta.value(QStringLiteral("gananciaCamaDb")).toDouble();
                }
            }
        }
    }
    if (!informe.desdeCache) {
        musica = generarCama(total, config);
        // Calibracion de la cama ecualizada (antes del ducking) a nivelLufs,
        // medida como se escucha: estereo con ambos canales iguales.
        const QString medida = QDir(dirTrabajo).filePath(QStringLiteral("cama.f32"));
        {
            QFile f(medida);
            if (!f.open(QIODevice::WriteOnly)) {
                return QStringLiteral("musica: no se pudo escribir la cama para medirla");
            }
            std::vector<float> bloque;
            for (qint64 n = 0; n < total; n += 48000) {
                const qint64 hasta = std::min(total, n + 48000);
                bloque.clear();
                for (qint64 k = n; k < hasta; ++k) {
                    bloque.push_back(musica[size_t(k)]);
                    bloque.push_back(musica[size_t(k)]);
                }
                f.write(reinterpret_cast<const char*>(bloque.data()), qint64(bloque.size() * sizeof(float)));
            }
        }
        double tp = 0;
        const bool ok = medir(ffmpeg, medida, QStringLiteral("f32le"), informe.lufsCama, tp);
        QFile::remove(medida);
        if (!ok) {
            return QStringLiteral("musica: ffmpeg no pudo medir el loudness de la cama");
        }
        informe.gananciaCamaDb = config.nivelLufs - informe.lufsCama;
        const double g = std::pow(10.0, informe.gananciaCamaDb / 20.0);
        Ducking duck(unidos, config);
        for (qint64 n = 0; n < total; ++n) {
            musica[size_t(n)] = float(double(musica[size_t(n)]) * g * duck.ganancia(n));
        }
        // Publicacion comprobada: PCM y manifiesto completos en temporales
        // (escritura, flush y cierre verificados); luego rename(2), que
        // reemplaza de forma atomica sin borrar antes lo anterior. Primero el
        // PCM y al final el manifiesto: hasta entonces un manifiesto previo no
        // coincide con el sha256 del PCM nuevo y la entrada se regenera.
        const QByteArray bytesMusica(reinterpret_cast<const char*>(musica.data()),
                                     qsizetype(total * qint64(sizeof(float))));
        informe.sha256Musica = QString::fromLatin1(
            QCryptographicHash::hash(bytesMusica, QCryptographicHash::Sha256).toHex());
        QJsonObject meta = canonica;
        meta.insert(QStringLiteral("sha256"), informe.sha256Musica);
        meta.insert(QStringLiteral("lufsCama"), informe.lufsCama);
        meta.insert(QStringLiteral("gananciaCamaDb"), informe.gananciaCamaDb);
        const QString temporalMusica = rutaMusica + QStringLiteral(".tmp");
        const QString temporalMeta = rutaMeta + QStringLiteral(".tmp");
        const auto escribirCompleto = [](const QString& ruta, const QByteArray& datos) {
            QFile f(ruta);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(datos) != datos.size() || !f.flush()) {
                return false;
            }
            f.close();
            return f.error() == QFileDevice::NoError && QFileInfo(ruta).size() == datos.size();
        };
        const auto descartar = [&](const QString& m) {
            QFile::remove(temporalMusica);
            QFile::remove(temporalMeta);
            return QStringLiteral("musica: ") + m;
        };
        if (!escribirCompleto(temporalMusica, bytesMusica)) {
            return descartar(QStringLiteral("no se pudo escribir el cache de musica"));
        }
        if (!escribirCompleto(temporalMeta, QJsonDocument(meta).toJson())) {
            return descartar(QStringLiteral("no se pudo escribir el manifiesto de musica"));
        }
        if (std::rename(QFile::encodeName(temporalMusica).constData(), QFile::encodeName(rutaMusica).constData()) != 0) {
            return descartar(QStringLiteral("no se pudo publicar el cache de musica"));
        }
        if (std::rename(QFile::encodeName(temporalMeta).constData(), QFile::encodeName(rutaMeta).constData()) != 0) {
            return descartar(QStringLiteral("no se pudo publicar el manifiesto de musica"));
        }
    }

    // 2) Suma en flotante con ganancia unitaria (sin loudnorm sobre la suma):
    //    la voz no cambia; solo se ajusta la musica si la mezcla se sale de
    //    -16 +-0.2 LUFS.
    const auto vozEn = [&](qint64 n, int canal) {
        return double(qFromLittleEndian<qint16>(voz.constData() + (n * 2 + canal) * 2)) / 32768.0;
    };
    const QString suma = QDir(dirTrabajo).filePath(QStringLiteral("mezcla.f32"));
    const auto escribirSuma = [&](double gananciaMusica) {
        QFile f(suma);
        if (!f.open(QIODevice::WriteOnly)) {
            return false;
        }
        std::vector<float> bloque;
        for (qint64 n = 0; n < total; n += 48000) {
            const qint64 hasta = std::min(total, n + 48000);
            bloque.clear();
            for (qint64 k = n; k < hasta; ++k) {
                const double m = gananciaMusica * double(musica[size_t(k)]);
                bloque.push_back(float(vozEn(k, 0) + m));
                bloque.push_back(float(vozEn(k, 1) + m));
            }
            f.write(reinterpret_cast<const char*>(bloque.data()), qint64(bloque.size() * sizeof(float)));
        }
        return true;
    };
    double tpVoz = 0;
    if (!medir(ffmpeg, pistaVoz, QStringLiteral("s16le"), informe.lufsVoz, tpVoz)) {
        return QStringLiteral("musica: ffmpeg no pudo medir la pista de voz");
    }
    double gananciaMusica = 1.0;
    for (;;) {
        if (!escribirSuma(gananciaMusica)
            || !medir(ffmpeg, suma, QStringLiteral("f32le"), informe.lufsMezcla, informe.picoVerdaderoMezcla)) {
            QFile::remove(suma);
            return QStringLiteral("musica: ffmpeg no pudo medir la mezcla");
        }
        if (std::abs(informe.lufsMezcla - kObjetivoMezclaLufs) <= kToleranciaMezclaLu) {
            break;
        }
        if (informe.lufsMezcla < kObjetivoMezclaLufs || informe.ajusteDb <= -12.0) {
            QFile::remove(suma);
            return QStringLiteral("musica: la mezcla mide %1 LUFS (voz sola %2) y ajustar la musica no la lleva a "
                                  "%3 +-%4")
                .arg(informe.lufsMezcla, 0, 'f', 2)
                .arg(informe.lufsVoz, 0, 'f', 2)
                .arg(kObjetivoMezclaLufs)
                .arg(kToleranciaMezclaLu);
        }
        informe.ajusteDb -= 0.5;
        gananciaMusica = std::pow(10.0, informe.ajusteDb / 20.0);
    }
    QFile::remove(suma);

    // 3) PCM final (s16le) y verificacion del margen voz/musica en ventanas
    //    de 400 ms con voz activa (RMS y banda 1-4 kHz).
    QByteArray pcm(qsizetype(total * 4), Qt::Uninitialized);
    const qint64 ventana = muestrasDeMs(kVentanaMargenMs);
    Biquad bvA = Biquad::pasaAltos(1000, std::numbers::sqrt2 / 2);
    Biquad bvB = Biquad::pasaBajos(4000, std::numbers::sqrt2 / 2);
    Biquad bmA = Biquad::pasaAltos(1000, std::numbers::sqrt2 / 2);
    Biquad bmB = Biquad::pasaBajos(4000, std::numbers::sqrt2 / 2);
    double pv = 0, pm = 0, pvb = 0, pmb = 0;
    int conMargen = 0;
    int conMargenBanda = 0;
    std::vector<double> margenes;
    std::vector<double> margenesBanda;
    const qint64 inicioCierre = unidos.isEmpty() ? total : unidos.last().first;
    double maximoCierre = -std::numeric_limits<double>::infinity();
    Ducking duck(unidos, config);
    for (qint64 n = 0; n < total; ++n) {
        const double m = gananciaMusica * double(musica[size_t(n)]);
        for (int canal = 0; canal < 2; ++canal) {
            const double x = std::round((vozEn(n, canal) + m) * 32768.0);
            if (x > 32767.0 || x < -32768.0) {
                ++informe.saturadas;
            }
            qToLittleEndian<qint16>(qint16(std::clamp(x, -32768.0, 32767.0)), pcm.data() + (n * 2 + canal) * 2);
        }
        if (n >= inicioCierre) {
            maximoCierre = std::max(maximoCierre, 20.0 * std::log10(duck.ganancia(n)));
        }
        const double v = (vozEn(n, 0) + vozEn(n, 1)) / 2.0;
        const double vb = bvB(bvA(v));
        const double mb = bmB(bmA(m));
        pv += v * v;
        pm += m * m;
        pvb += vb * vb;
        pmb += mb * mb;
        if ((n + 1) % ventana == 0) {
            const double v_db = dbfs(pv / double(ventana));
            if (v_db >= kUmbralVozActivaDb) {
                ++informe.ventanasConVoz;
                const double margen = v_db - dbfs(pm / double(ventana));
                margenes.push_back(margen);
                conMargen += margen >= kMargenMinimoDb ? 1 : 0;
                // Banda 1-4 kHz: solo ventanas donde la voz tiene energia en
                // esa banda (la voz del motor de prueba es un tono grave).
                if (dbfs(pvb / double(ventana)) >= kUmbralVozActivaDb - 15.0) {
                    const double mb_ = dbfs(pvb / double(ventana)) - dbfs(pmb / double(ventana));
                    margenesBanda.push_back(mb_);
                    conMargenBanda += mb_ >= kMargenBandaDb ? 1 : 0;
                }
            }
            pv = pm = pvb = pmb = 0;
        }
    }
    informe.duckCierreDb = maximoCierre;
    if (informe.ventanasConVoz > 0) {
        informe.fraccionMargen = double(conMargen) / informe.ventanasConVoz;
        std::sort(margenes.begin(), margenes.end());
        informe.margenMedianoDb = margenes[margenes.size() / 2];
        informe.margenP5Db = margenes[size_t(double(margenes.size()) * (1.0 - kFraccionMargen))];
    }
    informe.ventanasBanda = int(margenesBanda.size());
    informe.fraccionMargenBanda = 1.0;
    if (!margenesBanda.empty()) {
        informe.fraccionMargenBanda = double(conMargenBanda) / double(margenesBanda.size());
        std::sort(margenesBanda.begin(), margenesBanda.end());
        informe.margenBandaP5Db = margenesBanda[size_t(double(margenesBanda.size()) * (1.0 - kFraccionMargen))];
    }
    if (informe.saturadas > 0) {
        return QStringLiteral("musica: la mezcla satura %1 muestras").arg(informe.saturadas);
    }
    if (informe.ventanasConVoz > 0
        && (informe.fraccionMargen < kFraccionMargen || informe.fraccionMargenBanda < kFraccionMargen)) {
        const double falta = std::max(kMargenMinimoDb - informe.margenP5Db, kMargenBandaDb - informe.margenBandaP5Db);
        return QStringLiteral("musica: margen voz/musica insuficiente (%1 % de %2 ventanas >= %3 dB, percentil 5 "
                              "%4 dB; %5 % de %6 >= %7 dB en 1-4 kHz, percentil 5 %8 dB; se exige %9 %); baja "
                              "\"nivelLufs\" o \"duckDb\" al menos %10 dB")
            .arg(100.0 * informe.fraccionMargen, 0, 'f', 1)
            .arg(informe.ventanasConVoz)
            .arg(kMargenMinimoDb)
            .arg(informe.margenP5Db, 0, 'f', 1)
            .arg(100.0 * informe.fraccionMargenBanda, 0, 'f', 1)
            .arg(informe.ventanasBanda)
            .arg(kMargenBandaDb)
            .arg(informe.margenBandaP5Db, 0, 'f', 1)
            .arg(100.0 * kFraccionMargen)
            .arg(falta, 0, 'f', 1);
    }
    QFile fs(salida);
    if (!fs.open(QIODevice::WriteOnly) || fs.write(pcm) != pcm.size()) {
        return QStringLiteral("musica: no se pudo escribir la mezcla");
    }
    return {};
}

} // namespace video
