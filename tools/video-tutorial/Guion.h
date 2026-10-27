#pragma once

// Guion versionado del video tutorial (T017 D3, D13). Esquema en README.md.
// Lectura (JSON), validacion estructural y planificacion: todos los tiempos
// son cuadros enteros a 30 fps y se conocen ANTES de grabar (las barreras,
// como esperarDocumento, congelan el reloj y no consumen cuadros).

#include <QList>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace video {

inline constexpr int kFps = 30;
inline constexpr int kEsquema = 1;
// T024 D7 (antes T017 D7/D20, 7:30): duracion total del tutorial definitivo
// (tipo "tutorial"): de 5:00 a 8:00 (9000 a 14400 cuadros).
inline constexpr int kTotalTutorialMin = 5 * 60 * kFps;
inline constexpr int kTotalTutorialMax = 8 * 60 * kFps;
// T024 D7: el tutorial tiene exactamente 7 capitulos.
inline constexpr int kCapitulosTutorial = 7;
// Ventana logica de la app (raiz "ventana"; motion elige 960x540 o 960x600).
inline constexpr int kVentanaAnchoPorOmision = 960;
inline constexpr int kVentanaAltoPorOmision = 540;
// Audio de la voz (D8, D19): 48 kHz; 1600 muestras por cuadro.
inline constexpr int kMuestrasPorSegundo = 48000;
inline constexpr int kMuestrasPorCuadro = kMuestrasPorSegundo / kFps;
// Recorte de extremos de cada bloque (D19): se conservan ~65 ms antes y
// ~150 ms despues de la voz. Cuentan como silencio existente en las pausas.
inline constexpr int kCabezaMs = 65;
inline constexpr int kColaMs = 150;
// Marca de una linea interna: este adelanto antes del inicio de su voz.
inline constexpr int kAdelantoMarcaMs = 50;
// D19: duracion minima de un cue alineado.
inline constexpr int kCueMinimoMs = 1000;
// Motion design (revision-motion.md M6): `mover` sin `cuadros` dura
// clamp(round(10 + d/22), 14, 40) cuadros segun la distancia d (puntos
// logicos). El plan reserva siempre el maximo y el cursor reposa el resto.
inline constexpr int kMoverAutoMin = 14;
inline constexpr int kMoverAutoMax = 40;
// M7: pausa del cursor (quieto) antes del press de un clic.
inline constexpr int kPausaClic = 8;
// D6: limite superior del timeout de esperarDocumento.
inline constexpr int kTimeoutDocumentoMaxMs = 30000;

enum class TipoAccion {
    Esperar,            // cuadros
    Mover,              // objetivo (objectName), cuadros, desplazamiento
    Clic,               // en la posicion actual del cursor
    Escribir,           // texto, cuadrosPorCaracter
    Tecla,              // tecla: [Cmd+][Shift+][Opcion+][Ctrl+]<tecla> (p. ej. Enter, Cmd+4)
    EsperarLinea,       // linea, momento: inicio | fin
    AvanzarSolicitud,   // solicitud (clave), estado
    MostrarIlustracion, // ilustracion, parametros
    OcultarIlustracion,
    EsperarDocumento,   // timeoutMs (barrera documentoListo / documentoError)
    EsperarConteo,      // objetivo, conteo, timeoutMs (barrera: propiedad count)
    EsperarExportacion, // resultado?, timeoutMs (barrera: estado final de la exportacion, T024.2)
    EscribirContrasena, // longitud, cuadrosPorCaracter (valor sintetico, nunca en el guion)
    CambiarEFirma,      // perfil, efirma, vigenteHasta
    MostrarSolicitudes, // claves: reemplaza la lista visible
    SeleccionarArchivoEFirma, // archivo (certificado | llave), ruta sintetica
    CompletarEFirma,    // perfil, vigenteHasta: termina "Validando…" con exito
    // T024.4:
    AvanzarMes,         // perfil, mes (yyyy-MM), direccion, estado, causa: franja y lista de Facturas
    ElegirCarpeta,      // destino (exportacion | respaldo), ruta sintetica: lo que entregaria el FolderDialog
    ProgresoRespaldo,   // fase, bytes, total: progreso del respaldo en curso
    ResultadoRespaldo,  // resultado, fecha?, bytes?: termina el respaldo en curso
    EsperarRespaldo,    // resultado: barrera sobre el resultado visible del respaldo
    Desplazar,          // objetivo (Flickable), hasta | delta, cuadros: contentY por cuadro (motion 5)
    MostrarBanner,      // notificaciones: banner sobre la app viva (motion 2b)
    OcultarBanner,
};
// Motion 3: `tecla` con modificadores (o "mostrar") dura 36 cuadros con
// keycap; el evento sale en el cuadro 6.
inline constexpr int kCuadrosTeclaKeycap = 36;
inline constexpr int kCuadroEventoKeycap = 6;
// T024.4 (03-motion 4; 01-audio-voz 2): portada y cierre.
inline constexpr int kCuadrosPortada = 120;
inline constexpr int kCuadrosCierre = 150;
inline constexpr int kCierreInicioFundidoMusica = 45; // cuadro local del cierre
inline constexpr int kCierreSilencioMusica = 135;     // silencio desde aqui
// Motion 2b: el banner sale en 10 cuadros desde ocultarBanner.
inline constexpr int kCuadrosSalidaBanner = 10;

