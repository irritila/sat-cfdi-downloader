// satcfdi_video_tutorial (T017): graba el video tutorial a partir del guion
// versionado (README.md). Carga el modulo QML real (SatCfdiDownloader) con
// PresentacionViewModels sobre servicios fijos mutables (DatosDemo.h) y graba
// cuadro a cuadro con ffmpeg (Grabador.h). Sin SQLite, worker, SAT, Keychain
// ni red. Corte 1: video sin voz.
//
// Uso:
//   satcfdi_video_tutorial --guion <guion.json> --salida <dir> [--nombre <base>]
//                          [--rapido] [--offline] [--forzar-pdf fallo|sin-respuesta]
//                          [--ffmpeg <ruta>] [--voz <cache> [--motor-voz openai|prueba]]
//                          [--escenas <escena|capitulo/escena,...>]
//   satcfdi_video_tutorial --guion <guion.json> --validar [--detalle] [--voz <cache>]
//   satcfdi_video_tutorial --sonda-red   (prueba del sandbox sin red, D15)
// Codigos de salida: 0 ok, 1 error, 2 uso, 77 sin sesion grafica (SKIP, D17).

#include "DatosDemo.h"
#include "Grabador.h"
#include "Guion.h"
#include "Musica.h"
#include "Voz.h"

#include "presentation/estilo/EstiloVisual.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QSet>
#include <QFileInfo>
#include <QLocale>
#include <QStyleHints>
#include <QTemporaryDir>

#include <CoreGraphics/CoreGraphics.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <ctime>

#ifndef SATCFDI_FFMPEG
#define SATCFDI_FFMPEG "ffmpeg"
#endif
#ifndef SATCFDI_BINARY_DIR
#error "SATCFDI_BINARY_DIR debe definirse en CMake"
#endif

namespace {

constexpr int kSkip = 77;

void uso()
{
    std::fprintf(stderr,
                 "uso: satcfdi_video_tutorial --guion <guion.json> --salida <dir> [--nombre <base>]\n"
                 "                            [--rapido] [--offline] [--forzar-pdf fallo|sin-respuesta]\n"
                 "                            [--ffmpeg <ruta>] [--voz <cache> [--motor-voz openai|prueba]]\n"
                 "                            [--escenas <escena|capitulo/escena,...>] [--cache-musica <dir>]\n"
                 "     satcfdi_video_tutorial --guion <guion.json> --voz <cache> --solo-audio <dir> [--escenas ...]\n"
                 "     satcfdi_video_tutorial --guion <guion.json> --validar [--detalle] [--voz <cache>]\n"
                 "     satcfdi_video_tutorial --sonda-red\n");
}

// D15: intenta una conexion TCP real (servidor y cliente en 127.0.0.1). Sin
// sandbox debe funcionar ("RED DISPONIBLE"); dentro de sin-red.sb debe fallar
// ("RED BLOQUEADA"). Demuestra que el sandbox niega sockets de C++, no solo
// las URL de QML.
int sondaRed()
{
    const auto bloqueada = [](const char* paso) {
        std::printf("RED BLOQUEADA: %s: %s\n", paso, std::strerror(errno));
        return 3;
    };
    // 1) Conexion saliente a 127.0.0.1:1: sin sandbox, "rechazada" (ECONNREFUSED);
    //    dentro de sin-red.sb, "no permitida" (EPERM) antes de tocar la red.
    {
        const int s = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) {
            return bloqueada("socket");
        }
        sockaddr_in destino{};
        destino.sin_family = AF_INET;
        destino.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        destino.sin_port = htons(1);
        const int r = ::connect(s, reinterpret_cast<sockaddr*>(&destino), sizeof(destino));
        const int e = errno;
        ::close(s);
        if (r != 0 && (e == EPERM || e == EACCES)) {
            errno = e;
            return bloqueada("connect saliente");
        }
    }
    // 2) Servidor y cliente locales: la conexion debe completarse sin sandbox.
    const int servidor = ::socket(AF_INET, SOCK_STREAM, 0);
    if (servidor < 0) {
        return bloqueada("socket");
    }
    sockaddr_in dir{};
    dir.sin_family = AF_INET;
    dir.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dir.sin_port = 0;
    socklen_t largo = sizeof(dir);
    if (::bind(servidor, reinterpret_cast<sockaddr*>(&dir), sizeof(dir)) != 0 || ::listen(servidor, 1) != 0
        || ::getsockname(servidor, reinterpret_cast<sockaddr*>(&dir), &largo) != 0) {
        const int r = bloqueada("bind/listen");
        ::close(servidor);
        return r;
    }
    const int cliente = ::socket(AF_INET, SOCK_STREAM, 0);
    if (cliente < 0) {
        ::close(servidor);
        return bloqueada("socket cliente");
    }
    const bool conecto = ::connect(cliente, reinterpret_cast<sockaddr*>(&dir), sizeof(dir)) == 0;
    const int r = conecto ? 0 : bloqueada("connect");
    ::close(cliente);
    ::close(servidor);
    if (conecto) {
        std::printf("RED DISPONIBLE\n");
    }
    return r;
}

