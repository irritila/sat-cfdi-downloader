// satcfdi_video_voz (T017 D5, D9, D12, D19): paso de voz por bloques.
// Un bloque de narracion = un clip continuo (por omision, una escena). Por
// cada bloque del guion (o de la muestra):
//   1. Sintesis: solo si falta en el cache (unica parte con red y costo).
//   2. Montaje: recorte de extremos y medicion de loudness (local).
//   3. Alineacion: marca de inicio de cada linea dentro del clip (local;
//      silencedetect + estimacion proporcional + correcciones manuales).
// Publica una generacion nueva del cache de forma atomica. Es el unico
// ejecutable de la herramienta con cliente HTTP; la grabacion no tiene red.
//
// Uso:
//   satcfdi_video_voz --guion <guion.json> [--cache <dir>] [--motor openai|prueba]
//                     [--muestra <escena|capitulo/escena|.../linea,...>] [--estimar] [--confirmar]
// OPENAI_API_KEY solo se lee del entorno (D9) y solo con motor openai y
// bloques por sintetizar. Codigos: 0 ok; 1 error; 2 uso.

#include "Guion.h"
#include "MotoresTts.h"
#include "ProcesadoAudio.h"
#include "Voz.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/file.h>
#include <unistd.h>

#ifndef SATCFDI_BINARY_DIR
#error "SATCFDI_BINARY_DIR debe definirse en CMake"
#endif
#ifndef SATCFDI_FFMPEG
#error "SATCFDI_FFMPEG debe definirse en CMake"
#endif
#ifndef SATCFDI_FFPROBE
#error "SATCFDI_FFPROBE debe definirse en CMake"
#endif

using namespace video;