struct Accion {
    TipoAccion tipo = TipoAccion::Esperar;
    int cuadros = 0; // planificado (0 en las instantaneas y barreras)
    QString objetivo;
    QPointF desplazamiento;
    QString texto;
    QString tecla;          // Tecla: la tecla sin modificadores (Enter, 4, E...)
    QStringList modificadores; // Tecla: Cmd, Shift, Opcion, Ctrl (en ese orden)
    bool keycap = false;       // Tecla: dibujar el keycap (motion 3)
    std::optional<double> hasta; // Desplazar: contentY absoluto
    double delta = 0;            // Desplazar: relativo (sin `hasta`)
    QString linea;
    bool alFinal = false; // EsperarLinea: momento "fin"
    bool sinEfecto = false; // Clic: solo visual (p. ej. Elegir…, que abriria un dialogo nativo)
    int pausa = 0;          // Clic: cuadros quietos antes del press (planificado)
    bool cuadrosAuto = false; // Mover: duracion por distancia (M6)
    bool hover = false;     // Mover: MouseMove en cada cuadro (hover y tooltips de la app)
    QString ancla;          // Mover: auto | centro | derecha | abajo (M11)
    bool seguidoDeClic = false; // Mover: la siguiente accion es un clic
    QString archivo;        // SeleccionarArchivoEFirma
    QString ruta;           // SeleccionarArchivoEFirma
    QString verificadaEn;   // AvanzarSolicitud: ultima verificacion (resultado de Verificar ahora)
    QString solicitud;
    QString estado;
    QString ilustracion;
    QVariantMap parametros;
    QString resultadoExportacion; // EsperarExportacion: resultadosExportacion()
    int timeoutMs = 0;
    int conteo = 0;
    QString perfil;
    QString efirma;
    QString vigenteHasta; // ISO UTC; vacio = la del estado
    QStringList claves;
    QString mes;       // AvanzarMes: yyyy-MM
    QString direccion; // AvanzarMes: emitidas | recibidas | ambas
    QString causa;     // AvanzarMes con requiereAtencion: claveEstable(CausaAtencion)
    QString destino;   // ElegirCarpeta: exportacion | respaldo
    QString fase;      // ProgresoRespaldo: captura | escritura | publicacion | depuracion
    qint64 bytes = 0;  // ProgresoRespaldo / ResultadoRespaldo
    qint64 total = 0;  // ProgresoRespaldo
    QString resultado; // ResultadoRespaldo / EsperarRespaldo: exito | conAvisos | error
    QString fecha;     // ResultadoRespaldo: ISO UTC (vacio = datos.ahora)
    int inicio = 0; // cuadro relativo a la escena (planificado)
};

struct Linea {
    QString id;
    QString texto; // pantalla (no se quema) y .srt (un cue)
    QString voz;   // pronunciacion para el TTS; vacio = texto
    bool nuevoBloque = false;        // D19: abre un bloque de narracion nuevo
    std::optional<int> inicioPedido; // cuadro minimo relativo a la escena
    // Planificado:
    int inicio = 0;    // cuadro relativo a la escena (cue)
    int duracion = 0;  // cuadros (cue)
    int bloque = 0;    // indice del bloque en la escena
    qint64 inicioMuestra = 0; // inicio de su audio, muestras relativas a la escena
    qint64 origenInicio = 0;  // tramo de su audio dentro del bloque (muestras)
    qint64 origenFin = 0;

    QString textoHablado() const { return voz.isEmpty() ? texto : voz; }
};

// Bloque de narracion (D13, D19): un clip continuo con las lineas
// consecutivas de una escena; cada escena es un bloque salvo que una linea
// con "nuevoBloque" abra otro.
struct Bloque {
    QString id;        // "capitulo/escena" o "capitulo/escena/<primera linea>"
    int primera = 0;   // indice de su primera linea en la escena
    int cantidad = 0;
    QString textoHablado;        // textos hablados de sus lineas unidos con un espacio
    QStringList textosLineas;    // texto hablado de cada linea
    // Planificado:
    qint64 muestras = 0;         // duracion del audio (48 kHz)
    QList<qint64> marcas;        // inicio de cada linea dentro del audio (marcas[0] = 0)
    qint64 inicioMuestra = 0;    // relativo a la escena
};

