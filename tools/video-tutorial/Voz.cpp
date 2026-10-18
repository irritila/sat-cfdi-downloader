#include "Voz.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <vector>

namespace video {

namespace {

// Propuesta de especialista-audio-voz (sin verificar sin red).
const char* const kModeloPorOmision = "gpt-4o-mini-tts";
const char* const kVozPorOmision = "marin";
const char* const kInstruccionesPorOmision =
    "Español de México. Narrador explicativo, cercano y sobrio. Mantén timbre, registro, intensidad y energía "
    "moderada constantes durante todo el párrafo. Entonación natural, sin dramatización, susurros ni entusiasmo "
    "publicitario. Ritmo conversacional estable, aproximadamente 145–155 palabras por minuto. Enlaza las frases "
    "relacionadas sin convertir cada oración en un nuevo anuncio. Lee únicamente el texto recibido y respeta las "
    "pronunciaciones escritas.";

QString valor(const QVariantMap& m, const char* clave, const QString& porOmision)
{
    const QString v = m.value(QLatin1String(clave)).toString().trimmed();
    return v.isEmpty() ? porOmision : v;
}

QString sha256(const QByteArray& datos)
{
    return QString::fromLatin1(QCryptographicHash::hash(datos, QCryptographicHash::Sha256).toHex());
}

} // namespace

ConfigVoz configVoz(const Guion& guion, const QString& motor)
{
    ConfigVoz c;
    c.motor = motor;
    c.modelo = valor(guion.voz, "modelo", QString::fromUtf8(kModeloPorOmision));
    c.voz = valor(guion.voz, "voz", QString::fromUtf8(kVozPorOmision));
    c.instrucciones = valor(guion.voz, "instrucciones", QString::fromUtf8(kInstruccionesPorOmision));
    c.formato = valor(guion.voz, "formato", QStringLiteral("wav"));
    c.velocidad = guion.voz.value(QStringLiteral("velocidad"), 1.0).toDouble();
    c.tempo = guion.tempo;
    c.pausaMaxima = guion.pausaMaxima;
    return c;
}

QString validarConfigOpenAi(const ConfigVoz& c)
{
    for (const QString& v : {c.modelo, c.voz, c.instrucciones}) {
        if (v.isEmpty() || v.startsWith(QLatin1String("pendiente"))) {
            return QStringLiteral("la configuracion \"voz\" del guion esta incompleta (modelo, voz o instrucciones "
                                  "vacios o \"pendiente\")");
        }
    }
    if (c.formato != QLatin1String("wav")) {
        return QStringLiteral("formato \"%1\" no soportado: usa \"wav\"").arg(c.formato);
    }
    if (c.velocidad < 0.25 || c.velocidad > 4.0) {
        return QStringLiteral("velocidad fuera de [0.25, 4.0]");
    }
    return {};
}

QString claveSintesis(const ConfigVoz& c, const QString& textoBloque)
{
    // QJsonObject ordena las claves: el JSON compacto es canonico.
    QJsonObject o{{QStringLiteral("texto"), textoBloque},
                  {QStringLiteral("modelo"), c.modelo},
                  {QStringLiteral("voz"), c.voz},
                  {QStringLiteral("instrucciones"), c.instrucciones},
                  {QStringLiteral("formato"), c.formato},
                  {QStringLiteral("velocidad"), c.velocidad}};
    if (c.motor != QLatin1String("openai")) {
        o.insert(QStringLiteral("motor"), c.motor);
    }
    return sha256(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString claveBloque(const QString& claveSintesis, const QStringList& textosLineas, const ConfigVoz& config)
{
    QJsonObject o{{QStringLiteral("sintesis"), claveSintesis},
                  {QStringLiteral("lineas"), QJsonArray::fromStringList(textosLineas)}};
    if (config.tempo != 1.0) {
        o.insert(QStringLiteral("tempo"), config.tempo); // con 1.0 la clave no cambia (caches previos)
    }
    if (config.pausaMaxima > 0) {
        o.insert(QStringLiteral("pausaMaxima"), config.pausaMaxima);
    }
    return sha256(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString claveMontaje(const QString& sha256Audio, const ConfigVoz& config)
{
    QString base = QStringLiteral("%1|montaje%2").arg(sha256Audio).arg(kVersionMontaje);
    if (config.tempo != 1.0) {
        base += QStringLiteral("|tempo%1").arg(config.tempo, 0, 'f', 4);
    }
    if (config.pausaMaxima > 0) {
        base += QStringLiteral("|pausaMaxima%1").arg(config.pausaMaxima);
    }
    return sha256(base.toUtf8());
}

QString bloquesVoz(const Guion& guion, QList<BloqueVoz>& bloques)
{
    QSet<QString> ids;
    for (const Capitulo& c : guion.capitulos) {
        for (const Escena& e : c.escenas) {
            for (const Bloque& b : e.bloques) {
                BloqueVoz v;
                v.id = b.id;
                v.escena = nombreEscena(c, e);
                v.capitulo = c.id;
                v.idEscena = e.id;
                v.texto = b.textoHablado;
                v.textosLineas = b.textosLineas;
                for (int j = 0; j < b.cantidad; ++j) {
                    v.idsLineas.append(idEscena(c, e) + QLatin1Char('/') + e.lineas.at(b.primera + j).id);
                }
                if (ids.contains(v.id)) {
                    return QStringLiteral("id de bloque repetido \"%1\"").arg(v.id);
                }
                ids.insert(v.id);
                bloques.append(v);
            }
        }
    }
    return {};
}

QString leerCacheVoz(const QString& dirCache, QHash<QString, AudioCacheado>& bloques, QString* generacion)
{
    const QDir dir(dirCache);
    QFile puntero(dir.filePath(QStringLiteral("actual.json")));
    if (!puntero.exists()) {
        return {};
    }
    if (!puntero.open(QIODevice::ReadOnly)) {
        return QStringLiteral("no se pudo leer actual.json del cache de voz");
    }
    const QString gen = QJsonDocument::fromJson(puntero.readAll()).object().value(QStringLiteral("generacion")).toString();
    if (gen.isEmpty() || gen.contains(QLatin1Char('/')) || gen.startsWith(QLatin1Char('.'))) {
        return QStringLiteral("actual.json del cache de voz no es valido");
    }
    if (generacion != nullptr) {
        *generacion = gen;
    }
    const QDir dirGen(dir.filePath(QStringLiteral("generaciones/") + gen));
    QFile manifiesto(dirGen.filePath(QStringLiteral("manifiesto.json")));
    if (!manifiesto.open(QIODevice::ReadOnly)) {
        return QStringLiteral("falta el manifiesto de la generacion %1 del cache de voz").arg(gen);
    }
    const QJsonObject m = QJsonDocument::fromJson(manifiesto.readAll()).object();
    if (m.value(QStringLiteral("esquema")).toInt() != kEsquemaCacheVoz) {
        return {}; // cache por linea anterior: sin uso (D19)
    }
    const QJsonObject montaje = m.value(QStringLiteral("montaje")).toObject();
    const QJsonObject alineacion = m.value(QStringLiteral("alineacion")).toObject();
    for (auto it = alineacion.begin(); it != alineacion.end(); ++it) {
        const QJsonObject a = it.value().toObject();
        const QJsonObject mo = montaje.value(a.value(QStringLiteral("montaje")).toString()).toObject();
        AudioCacheado audio;
        audio.archivoPcm = dirGen.filePath(mo.value(QStringLiteral("pcm")).toString());
        audio.muestras = qint64(mo.value(QStringLiteral("muestras")).toDouble());
        audio.lufs = mo.value(QStringLiteral("lufs")).toDouble(-20.0);
        audio.pausasAcortadas = mo.value(QStringLiteral("pausasAcortadas")).toInt();
        for (const QJsonValue& v : a.value(QStringLiteral("marcas")).toArray()) {
            audio.marcas.append(qint64(v.toDouble()));
        }
        audio.metodo = a.value(QStringLiteral("metodo")).toString();
        audio.pausasEncontradas = int(a.value(QStringLiteral("pausasCandidatas")).toArray().size());
        audio.pausasEsperadas = a.value(QStringLiteral("pausasEsperadas")).toInt();
        if (mo.isEmpty() || audio.muestras <= 0 || audio.marcas.isEmpty() || !QFile::exists(audio.archivoPcm)) {
            return QStringLiteral("el bloque %1 del cache de voz esta incompleto").arg(it.key().left(12));
        }
        bloques.insert(it.key(), audio);
    }
    return {};
}

namespace {

struct Salida {
    int codigo = -1;
    QByteArray err;
};

Salida ejecutar(const QString& programa, const QStringList& args)
{
    QProcess p;
    p.setStandardOutputFile(QProcess::nullDevice());
    p.start(programa, args);
    Salida s;
    if (!p.waitForStarted(10000) || !p.waitForFinished(600000)) {
        p.kill();
        p.waitForFinished(5000);
        return s;
    }
    s.codigo = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    s.err = p.readAllStandardError();
    return s;
}

// Mediana de los LUFS de los bloques usados.
double mediana(std::vector<double> v)
{
    if (v.empty()) {
        return -20.0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

QString escribirMezcla(const Guion& guion, const ConfigVoz& config, const QHash<QString, AudioCacheado>& bloques,
                       const QString& ffmpeg, const QString& dirTrabajo, const QString& ruta)
{
    const qint64 total = qint64(guion.totalCuadros) * kMuestrasPorCuadro; // por canal
    std::vector<float> pista(size_t(total) * 2, 0.0f);

    // Ajuste moderado por bloque: +-3 dB hacia la mediana de los bloques.
    std::vector<double> niveles;
    for (const Capitulo& c : guion.capitulos) {
        for (const Escena& e : c.escenas) {
            for (const Bloque& b : e.bloques) {
                const auto it = bloques.constFind(claveBloque(claveSintesis(config, b.textoHablado), b.textosLineas, config));
                if (it != bloques.constEnd()) {
                    niveles.push_back(it->lufs);
                }
            }
        }
    }
    const double referencia = mediana(niveles);

    QHash<QString, QByteArray> leidos;
    for (const Capitulo& c : guion.capitulos) {
        for (const Escena& e : c.escenas) {
            for (const Linea& l : e.lineas) {
                const Bloque& b = e.bloques.at(l.bloque);
                const auto it =
                    bloques.constFind(claveBloque(claveSintesis(config, b.textoHablado), b.textosLineas, config));
                if (it == bloques.constEnd()) {
                    return nombreEscena(c, e) + QStringLiteral(", bloque '%1': falta su audio de voz").arg(b.id);
                }
                if (!leidos.contains(it->archivoPcm)) {
                    QFile f(it->archivoPcm);
                    if (!f.open(QIODevice::ReadOnly)) {
                        return nombreEscena(c, e) + QStringLiteral(", bloque '%1': no se pudo leer su audio").arg(b.id);
                    }
                    leidos.insert(it->archivoPcm, f.readAll());
                }
                const QByteArray& pcm = leidos[it->archivoPcm];
                const double ganancia = std::pow(10.0, std::clamp(referencia - it->lufs, -3.0, 3.0) / 20.0);
                const qint64 destino = qint64(e.inicio) * kMuestrasPorCuadro + l.inicioMuestra;
                const qint64 n = l.origenFin - l.origenInicio;
                const qint64 fundido = muestrasDeMs(5);
                if (destino < 0 || destino + n > total || l.origenFin * 4 > pcm.size()) {
                    return nombreEscena(c, e) + QStringLiteral(", bloque '%1': el audio se sale del video").arg(b.id);
                }
                for (qint64 k = 0; k < n; ++k) {
                    // Fundido de 5 ms en los extremos de cada tramo (los cortes
                    // caen en pausas entre frases; evita clics).
                    double g = ganancia;
                    if (k < fundido) {
                        g *= double(k) / double(fundido);
                    } else if (n - k < fundido) {
                        g *= double(n - k) / double(fundido);
                    }
                    for (int canal = 0; canal < 2; ++canal) {
                        const qint16 v = qFromLittleEndian<qint16>(pcm.constData() + ((l.origenInicio + k) * 2 + canal) * 2);
                        pista[size_t((destino + k) * 2 + canal)] += float(g * v / 32768.0);
                    }
                }
            }
        }
    }

    // Normalizacion de la pista completa (dos pasadas de loudnorm).
    const QString cruda = QDir(dirTrabajo).filePath(QStringLiteral("voz-cruda.f32"));
    {
        QFile f(cruda);
        const qint64 bytes = qint64(pista.size() * sizeof(float));
        if (!f.open(QIODevice::WriteOnly) || f.write(reinterpret_cast<const char*>(pista.data()), bytes) != bytes) {
            return QStringLiteral("no se pudo escribir la pista de voz");
        }
    }
    const QStringList entrada{QStringLiteral("-hide_banner"), QStringLiteral("-nostdin"), QStringLiteral("-f"),
                              QStringLiteral("f32le"), QStringLiteral("-ar"), QStringLiteral("48000"),
                              QStringLiteral("-ac"), QStringLiteral("2"), QStringLiteral("-i"), cruda};
    Salida s = ejecutar(ffmpeg, entrada + QStringList{QStringLiteral("-af"),
                                                      QStringLiteral("loudnorm=I=-16:TP=-2:LRA=11:print_format=json"),
                                                      QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});
    const QString texto = QString::fromUtf8(s.err);
    const qsizetype i = texto.lastIndexOf(QLatin1Char('{'));
    const qsizetype j = i >= 0 ? texto.indexOf(QLatin1Char('}'), i) : -1;
    const QJsonObject medida = j > i ? QJsonDocument::fromJson(texto.mid(i, j - i + 1).toUtf8()).object() : QJsonObject();
    if (s.codigo != 0 || medida.isEmpty() || medida.value(QStringLiteral("input_i")).toString().contains(QLatin1String("inf"))) {
        return QStringLiteral("ffmpeg no pudo medir el loudness de la pista de voz");
    }
    const auto m = [&](const char* k) { return medida.value(QLatin1String(k)).toString(); };
    const QString filtro = QStringLiteral("loudnorm=I=-16:TP=-2:LRA=11:measured_I=%1:measured_TP=%2:measured_LRA=%3:"
                                          "measured_thresh=%4:offset=%5:linear=true,aresample=48000")
                               .arg(m("input_i"), m("input_tp"), m("input_lra"), m("input_thresh"), m("target_offset"));
    s = ejecutar(ffmpeg, QStringList{QStringLiteral("-y"), QStringLiteral("-loglevel"), QStringLiteral("error")} + entrada
                             + QStringList{QStringLiteral("-af"), filtro, QStringLiteral("-f"), QStringLiteral("s16le"),
                                           QStringLiteral("-ar"), QStringLiteral("48000"), QStringLiteral("-ac"),
                                           QStringLiteral("2"), ruta});
    QFile::remove(cruda);
    if (s.codigo != 0) {
        return QStringLiteral("ffmpeg no pudo normalizar la pista de voz");
    }
    // Longitud exacta: la del video.
    if (!QFile::resize(ruta, total * 4)) {
        return QStringLiteral("no se pudo ajustar la longitud de la pista de voz");
    }
    return {};
}

} // namespace video