namespace {

// Referencia historica NO verificada (03-audio.md): USD 0.015 por minuto de
// audio. Solo orientativa; la tarifa vigente la confirma el usuario.
constexpr double kUsdPorMinuto = 0.015;
constexpr double kCaracteresPorSegundo = 15.0;

void error(const QString& m)
{
    std::fprintf(stderr, "error: %s\n", qPrintable(m));
}

void uso()
{
    std::fprintf(stderr,
                 "uso: satcfdi_video_voz --guion <guion.json> [--cache <dir>] [--motor openai|prueba]\n"
                 "                       [--muestra <escena|capitulo/escena|capitulo/escena/linea,...>]\n"
                 "                       [--estimar] [--confirmar] [--pausa-minima-ms 120]\n");
}

bool dentroDelBuild(const QString& ruta)
{
    const QString build = QDir(QStringLiteral(SATCFDI_BINARY_DIR)).canonicalPath();
    const QString c = QDir(ruta).canonicalPath();
    return !build.isEmpty() && !c.isEmpty() && (c == build || c.startsWith(build + QLatin1Char('/')));
}

// Enlace duro (sin copiar); si falla, copia.
bool enlazar(const QString& origen, const QString& destino)
{
    QDir().mkpath(QFileInfo(destino).absolutePath());
    if (::link(QFile::encodeName(origen).constData(), QFile::encodeName(destino).constData()) == 0) {
        return true;
    }
    return QFile::copy(origen, destino);
}

bool escribirAtomico(const QString& ruta, const QByteArray& datos)
{
    QSaveFile f(ruta);
    return f.open(QIODevice::WriteOnly) && f.write(datos) == datos.size() && f.commit();
}

int numeroGeneracion(const QString& id)
{
    return id.startsWith(QLatin1Char('g')) ? id.mid(1).toInt() : 0;
}

QString sha256(const QByteArray& datos)
{
    return QString::fromLatin1(QCryptographicHash::hash(datos, QCryptographicHash::Sha256).toHex());
}

// --muestra: escena, capitulo/escena (todos sus bloques) o una linea
// (escena/linea o capitulo/escena/linea: el bloque que la contiene).
bool coincide(const BloqueVoz& b, const QString& id)
{
    const QString escena = b.capitulo + QLatin1Char('/') + b.idEscena;
    if (id == b.idEscena || id == escena || id == b.id) {
        return true;
    }
    for (const QString& l : b.idsLineas) {
        if (id == l || b.capitulo + QLatin1Char('/') + id == l) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QString rutaGuion;
    QString dirCache = QDir(QStringLiteral(SATCFDI_BINARY_DIR)).filePath(QStringLiteral("video/tts-cache"));
    QString motor = QStringLiteral("openai");
    QStringList muestra;
    bool estimar = false;
    bool confirmar = false;
    int pausaMinimaMs = 120;
    const QStringList args = QCoreApplication::arguments();
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        const bool conValor = i + 1 < args.size();
        if (a == QLatin1String("--guion") && conValor) {
            rutaGuion = args.at(++i);
        } else if (a == QLatin1String("--cache") && conValor) {
            dirCache = args.at(++i);
        } else if (a == QLatin1String("--motor") && conValor) {
            motor = args.at(++i);
        } else if (a == QLatin1String("--muestra") && conValor) {
            muestra = args.at(++i).split(QLatin1Char(','), Qt::SkipEmptyParts);
        } else if (a == QLatin1String("--estimar")) {
            estimar = true;
        } else if (a == QLatin1String("--confirmar")) {
            confirmar = true;
        } else if (a == QLatin1String("--pausa-minima-ms") && conValor) {
            pausaMinimaMs = args.at(++i).toInt();
            if (pausaMinimaMs < 40 || pausaMinimaMs > 1000) {
                uso();
                return 2;
            }
        } else {
            uso();
            return 2;
        }
    }
    if (rutaGuion.isEmpty() || (motor != QLatin1String("openai") && motor != QLatin1String("prueba"))) {
        uso();
        return 2;
    }

    Guion guion;
    if (const QString e = leerGuion(rutaGuion, guion); !e.isEmpty()) {
        error(QStringLiteral("guion invalido: ") + e);
        return 1;
    }
    QList<BloqueVoz> bloques;
    if (const QString e = bloquesVoz(guion, bloques); !e.isEmpty()) {
        error(e);
        return 1;
    }
    const ConfigVoz config = configVoz(guion, motor);
    if (motor == QLatin1String("openai")) {
        if (const QString e = validarConfigOpenAi(config); !e.isEmpty()) {
            error(e);
            return 1;
        }
    }

    // Cache bajo el directorio de build (D1).
    if (!QDir().mkpath(dirCache) || !dentroDelBuild(dirCache)) {
        error(QStringLiteral("--cache debe estar dentro del directorio de build (%1)").arg(QLatin1String(SATCFDI_BINARY_DIR)));
        return 1;
    }
    dirCache = QDir(dirCache).canonicalPath();
    const QDir cache(dirCache);

    // Bloqueo exclusivo: un solo paso de voz a la vez sobre este cache.
    const int bloqueo = ::open(QFile::encodeName(cache.filePath(QStringLiteral(".bloqueo"))).constData(),
                               O_CREAT | O_RDWR, 0644);
    if (bloqueo < 0 || ::flock(bloqueo, LOCK_EX | LOCK_NB) != 0) {
        error(QStringLiteral("el cache de voz esta en uso por otro proceso"));
        return 1;
    }

    // Generacion publicada (un manifiesto por linea anterior a D19 se ignora).
    QString generacionActual;
    QJsonObject manifiestoActual;
    {
        QFile puntero(cache.filePath(QStringLiteral("actual.json")));
        if (puntero.open(QIODevice::ReadOnly)) {
            generacionActual =
                QJsonDocument::fromJson(puntero.readAll()).object().value(QStringLiteral("generacion")).toString();
        }
        QFile f(cache.filePath(QStringLiteral("generaciones/%1/manifiesto.json").arg(generacionActual)));
        if (!generacionActual.isEmpty() && f.open(QIODevice::ReadOnly)) {
            const QJsonObject m = QJsonDocument::fromJson(f.readAll()).object();
            if (m.value(QStringLiteral("esquema")).toInt() == kEsquemaCacheVoz) {
                manifiestoActual = m;
            }
        }
    }
    QJsonObject sintesis = manifiestoActual.value(QStringLiteral("sintesis")).toObject();
    QJsonObject montaje = manifiestoActual.value(QStringLiteral("montaje")).toObject();
    QJsonObject alineacion = manifiestoActual.value(QStringLiteral("alineacion")).toObject();

    // Correcciones manuales de marcas: {"capitulo/escena/linea": segundos}.
    QJsonObject correcciones;
    {
        QFile f(cache.filePath(QStringLiteral("alineacion/correcciones.json")));
        if (f.exists()) {
            QJsonParseError pe{};
            const QJsonDocument d = QJsonDocument::fromJson(f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(), &pe);
            if (pe.error != QJsonParseError::NoError || !d.isObject()) {
                error(QStringLiteral("alineacion/correcciones.json no es un objeto JSON valido"));
                return 1;
            }
            correcciones = d.object();
        }
    }

    // Bloques pedidos (todos o la muestra D12).
    QList<BloqueVoz> pedidos;
    if (muestra.isEmpty()) {
        pedidos = bloques;
    } else {
        for (const QString& id : muestra) {
            bool alguno = false;
            for (const BloqueVoz& b : bloques) {
                if (coincide(b, id)) {
                    alguno = true;
                    if (std::none_of(pedidos.cbegin(), pedidos.cend(), [&](const BloqueVoz& x) { return x.id == b.id; })) {
                        pedidos.append(b);
                    }
                }
            }
            if (!alguno) {
                error(QStringLiteral("--muestra: \"%1\" no es una escena, bloque ni linea del guion").arg(id));
                return 1;
            }
        }
    }

    // Sintesis que faltan (bloques con el mismo texto comparten clip).
    QStringList faltantes;
    QHash<QString, QStringList> idsPorClave;
    QHash<QString, QString> textoPorClave;
    qsizetype caracteres = 0;
    for (const BloqueVoz& b : pedidos) {
        const QString clave = claveSintesis(config, b.texto);
        idsPorClave[clave].append(b.id);
        textoPorClave.insert(clave, b.texto);
        if (!sintesis.contains(clave) && !faltantes.contains(clave)) {
            faltantes.append(clave);
            caracteres += b.texto.size();
        }
    }
    const double minutos = double(caracteres) / kCaracteresPorSegundo / 60.0;
    const QString pausaMaxima =
        config.pausaMaxima > 0 ? QStringLiteral("%1 ms").arg(config.pausaMaxima) : QStringLiteral("sin limite");
    std::printf("motor: %s; modelo: %s; voz: %s; tempo de montaje: %.2f; pausa maxima: %s\n", qPrintable(config.motor),
                qPrintable(config.modelo), qPrintable(config.voz), config.tempo, qPrintable(pausaMaxima));
    std::printf("bloques pedidos: %lld; por sintetizar: %lld (%lld caracteres, ~%.1f min de audio)\n",
                static_cast<long long>(pedidos.size()), static_cast<long long>(faltantes.size()),
                static_cast<long long>(caracteres), minutos);
    if (config.motor == QLatin1String("openai")) {
        std::printf("costo estimado: ~USD %.3f (referencia no verificada: USD %.3f/min; confirma la tarifa vigente)\n",
                    minutos * kUsdPorMinuto, kUsdPorMinuto);
    }
    for (const QString& clave : faltantes) {
        std::printf("  %s (%lld caracteres): %s\n", qPrintable(idsPorClave.value(clave).join(QStringLiteral(", "))),
                    static_cast<long long>(textoPorClave.value(clave).size()), qPrintable(textoPorClave.value(clave)));
    }
    std::fflush(stdout);
    if (estimar) {
        std::printf("estimacion: sin llamadas\n");
        return 0;
    }

    std::unique_ptr<MotorTts> motorTts;
    if (!faltantes.isEmpty()) {
        if (config.motor == QLatin1String("openai")) {
            const QByteArray clave = qgetenv("OPENAI_API_KEY");
            if (clave.trimmed().isEmpty()) {
                error(QStringLiteral("falta OPENAI_API_KEY en el entorno (D9); no se hizo ninguna llamada"));
                return 1;
            }
            if (!confirmar) {
                if (!::isatty(STDIN_FILENO)) {
                    error(QStringLiteral("sin terminal interactiva: confirma con --confirmar (D12)"));
                    return 1;
                }
                std::printf("Se haran %lld llamadas a la API de OpenAI con costo para tu cuenta. Escribe \"si\" para "
                            "continuar: ",
                            static_cast<long long>(faltantes.size()));
                std::fflush(stdout);
                std::string respuesta;
                std::getline(std::cin, respuesta);
                if (respuesta != "si") {
                    std::printf("cancelado: sin llamadas\n");
                    return 0;
                }
            }
            motorTts = motorOpenAi(clave);
        } else {
            motorTts = motorPrueba();
        }
    }

    // Staging: la generacion nueva = la actual (por enlace duro) + lo nuevo.
    const int siguiente = numeroGeneracion(generacionActual) + 1;
    const QString idGeneracion = QStringLiteral("g%1").arg(siguiente, 6, 10, QLatin1Char('0'));
    const QString dirStaging = cache.filePath(QStringLiteral(".preparando-") + idGeneracion);
    QDir(dirStaging).removeRecursively();
    const QDir staging(dirStaging);
    const auto abortar = [&](const QString& m) {
        QDir(dirStaging).removeRecursively();
        error(m + QStringLiteral(" (no se publico nada: el cache de voz queda como estaba)"));
        return 1;
    };
    const QDir dirActual(cache.filePath(QStringLiteral("generaciones/") + generacionActual));
    for (const QJsonObject* capa : {&sintesis, &montaje}) {
        for (auto it = capa->begin(); it != capa->end(); ++it) {
            const QJsonObject entrada = it.value().toObject();
            QStringList rels{entrada.value(capa == &sintesis ? QStringLiteral("wav") : QStringLiteral("pcm")).toString()};
            if (capa == &montaje && entrada.contains(QStringLiteral("pcmAlineacion"))) {
                rels.append(entrada.value(QStringLiteral("pcmAlineacion")).toString());
            }
            for (const QString& rel : rels) {
                if (!enlazar(dirActual.filePath(rel), staging.filePath(rel))) {
                    return abortar(QStringLiteral("no se pudo conservar %1").arg(rel));
                }
            }
        }
    }

    // 1) Sintesis (red solo con motor openai).
    const Herramientas herramientas{QStringLiteral(SATCFDI_FFMPEG), QStringLiteral(SATCFDI_FFPROBE)};
    int sintetizados = 0;
    for (const QString& clave : faltantes) {
        const QString ids = idsPorClave.value(clave).join(QStringLiteral(", "));
        std::printf("sintetizando %s...\n", qPrintable(ids));
        std::fflush(stdout);
        QByteArray audio;
        if (const QString e = motorTts->sintetizar(config, textoPorClave.value(clave), audio); !e.isEmpty()) {
            return abortar(QStringLiteral("bloque %1: %2").arg(idsPorClave.value(clave).first(), e));
        }
        const QString wav = QStringLiteral("sintesis/%1.wav").arg(clave);
        QDir().mkpath(staging.filePath(QStringLiteral("sintesis")));
        if (!escribirAtomico(staging.filePath(wav), audio)) {
            return abortar(QStringLiteral("no se pudo guardar el audio de %1").arg(ids));
        }
        sintesis.insert(clave, QJsonObject{{QStringLiteral("bloques"), QJsonArray::fromStringList(idsPorClave.value(clave))},
                                           {QStringLiteral("texto"), textoPorClave.value(clave)},
                                           {QStringLiteral("motor"), config.motor},
                                           {QStringLiteral("modelo"), config.modelo},
                                           {QStringLiteral("voz"), config.voz},
                                           {QStringLiteral("instrucciones"), config.instrucciones},
                                           {QStringLiteral("formato"), config.formato},
                                           {QStringLiteral("velocidad"), config.velocidad},
                                           {QStringLiteral("sha256Audio"), sha256(audio)},
                                           {QStringLiteral("wav"), wav}});
        ++sintetizados;
    }

    // 2) Montaje y 3) alineacion (locales, sin TTS).
    int montados = 0;
    int alineados = 0;
    QStringList erroresAlineacion;
    for (const BloqueVoz& b : pedidos) {
        const QString ks = claveSintesis(config, b.texto);
        const QJsonObject s = sintesis.value(ks).toObject();
        const QString shaAudio = s.value(QStringLiteral("sha256Audio")).toString();
        const QString km = claveMontaje(shaAudio, config);
        if (!montaje.contains(km)) {
            const QString pcm = QStringLiteral("montaje/%1.s16").arg(km);
            QDir().mkpath(staging.filePath(QStringLiteral("montaje")));
            // Audio sin comprimir: la alineacion detecta ahi las pausas. Solo
            // se conserva si la compresion de pausas quito algo.
            const QString pcmAlineacion = QStringLiteral("montaje/%1.sin-comprimir.s16").arg(km);
            QJsonObject meta;
            MapaCompresion mapa;
            if (const QString e = prepararBloque(herramientas, staging.filePath(s.value(QStringLiteral("wav")).toString()),
                                                 config.tempo, config.pausaMaxima, staging.filePath(pcm),
                                                 staging.filePath(pcmAlineacion), meta, mapa);
                !e.isEmpty()) {
                return abortar(QStringLiteral("%1, bloque '%2': %3").arg(b.escena, b.id, e));
            }
            meta.insert(QStringLiteral("pcm"), pcm);
            if (mapa.cortes.isEmpty()) {
                QFile::remove(staging.filePath(pcmAlineacion));
            } else {
                meta.insert(QStringLiteral("pcmAlineacion"), pcmAlineacion);
            }
            meta.insert(QStringLiteral("sha256Audio"), shaAudio);
            meta.insert(QStringLiteral("version"), kVersionMontaje);
            montaje.insert(km, meta);
            ++montados;
        }
        const QJsonObject mo = montaje.value(km).toObject();
        const qint64 muestras = qint64(mo.value(QStringLiteral("muestras")).toDouble());
        const MapaCompresion mapa = MapaCompresion::desdeJson(mo.value(QStringLiteral("mapa")).toArray());
        const QString pcmDeteccion = mo.value(QStringLiteral("pcmAlineacion")).toString(mo.value(QStringLiteral("pcm")).toString());
        const qint64 muestrasDeteccion =
            qint64(mo.value(QStringLiteral("muestrasSinComprimir")).toDouble(double(muestras)));

        // Correcciones de este bloque (segundos -> muestras).
        QList<std::optional<qint64>> corr;
        QJsonObject corrUsadas;
        for (const QString& idLinea : b.idsLineas) {
            const QJsonValue v = correcciones.value(idLinea);
            if (v.isDouble()) {
                corr.append(qint64(v.toDouble() * kMuestrasPorSegundo));
                corrUsadas.insert(idLinea, v);
            } else {
                corr.append(std::nullopt);
            }
        }
        const QString kb = claveBloque(ks, b.textosLineas, config);
        const QString firma = sha256(QJsonDocument(QJsonObject{{QStringLiteral("montaje"), km},
                                                               {QStringLiteral("lineas"),
                                                                QJsonArray::fromStringList(b.textosLineas)},
                                                                               {QStringLiteral("version"), kVersionAlineacion},
                                                               {QStringLiteral("pausaMinimaMs"), pausaMinimaMs},
                                                               {QStringLiteral("correcciones"), corrUsadas}})
                                         .toJson(QJsonDocument::Compact));
        if (alineacion.value(kb).toObject().value(QStringLiteral("firma")).toString() == firma) {
            continue; // vigente
        }
        Alineacion a;
        if (const QString e = alinearBloque(herramientas, staging.filePath(pcmDeteccion), muestrasDeteccion,
                                            b.textosLineas, b.idsLineas, corr, pausaMinimaMs, mapa, muestras, a);
            !e.isEmpty()) {
            erroresAlineacion.append(QStringLiteral("%1, bloque '%2': %3").arg(b.escena, b.id, e));
            alineacion.remove(kb);
            continue;
        }
        QJsonArray marcas;
        QJsonArray marcasS;
        for (const qint64 m : a.marcas) {
            marcas.append(double(m));
            marcasS.append(double(m) / kMuestrasPorSegundo);
        }
        QJsonArray pausas;
        for (const auto& p : a.pausas) {
            pausas.append(QJsonArray{double(p.first) / kMuestrasPorSegundo, double(p.second) / kMuestrasPorSegundo});
        }
        alineacion.insert(kb, QJsonObject{{QStringLiteral("bloque"), b.id},
                                          {QStringLiteral("lineas"), QJsonArray::fromStringList(b.idsLineas)},
                                          {QStringLiteral("sintesis"), ks},
                                          {QStringLiteral("montaje"), km},
                                          {QStringLiteral("marcas"), marcas},
                                          {QStringLiteral("marcasSegundos"), marcasS},
                                          {QStringLiteral("pausasCandidatas"), pausas},
                                          {QStringLiteral("correcciones"), corrUsadas},
                                          {QStringLiteral("metodo"), a.metodo},
                                          {QStringLiteral("pausasEsperadas"), a.pausasEsperadas},
                                          {QStringLiteral("pausaMinimaMs"), pausaMinimaMs},
                                          {QStringLiteral("version"), kVersionAlineacion},
                                          {QStringLiteral("firma"), firma}});
        ++alineados;
        // Marcas legibles (para corregir sin resintetizar).
        QStringList legibles;
        for (int j = 0; j < a.marcas.size(); ++j) {
            legibles.append(QStringLiteral("%1=%2 s").arg(b.idsLineas.at(j)).arg(double(a.marcas.at(j)) / kMuestrasPorSegundo, 0, 'f', 2));
        }
        std::printf("alineado %s [%s; pausas %lld de %d esperadas; %lld acortadas]: %s\n", qPrintable(b.id),
                    qPrintable(a.metodo), static_cast<long long>(a.pausas.size()), a.pausasEsperadas,
                    static_cast<long long>(mapa.cortes.size()), qPrintable(legibles.join(QStringLiteral("; "))));
        if (a.metodo.startsWith(QLatin1String("respaldo"))) {
            std::printf("aviso: %s usa un respaldo (pausas %lld, frases-1 %d); revisa sus marcas\n",
                        qPrintable(b.id), static_cast<long long>(a.pausas.size()), a.pausasEsperadas);
        }
    }

    const bool cambio = sintetizados > 0 || montados > 0 || alineados > 0
                        || alineacion.size() != manifiestoActual.value(QStringLiteral("alineacion")).toObject().size();
    if (!cambio) {
        QDir(dirStaging).removeRecursively();
        std::printf("sintetizados: 0; montados: 0; alineados: 0 (cache completo, sin llamadas)\n");
    } else {
        const QJsonObject manifiesto{{QStringLiteral("esquema"), kEsquemaCacheVoz},
                                     {QStringLiteral("generacion"), idGeneracion},
                                     {QStringLiteral("versionMontaje"), kVersionMontaje},
                                     {QStringLiteral("versionAlineacion"), kVersionAlineacion},
                                     {QStringLiteral("ffmpeg"), versionFfmpeg(herramientas)},
                                     {QStringLiteral("sintesis"), sintesis},
                                     {QStringLiteral("montaje"), montaje},
                                     {QStringLiteral("alineacion"), alineacion}};
        if (!escribirAtomico(staging.filePath(QStringLiteral("manifiesto.json")),
                             QJsonDocument(manifiesto).toJson(QJsonDocument::Indented))) {
            return abortar(QStringLiteral("no se pudo escribir el manifiesto"));
        }
        // Publicacion: la generacion entra con un rename y el puntero unico
        // (actual.json) se reemplaza de forma atomica.
        if (!QDir().mkpath(cache.filePath(QStringLiteral("generaciones")))) {
            return abortar(QStringLiteral("no se pudo crear generaciones/"));
        }
        const QString dirFinal = cache.filePath(QStringLiteral("generaciones/") + idGeneracion);
        QDir(dirFinal).removeRecursively(); // restos de una corrida interrumpida, nunca publicados
        if (std::rename(QFile::encodeName(dirStaging).constData(), QFile::encodeName(dirFinal).constData()) != 0) {
            return abortar(QStringLiteral("no se pudo mover la generacion: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        }
        if (!escribirAtomico(cache.filePath(QStringLiteral("actual.json")),
                             QJsonDocument(QJsonObject{{QStringLiteral("generacion"), idGeneracion}}).toJson())) {
            QDir(dirFinal).removeRecursively();
            error(QStringLiteral("no se pudo publicar actual.json (el cache de voz queda como estaba)"));
            return 1;
        }
        // Se conservan la generacion publicada y la anterior.
        const QDir generaciones(cache.filePath(QStringLiteral("generaciones")));
        for (const QString& g : generaciones.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (numeroGeneracion(g) < siguiente - 1) {
                QDir(generaciones.filePath(g)).removeRecursively();
            }
        }
        std::printf("sintetizados: %d; montados: %d; alineados: %d; generacion %s publicada en %s\n", sintetizados,
                    montados, alineados, qPrintable(idGeneracion), qPrintable(dirCache));
        // Para escuchar la muestra (D12): el WAV original de cada bloque nuevo.
        for (const QString& clave : faltantes) {
            std::printf("  %s: %s\n", qPrintable(idsPorClave.value(clave).join(QStringLiteral(", "))),
                        qPrintable(QDir(dirFinal).filePath(QStringLiteral("sintesis/%1.wav").arg(clave))));
        }
    }
    if (!erroresAlineacion.isEmpty()) {
        // El audio sintetizado queda publicado: corregir las marcas en
        // alineacion/correcciones.json y repetir este paso no llama a TTS.
        for (const QString& e : erroresAlineacion) {
            error(QStringLiteral("alineacion: ") + e);
        }
        return 1;
    }
    return 0;
}