struct Escena {
    QString id;
    int presupuestoMin = 0;
    int presupuestoMax = 0;
    int colchon = 15;
    int separacionLineas = 6; // sin efecto desde D19 (las lineas de un bloque son continuas)
    QList<Accion> acciones;
    QList<Linea> lineas;
    QList<Bloque> bloques;
    int inicio = 0;   // cuadro global (planificado)
    int duracion = 0; // planificado
};

struct Capitulo {
    QString id;
    QString titulo;
    QList<Escena> escenas;
    int inicio = 0; // cuadro global
    int fin = 0;    // exclusivo
};

// Datos sinteticos (D2) que gobiernan los servicios fijos (D6).
struct PerfilGuion {
    QString clave;
    QString rfc;
    QString nombre;
    QString efirma; // lista | porVencer | sinEFirma
    QString vigenteHasta; // ISO UTC opcional (fija el vencimiento)
    bool visible = true;  // false: aparece al crearlo desde la UI con su RFC
};

struct CfdiGuion {
    QString tipo; // I | E
    QString serie;
    QString folio;
    QString contraparte; // nombre inventado de la contraparte (emisor en recibidos)
    QString fecha;  // yyyy-MM-ddTHH:mm:ss
    QString subtotal; // "1000.00"; IVA 16 % calculado
    QString descripcion;
};

struct SolicitudGuion {
    QString clave;
    QString perfil; // clave de perfil
    QString tipo;   // emitidos | recibidos
    QString desde;  // yyyy-MM-dd
    QString hasta;
    QString estado; // ver estadosSolicitud()
    QList<CfdiGuion> cfdis; // si hay: un paquete ZIP sintetico
    bool visible = true;    // false: aparece con mostrarSolicitudes
};

// T024.4: mes del catalogo de Facturas (descarga guiada simulada). Las
// facturas son las que el SAT tiene del mes; cuales se ven depende del estado
// (avanzarMes cambia el estado como lo haria el worker real).
struct MesGuion {
    QString perfil;    // clave de perfil
    QString mes;       // yyyy-MM
    QString estado;    // estadosMes()
    QString direccion; // emitidas | recibidas | ambas (ver README)
    QString causa;     // requiereAtencion: claveEstable(CausaAtencion); vacio = la del fake
    QString descargadoEn; // ISO UTC opcional: fecha del lote en el Historial
    QList<CfdiGuion> emitidas;
    QList<CfdiGuion> recibidas;
};

// T024.4: estado inicial del respaldo (raiz datos.respaldo; ausente = carpeta,
// contrasena y respaldo diario configurados, como el fake comun).
struct RespaldoGuion {
    bool declarado = false;
    bool carpeta = true;
    bool contrasena = true;
    bool diario = true;
    QString ultimoExito; // ISO UTC; vacio = sin respaldo previo
};

struct DatosGuion {
    QString ahora; // ISO UTC, reloj fijo de la app
    QList<PerfilGuion> perfiles;
    QList<SolicitudGuion> solicitudes;
    QList<MesGuion> meses;
    RespaldoGuion respaldo;
};

// Musica de fondo generada localmente (R4, 08-audio-musica.md; raiz
// "musica", opcional: ausente = desactivada). El preset fija armonia, tempo,
// timbre, EQ y anticipacion del ducking; aqui solo niveles y tiempos.
struct ConfigMusica {
    bool activa = false;
    QString preset = QStringLiteral("pad-fiscal-v1");
    double nivelLufs = -34.0; // cama ecualizada, antes del ducking (-45..-30)
    double duckDb = -6.0;     // atenuacion adicional durante la voz (-12..-4)
    int ataqueMs = 150;       // 50..300, completado 50 ms antes de la voz
    int retencionMs = 250;    // 100..500
    int liberacionMs = 1000;  // 500..2000
    int entradaMs = 2000;     // fundido global de entrada (500..5000)
    int salidaMs = 3000;      // fundido global de salida (500..5000)
};

// Composicion del video (D8, D21, D22; raiz "composicion", opcional).
struct Composicion {
    int ancho = 2560; // lienzo en pixeles (unico valor admitido: 2560x1440)
    int alto = 1440;
    int semilla = 2008;
    QStringList paleta{QStringLiteral("#052E1C"), QStringLiteral("#0A5F38"), QStringLiteral("#15924F"),
                       QStringLiteral("#0E1F18"), QStringLiteral("#C9A227")};
    double velocidad = 0.02; // deriva del fondo
    int lineasTranscripcion = 2; // 1 o 2
};