// D10: --salida debe quedar bajo el directorio de build (CMAKE_BINARY_DIR).
bool dentroDelBuild(const QString& ruta)
{
    const QString build = QDir(QStringLiteral(SATCFDI_BINARY_DIR)).canonicalPath();
    const QString canonica = QDir(ruta).canonicalPath();
    return !build.isEmpty() && !canonica.isEmpty()
           && (canonica == build || canonica.startsWith(build + QLatin1Char('/')));
}

// D17: sesion grafica de macOS con acceso a la pantalla (consola). Sin ella
// (ssh, CI, sesion bloqueada en segundo plano) la herramienta hace SKIP.
// SATCFDI_VIDEO_SIN_SESION=1 simula la ausencia para probar el SKIP.
bool haySesionGrafica()
{
    if (qEnvironmentVariableIntValue("SATCFDI_VIDEO_SIN_SESION") == 1) {
        return false;
    }
    CFDictionaryRef sesion = CGSessionCopyCurrentDictionary();
    if (sesion == nullptr) {
        return false;
    }
    const void* enConsola = CFDictionaryGetValue(sesion, kCGSessionOnConsoleKey);
    const bool ok = enConsola != nullptr && CFGetTypeID(enConsola) == CFBooleanGetTypeID()
                    && CFBooleanGetValue(static_cast<CFBooleanRef>(enConsola));
    CFRelease(sesion);
    return ok;
}

} // namespace