struct Guion {
    int esquema = 0;
    QString tipo; // tutorial | ejemplo
    QString titulo;
    int totalMin = 0;
    int totalMax = 0;
    int cuadrosTitulo = 75; // titulo de capitulo en pantalla
    // D14: la herramienta agrega su propio rotulo "Ilustración". Por omision
    // no: las ilustraciones de tools/capturas-manual ya traen el suyo.
    bool rotuloHerramienta = false;
    // T024.4 (03-motion 4): portada y cierre con la marca, sin voz. Raiz
    // "portada": true / "cierre": true. Planificados (escalados con --rapido);
    // 0 = sin ellos (tambien en un guion recortado con --escenas).
    bool portada = false;
    bool cierre = false;
    int cuadrosPortada = 0;
    int cuadrosCierre = 0;
    QVariantMap voz;        // modelo, voz, instrucciones, formato, velocidad
    // D19: pausas minimas entre bloques, escenas y capitulos (ms), contando el
    // silencio existente en los extremos de los bloques.
    int pausaBloqueMs = 300;
    int pausaEscenaMs = 600;
    int pausaCapituloMs = 1000;
    // Montaje local de la voz (raiz "montaje"): tempo aplicado con ffmpeg
    // atempo (conserva el tono), 0.8-1.25. 1.0 = sin cambio. No resintetiza.
    double tempo = 1.0;
    // Pausa interna maxima (ms) del audio montado (raiz "montaje"); 0 = sin
    // compresion. Cada silencio interno mas largo se acorta a este valor.
    int pausaMaxima = 0;
    ConfigMusica musica;
    Composicion composicion;
    // T024.4: ventana logica de la app (raiz "ventana": {ancho, alto}).
    int ventanaAncho = kVentanaAnchoPorOmision;
    int ventanaAlto = kVentanaAltoPorOmision;
    // D16 (T024.4): SHA-256 del archivo del guion (lo fija leerGuion).
    QString huella;
    // D16 (T024.4): SHA-256 de las claves de bloque de la voz usada (cache de
    // montaje y alineacion), "ninguna" sin --voz. Lo fija la grabacion.
    QString huellaVoz = QStringLiteral("ninguna");
    DatosGuion datos;
    QList<Capitulo> capitulos;
    int totalCuadros = 0; // planificado
};

// Estados de solicitud y nombres de ilustracion aceptados por el guion.
const QStringList& estadosSolicitud();
const QStringList& ilustracionesDisponibles();
// T024.4: estados de mes de avanzarMes y datos.meses.
const QStringList& estadosMes();
// T024 D3: palabras prohibidas en texto y voz del tutorial. Devuelve la
// palabra encontrada (vacio si no hay), sin distinguir mayusculas ni acentos.
QString vocabularioProhibido(const QString& texto);

// Lee y valida la estructura. Mensaje de error con la ruta (escena) o vacio.
QString leerGuion(const QString& ruta, Guion& guion);

// Audio de un bloque: duracion y marca de inicio de cada linea (muestras).
struct AudioBloque {
    qint64 muestras = 0;
    QList<qint64> marcas;
};
// Sin voz: estimacion por longitud del texto hablado (15 caracteres/s) con
// marcas proporcionales. Con voz, el cache (Voz.h) entrega el audio real
// alineado; nullopt = falta -> la planificacion falla nombrando escena y bloque.
using AudioBloqueFn = std::function<std::optional<AudioBloque>(const Capitulo&, const Escena&, const Bloque&)>;
AudioBloque estimarAudioBloque(const Bloque& bloque);

struct OpcionesPlan {
    AudioBloqueFn audioBloque; // vacio = estimarAudioBloque
    // Modo rapido (ctest): escala las duraciones (minimo 1 cuadro) y no
    // valida presupuestos. 1.0 = normal.
    double escala = 1.0;
    bool validarPresupuestos = true;
};

// Calcula inicios y duraciones y valida presupuestos por escena y total
// (D13). Devuelve el error (que nombra la escena) o vacio.
QString planificar(Guion& guion, const OpcionesPlan& opciones);

QString nombreEscena(const Capitulo& c, const Escena& e);
// Copia del guion planificado con solo esas escenas ("capitulo/escena"),
// consecutivas desde el cuadro 0 (para una muestra montada).
Guion recortarGuion(const Guion& guion, const QSet<QString>& escenas);
QString idEscena(const Capitulo& c, const Escena& e); // "capitulo/escena"

// Cuantos cuadros cubren `muestras` (redondeo hacia arriba).
inline int cuadrosDeMuestras(qint64 muestras)
{
    return int((muestras + kMuestrasPorCuadro - 1) / kMuestrasPorCuadro);
}
inline qint64 muestrasDeMs(qint64 ms)
{
    return ms * kMuestrasPorSegundo / 1000;
}

} // namespace video