int main(int argc, char* argv[])
{
    QString rutaGuion;
    video::OpcionesGrabacion opciones;
    opciones.ffmpeg = QStringLiteral(SATCFDI_FFMPEG);
    bool validar = false;
    bool detalle = false; // --validar --detalle: tiempos por escena, bloque y linea
    QString forzarPdf;
    QString dirVoz;
    QString dirCacheMusica; // por omision <voz>/../musica-cache (independiente del TTS)
    QString dirSoloAudio;   // escribe voz.s16 y pista.s16 sin grabar (pruebas, R4)
    QStringList escenasPedidas;
    QString motorVoz = QStringLiteral("openai");
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        const bool conValor = i + 1 < argc;
        if (a == QLatin1String("--guion") && conValor) {
            rutaGuion = QString::fromLocal8Bit(argv[++i]);
        } else if (a == QLatin1String("--salida") && conValor) {
            opciones.dirSalida = QString::fromLocal8Bit(argv[++i]);
        } else if (a == QLatin1String("--nombre") && conValor) {
            opciones.nombre = QString::fromLocal8Bit(argv[++i]);
        } else if (a == QLatin1String("--ffmpeg") && conValor) {
            opciones.ffmpeg = QString::fromLocal8Bit(argv[++i]);
        } else if (a == QLatin1String("--forzar-pdf") && conValor) {
            forzarPdf = QString::fromLocal8Bit(argv[++i]);
            if (forzarPdf != QLatin1String("fallo") && forzarPdf != QLatin1String("sin-respuesta")) {
                uso();
                return 2;
            }
        } else if (a == QLatin1String("--escenas") && conValor) {
            escenasPedidas = QString::fromLocal8Bit(argv[++i]).split(QLatin1Char(','), Qt::SkipEmptyParts);
        } else if (a == QLatin1String("--cache-musica") && conValor) {
            dirCacheMusica = QString::fromLocal8Bit(argv[++i]);
        } else if (a == QLatin1String("--solo-audio") && conValor) {
            dirSoloAudio = QString::fromLocal8Bit(argv[++i]);
        } else if (a == QLatin1String("--voz") && conValor) {
            dirVoz = QString::fromLocal8Bit(argv[++i]);
        } else if (a == QLatin1String("--motor-voz") && conValor) {
            motorVoz = QString::fromLocal8Bit(argv[++i]);
            if (motorVoz != QLatin1String("openai") && motorVoz != QLatin1String("prueba")) {
                uso();
                return 2;
            }
        } else if (a == QLatin1String("--rapido")) {
            opciones.rapido = true;
        } else if (a == QLatin1String("--offline")) {
            opciones.offline = true;
        } else if (a == QLatin1String("--validar")) {
            validar = true;
        } else if (a == QLatin1String("--detalle")) {
            detalle = true;
        } else if (a == QLatin1String("--sonda-red") && argc == 2) {
            return sondaRed();
        } else {
            uso();
            return 2;
        }
    }
    if (rutaGuion.isEmpty() || (!validar && opciones.dirSalida.isEmpty() && dirSoloAudio.isEmpty())
        || (!dirSoloAudio.isEmpty() && (dirVoz.isEmpty() || validar || opciones.rapido))
        || (opciones.rapido && !dirVoz.isEmpty())
        || opciones.nombre.isEmpty() || opciones.nombre.contains(QLatin1Char('/'))
        || opciones.nombre.startsWith(QLatin1Char('.'))) {
        uso();
        return 2;
    }

    // D13: validacion previa a grabar (sin interfaz grafica).
    video::Guion guion;
    if (const QString e = video::leerGuion(rutaGuion, guion); !e.isEmpty()) {
        std::fprintf(stderr, "guion invalido: %s\n", qPrintable(e));
        return 1;
    }
    // --escenas: solo esas escenas salen en el video (muestra montada, D12).
    // Las demas se ejecutan igual (para dejar la app en el mismo estado) pero
    // sus cuadros no se graban; sin voz propia usan la duracion estimada.
    QSet<QString> incluidas;
    for (const QString& id : escenasPedidas) {
        bool alguna = false;
        for (const video::Capitulo& c : guion.capitulos) {
            for (const video::Escena& e : c.escenas) {
                if (id == e.id || id == video::idEscena(c, e)) {
                    incluidas.insert(video::idEscena(c, e));
                    alguna = true;
                }
            }
        }
        if (!alguna) {
            std::fprintf(stderr, "error: --escenas: \"%s\" no es una escena del guion\n", qPrintable(id));
            return 2;
        }
    }
    video::OpcionesPlan plan;
    // Sin --voz: audio estimado por longitud del texto (video sin audio).
    // Con --voz: el de los bloques alineados del cache (D19); si falta uno,
    // la planificacion falla nombrando escena y bloque. Nunca se llama a
    // OpenAI aqui.
    QHash<QString, video::AudioCacheado> audiosVoz;
    const video::ConfigVoz configVoz = video::configVoz(guion, motorVoz);
    if (!dirVoz.isEmpty()) {
        QString generacion;
        if (const QString e = video::leerCacheVoz(dirVoz, audiosVoz, &generacion); !e.isEmpty()) {
            std::fprintf(stderr, "error: %s\n", qPrintable(e));
            return 1;
        }
        plan.audioBloque = [&](const video::Capitulo& c, const video::Escena& e,
                               const video::Bloque& b) -> std::optional<video::AudioBloque> {
            const auto it = audiosVoz.constFind(
                video::claveBloque(video::claveSintesis(configVoz, b.textoHablado), b.textosLineas, configVoz));
            if (it != audiosVoz.constEnd()) {
                return video::AudioBloque{it->muestras, it->marcas};
            }
            if (!incluidas.isEmpty() && !incluidas.contains(video::idEscena(c, e))) {
                return video::estimarAudioBloque(b); // no sale en el video
            }
            return std::nullopt;
        };
        std::printf("voz: cache %s, generacion %s (motor %s)\n", qPrintable(dirVoz),
                    qPrintable(generacion.isEmpty() ? QStringLiteral("ninguna") : generacion), qPrintable(motorVoz));
    }
    plan.escala = opciones.rapido ? 0.1 : 1.0;
    // Una muestra de escenas no es el video completo: sin presupuestos.
    plan.validarPresupuestos = !opciones.rapido && incluidas.isEmpty();
    if (const QString e = video::planificar(guion, plan); !e.isEmpty()) {
        std::fprintf(stderr, "guion invalido: %s\n", qPrintable(e));
        return 1;
    }
    // Linea de tiempo de las salidas (.srt, capitulos, mezcla): el guion
    // completo o solo las escenas pedidas, consecutivas.
    const video::Guion salida = incluidas.isEmpty() ? guion : video::recortarGuion(guion, incluidas);
    opciones.escenas = incluidas;
    if (validar && detalle) {
        for (const video::Capitulo& c : salida.capitulos) {
            for (const video::Escena& e : c.escenas) {
                std::printf("%s: inicio %d, dura %d [%d, %d]\n", qPrintable(video::idEscena(c, e)), e.inicio, e.duracion,
                            e.presupuestoMin, e.presupuestoMax);
                for (const video::Bloque& b : e.bloques) {
                    const auto it = audiosVoz.constFind(
                        video::claveBloque(video::claveSintesis(configVoz, b.textoHablado), b.textosLineas, configVoz));
                    if (it != audiosVoz.constEnd()) {
                        std::printf("  bloque %s: alineacion %s, pausas %d de %d esperadas, %d acortadas%s\n",
                                    qPrintable(b.id), qPrintable(it->metodo), it->pausasEncontradas,
                                    it->pausasEsperadas, it->pausasAcortadas,
                                    it->metodo.startsWith(QLatin1String("respaldo")) ? " (AVISO: respaldo)" : "");
                    }
                }
                for (const video::Linea& l : e.lineas) {
                    std::printf("  %-12s bloque %-40s cue %6d +%4d  audio %.3f s\n", qPrintable(l.id),
                                qPrintable(e.bloques.at(l.bloque).id), e.inicio + l.inicio, l.duracion,
                                double(qint64(e.inicio) * video::kMuestrasPorCuadro + l.inicioMuestra)
                                    / video::kMuestrasPorSegundo);
                }
            }
        }
    }
    // R4: musica de fondo (raiz "musica"; ausente o "activa": false = sin
    // musica y la pista sale exactamente igual que antes).
    if (salida.musica.activa) {
        const qint64 total = qint64(salida.totalCuadros) * video::kMuestrasPorCuadro;
        if (video::muestrasDeMs(salida.musica.entradaMs + salida.musica.salidaMs) > total) {
            std::fprintf(stderr, "guion invalido: musica: los fundidos (%d + %d ms) no caben en %.2f s\n",
                         salida.musica.entradaMs, salida.musica.salidaMs, double(total) / video::kMuestrasPorSegundo);
            return 1;
        }
        std::printf("musica: %s, cama %.1f LUFS, duck %.1f dB (ataque %d, retencion %d, liberacion %d ms), "
                    "fundidos %d/%d ms%s\n",
                    qPrintable(salida.musica.preset), salida.musica.nivelLufs, salida.musica.duckDb,
                    salida.musica.ataqueMs, salida.musica.retencionMs, salida.musica.liberacionMs,
                    salida.musica.entradaMs, salida.musica.salidaMs,
                    dirVoz.isEmpty() ? "; omitida: sin --voz no hay pista de audio" : "");
    }
    if (validar) {
        int lineas = 0;
        for (const video::Capitulo& c : guion.capitulos) {
            for (const video::Escena& e : c.escenas) {
                lineas += int(e.lineas.size());
            }
        }
        std::printf("OK guion %s: %lld capitulos, %d lineas, %d cuadros (%.2f s)\n", qPrintable(guion.titulo),
                    static_cast<long long>(guion.capitulos.size()), lineas, guion.totalCuadros,
                    double(guion.totalCuadros) / video::kFps);
        return 0;
    }

    // Pista de audio: voz (D13/D19, normalizada como pista) y, si hay musica,
    // la suma voz + musica (la voz no cambia). Devuelve la ruta en `pista`.
    const bool conMusica = salida.musica.activa && !dirVoz.isEmpty();
    if (dirCacheMusica.isEmpty() && !dirVoz.isEmpty()) {
        dirCacheMusica = QDir::cleanPath(QDir(dirVoz).absoluteFilePath(QStringLiteral("../musica-cache")));
    }
    const auto prepararAudio = [&](const QString& dirTrabajo, QString& pista) -> QString {
        const QString voz = QDir(dirTrabajo).filePath(QStringLiteral("voz.s16"));
        if (const QString e = video::escribirMezcla(salida, configVoz, audiosVoz, opciones.ffmpeg, dirTrabajo, voz);
            !e.isEmpty()) {
            return e;
        }
        pista = voz;
        if (!conMusica) {
            return {};
        }
        pista = QDir(dirTrabajo).filePath(QStringLiteral("pista.s16"));
        video::InformeMusica m;
        if (const QString e = video::mezclarMusica(salida, voz, dirCacheMusica, opciones.ffmpeg, dirTrabajo, pista, m);
            !e.isEmpty()) {
            return e;
        }
        std::printf("musica: %s %s (cache %s); cama %.2f LUFS, ganancia %+.2f dB; ajuste %+.1f dB; voz %.2f LUFS; "
                    "mezcla %.2f LUFS, %.2f dBTP (PCM); margen >= %.0f dB en %.1f %% de %d ventanas con voz "
                    "(mediana %.1f dB), >= %.0f dB en 1-4 kHz en %.1f %%; cierre: musica a %.1f dB hasta el final; "
                    "sha256 musica %s\n",
                    m.desdeCache ? "del cache" : "generada", qPrintable(m.clave.left(12)), qPrintable(dirCacheMusica),
                    m.lufsCama, m.gananciaCamaDb, m.ajusteDb, m.lufsVoz, m.lufsMezcla, m.picoVerdaderoMezcla,
                    video::kMargenMinimoDb, 100.0 * m.fraccionMargen, m.ventanasConVoz, m.margenMedianoDb,
                    video::kMargenBandaDb, 100.0 * m.fraccionMargenBanda, m.duckCierreDb, qPrintable(m.sha256Musica));
        return {};
    };

    if (!dirSoloAudio.isEmpty()) {
        // Solo la pista de audio (sin sesion grafica ni grabacion): voz.s16
        // (antes de sumar) y pista.s16 (lo que recibiria ffmpeg).
        QCoreApplication app(argc, argv);
        if (!QDir().mkpath(dirSoloAudio) || !dentroDelBuild(dirSoloAudio)) {
            std::fprintf(stderr, "error: --solo-audio debe estar dentro del directorio de build (%s)\n",
                         SATCFDI_BINARY_DIR);
            return 1;
        }
        const QDir dir(QDir(dirSoloAudio).canonicalPath());
        QString pista;
        if (const QString e = prepararAudio(dir.path(), pista); !e.isEmpty()) {
            std::fprintf(stderr, "error: %s\n", qPrintable(e));
            return 1;
        }
        if (pista != dir.filePath(QStringLiteral("pista.s16"))) {
            QFile::remove(dir.filePath(QStringLiteral("pista.s16")));
            QFile::copy(pista, dir.filePath(QStringLiteral("pista.s16")));
        }
        const auto sha = [](const QString& ruta) {
            QFile f(ruta);
            QCryptographicHash h(QCryptographicHash::Sha256);
            if (f.open(QIODevice::ReadOnly)) {
                h.addData(&f);
            }
            return QString::fromLatin1(h.result().toHex());
        };
        std::printf("audio: %lld muestras por canal; voz.s16 sha256 %s; pista.s16 sha256 %s\n",
                    static_cast<long long>(qint64(salida.totalCuadros) * video::kMuestrasPorCuadro),
                    qPrintable(sha(dir.filePath(QStringLiteral("voz.s16")))),
                    qPrintable(sha(dir.filePath(QStringLiteral("pista.s16")))));
        return 0;
    }

    if (!haySesionGrafica()) {
        std::printf("SKIP: sin sesion grafica de macOS (la grabacion requiere ventana cocoa)\n");
        return kSkip;
    }
    // Solo se crea la salida si su padre ya esta dentro del build.
    QString existente = QDir::cleanPath(QDir(opciones.dirSalida).absolutePath());
    while (!QFileInfo::exists(existente) && existente != QLatin1String("/")) {
        existente = QFileInfo(existente).absolutePath();
    }
    if (!dentroDelBuild(existente) || !QDir().mkpath(opciones.dirSalida) || !dentroDelBuild(opciones.dirSalida)) {
        std::fprintf(stderr, "error: --salida debe estar dentro del directorio de build (%s)\n", SATCFDI_BINARY_DIR);
        return 1;
    }
    opciones.dirSalida = QDir(opciones.dirSalida).canonicalPath();
    // Diagnostico: los cuadros solo pueden ir a un subdirectorio de la salida.
    if (qEnvironmentVariableIntValue("SATCFDI_VIDEO_DEPURAR_CUADROS") == 1) {
        opciones.dirDepuracion = QDir(opciones.dirSalida).filePath(opciones.nombre + QStringLiteral(".cuadros"));
        const QStringList rango = qEnvironmentVariable("SATCFDI_VIDEO_DEPURAR_RANGO").split(QLatin1Char('-'));
        if (rango.size() == 2) {
            opciones.depurarDesde = rango.at(0).toInt();
            opciones.depurarHasta = rango.at(1).toInt();
        }
        if (!QDir().mkpath(opciones.dirDepuracion)) {
            std::fprintf(stderr, "error: no se pudo crear %s\n", qPrintable(opciones.dirDepuracion));
            return 1;
        }
    }

    // D4/D8: entorno fijo antes de crear la app. Cache de QML desactivada
    // para no escribir fuera del temporal y de la salida.
    qputenv("TZ", "UTC");
    ::tzset();
    qputenv("QSG_RENDER_LOOP", "basic");
    qputenv("QT_QPA_PLATFORM", "cocoa");
    qputenv("QML_DISABLE_DISK_CACHE", "1");
    // D16: texto rasterizado por CoreText (atlas en CPU) en lugar de campos de
    // distancia en la GPU, que variaban 1 nivel entre corridas.
    qputenv("QML_DISABLE_DISTANCEFIELD", "1");
    QApplication app(argc, argv);
    satcfdi::presentacion::fijarEstiloBasico(); // T013 D1, igual que la app
    // Nombre propio: nada de lo que Qt pudiera guardar coincide con la app real.
    QCoreApplication::setOrganizationName(QStringLiteral("Adenium"));
    QCoreApplication::setApplicationName(QStringLiteral("Archivo Fiscal Video Tutorial"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Archivo Fiscal")); // T019: lo visible, como la app
    QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Mexico));
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    // El parpadeo del cursor de texto usa un QTimer de tiempo real: fijo (sin
    // parpadeo) para que dos grabaciones den los mismos cuadros (D16).
    QGuiApplication::styleHints()->setCursorFlashTime(0);

    QTemporaryDir temporal;
    if (!temporal.isValid()) {
        std::fprintf(stderr, "error: no se pudo crear el directorio temporal\n");
        return 1;
    }
    opciones.dirTemporal = temporal.path();
    if (!dirVoz.isEmpty()) {
        // D13/D19: pista de voz completa, cada tramo en el cuadro de su cue,
        // normalizada como pista; R4: mas la musica si esta activa.
        if (const QString e = prepararAudio(temporal.path(), opciones.pistaVoz); !e.isEmpty()) {
            std::fprintf(stderr, "error: %s\n", qPrintable(e));
            return 1;
        }
    }

    video::DemoVideo demo(guion.datos, temporal.path());
    if (forzarPdf == QLatin1String("fallo")) {
        demo.previsualizacion.modo = video::PrevisualizacionZip::Modo::Fallo;
    } else if (forzarPdf == QLatin1String("sin-respuesta")) {
        demo.previsualizacion.modo = video::PrevisualizacionZip::Modo::SinRespuesta;
    }
    if (const QString e = demo.preparar(); !e.isEmpty()) {
        std::fprintf(stderr, "error: datos del guion: %s\n", qPrintable(e));
        return 1;
    }
    return video::grabar(guion, salida, demo, opciones);
}
