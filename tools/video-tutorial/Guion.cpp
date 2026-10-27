#include "Guion.h"

#include "ResultadosExportacion.h"

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace video {

namespace {

// D2: solo RFC genericos del SAT en los datos del guion.
const QStringList kRfcPermitidos{QStringLiteral("XAXX010101000"), QStringLiteral("XEXX010101000")};

// Valores por omision de las acciones (cuadros a 30 fps).
constexpr int kCuadrosClic = 10;
constexpr int kCuadrosTecla = 6;
constexpr int kCuadrosPorCaracter = 3;
// Estimacion sin voz: ~15 caracteres hablados por segundo, minimo 1 s.
constexpr int kCaracteresPorSegundo = 15;

struct Lector {
    QString contexto; // "capitulo 'x' / escena 'y'"
    QString error;

    bool fallar(const QString& m)
    {
        if (error.isEmpty()) {
            error = contexto.isEmpty() ? m : contexto + QStringLiteral(": ") + m;
        }
        return false;
    }

    QString texto(const QJsonObject& o, const char* clave, bool obligatorio = true)
    {
        const QJsonValue v = o.value(QLatin1String(clave));
        if (v.isUndefined() || v.isNull()) {
            if (obligatorio) {
                fallar(QStringLiteral("falta \"%1\"").arg(QLatin1String(clave)));
            }
            return {};
        }
        if (!v.isString()) {
            fallar(QStringLiteral("\"%1\" debe ser texto").arg(QLatin1String(clave)));
            return {};
        }
        return v.toString();
    }

    std::optional<int> entero(const QJsonObject& o, const char* clave, int minimo, int maximo, bool obligatorio)
    {
        const QJsonValue v = o.value(QLatin1String(clave));
        if (v.isUndefined()) {
            if (obligatorio) {
                fallar(QStringLiteral("falta \"%1\"").arg(QLatin1String(clave)));
            }
            return std::nullopt;
        }
        const double d = v.toDouble(-1e18);
        if (!v.isDouble() || d != std::floor(d) || d < minimo || d > maximo) {
            fallar(QStringLiteral("\"%1\" debe ser un entero entre %2 y %3").arg(QLatin1String(clave)).arg(minimo).arg(maximo));
            return std::nullopt;
        }
        return static_cast<int>(d);
    }

    bool soloClaves(const QJsonObject& o, const QStringList& permitidas)
    {
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (!permitidas.contains(it.key())) {
                return fallar(QStringLiteral("clave desconocida \"%1\"").arg(it.key()));
            }
        }
        return true;
    }
};

std::optional<TipoAccion> tipoAccion(const QString& t)
{
    static const QHash<QString, TipoAccion> kTipos{
        {QStringLiteral("esperar"), TipoAccion::Esperar},
        {QStringLiteral("mover"), TipoAccion::Mover},
        {QStringLiteral("clic"), TipoAccion::Clic},
        {QStringLiteral("escribir"), TipoAccion::Escribir},
        {QStringLiteral("tecla"), TipoAccion::Tecla},
        {QStringLiteral("esperarLinea"), TipoAccion::EsperarLinea},
        {QStringLiteral("avanzarSolicitud"), TipoAccion::AvanzarSolicitud},
        {QStringLiteral("mostrarIlustracion"), TipoAccion::MostrarIlustracion},
        {QStringLiteral("ocultarIlustracion"), TipoAccion::OcultarIlustracion},
        {QStringLiteral("esperarDocumento"), TipoAccion::EsperarDocumento},
        {QStringLiteral("esperarConteo"), TipoAccion::EsperarConteo},
        {QStringLiteral("esperarExportacion"), TipoAccion::EsperarExportacion},
        {QStringLiteral("escribirContrasena"), TipoAccion::EscribirContrasena},
        {QStringLiteral("cambiarEFirma"), TipoAccion::CambiarEFirma},
        {QStringLiteral("mostrarSolicitudes"), TipoAccion::MostrarSolicitudes},
        {QStringLiteral("seleccionarArchivoEFirma"), TipoAccion::SeleccionarArchivoEFirma},
        {QStringLiteral("completarEFirma"), TipoAccion::CompletarEFirma},
        {QStringLiteral("avanzarMes"), TipoAccion::AvanzarMes},
        {QStringLiteral("elegirCarpeta"), TipoAccion::ElegirCarpeta},
        {QStringLiteral("progresoRespaldo"), TipoAccion::ProgresoRespaldo},
        {QStringLiteral("resultadoRespaldo"), TipoAccion::ResultadoRespaldo},
        {QStringLiteral("esperarRespaldo"), TipoAccion::EsperarRespaldo},
        {QStringLiteral("desplazar"), TipoAccion::Desplazar},
        {QStringLiteral("mostrarBanner"), TipoAccion::MostrarBanner},
        {QStringLiteral("ocultarBanner"), TipoAccion::OcultarBanner},
    };
    const auto it = kTipos.constFind(t);
    if (it == kTipos.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

const QStringList& teclas()
{
    static const QStringList k{QStringLiteral("Enter"),     QStringLiteral("Escape"),  QStringLiteral("Tab"),
                               QStringLiteral("Abajo"),     QStringLiteral("Arriba"),  QStringLiteral("Izquierda"),
                               QStringLiteral("Derecha"),   QStringLiteral("Espacio"), QStringLiteral("Retroceso"),
                               QStringLiteral("Coma"),      QStringLiteral("Punto")};
    return k;
}

// T024.4: modificadores de `tecla`, en el orden en que deben escribirse.
// Cmd = Qt::ControlModifier en macOS (Qt intercambia Ctrl y Cmd); Ctrl = la
// tecla Control fisica (Qt::MetaModifier).
const QStringList& modificadoresTecla()
{
    static const QStringList k{QStringLiteral("Cmd"), QStringLiteral("Shift"), QStringLiteral("Opcion"),
                               QStringLiteral("Ctrl")};
    return k;
}

// "Cmd+Shift+E" -> tecla "E" y modificadores {Cmd, Shift}. Tecla: un nombre de
// teclas(), una letra A-Z o un digito. Error o vacio.
QString leerTecla(const QString& combinacion, QString& tecla, QStringList& modificadores)
{
    const QStringList partes = combinacion.split(QLatin1Char('+'));
    tecla = partes.constLast();
    int ultimo = -1;
    for (qsizetype i = 0; i + 1 < partes.size(); ++i) {
        const int k = int(modificadoresTecla().indexOf(partes.at(i)));
        if (k < 0) {
            return QStringLiteral("modificador desconocido \"%1\" en \"%2\" (usa %3)")
                .arg(partes.at(i), combinacion, modificadoresTecla().join(QStringLiteral(", ")));
        }
        if (k <= ultimo) {
            return QStringLiteral("modificadores repetidos o fuera de orden en \"%1\" (orden: %2)")
                .arg(combinacion, modificadoresTecla().join(QLatin1Char('+')));
        }
        ultimo = k;
        modificadores.append(partes.at(i));
    }
    static const QRegularExpression kSimple(QStringLiteral("^[A-Z0-9]$"));
    if (!teclas().contains(tecla) && !kSimple.match(tecla).hasMatch()) {
        return QStringLiteral("tecla desconocida \"%1\" (usa %2, una letra A-Z o un digito, con modificadores %3)")
            .arg(tecla, teclas().join(QStringLiteral(", ")), modificadoresTecla().join(QLatin1Char('+')));
    }
    return {};
}

// D2: rutas sinteticas.
bool rutaSintetica(const QString& ruta)
{
    return ruta.startsWith(QLatin1String("/Users/usuario/")) && !ruta.contains(QLatin1String("/../"));
}

bool esMes(const QString& s)
{
    static const QRegularExpression kMes(QStringLiteral("^\\d{4}-(0[1-9]|1[0-2])$"));
    return kMes.match(s).hasMatch();
}

// Nombres de CausaAtencion (claveEstable); la prueba de DatosDemo comprueba
// que coinciden con el dominio.
const QStringList& causasAtencion()
{
    static const QStringList k{
        QStringLiteral("Codigo5002"),     QStringLiteral("Codigo5005"),      QStringLiteral("EnvioIncierto"),
        QStringLiteral("EnvioFallido"),   QStringLiteral("ErrorSat"),        QStringLiteral("Rechazada"),
        QStringLiteral("Vencida"),        QStringLiteral("PaqueteError"),    QStringLiteral("PaqueteVencido"),
        QStringLiteral("ZipNoDisponible"), QStringLiteral("ZipIlegible"),    QStringLiteral("IndiceIlegible"),
        QStringLiteral("IndexacionSinProgramar")};
    return k;
}

// Direccion de un estado de mes: valida la combinacion (README, avanzarMes) y
// deja la direccion por omision. Error o vacio.
QString validarDireccionMes(const QString& estado, QString& direccion)
{
    const QStringList validas{QStringLiteral("emitidas"), QStringLiteral("recibidas"), QStringLiteral("ambas")};
    if (!direccion.isEmpty() && !validas.contains(direccion)) {
        return QStringLiteral("\"direccion\" debe ser emitidas, recibidas o ambas");
    }
    if (estado == QLatin1String("disponiblesParcialmente")) {
        direccion = direccion.isEmpty() ? QStringLiteral("emitidas") : direccion;
    } else if (estado == QLatin1String("requiereAtencion")) {
        direccion = direccion.isEmpty() ? QStringLiteral("recibidas") : direccion;
    } else {
        direccion = direccion.isEmpty() ? QStringLiteral("ambas") : direccion;
    }
    if ((estado == QLatin1String("disponiblesParcialmente") || estado == QLatin1String("requiereAtencion"))
        && direccion == QLatin1String("ambas")) {
        return QStringLiteral("con %1, \"direccion\" es emitidas o recibidas (la direccion %2)")
            .arg(estado, estado == QLatin1String("requiereAtencion") ? QStringLiteral("con la causa")
                                                                       : QStringLiteral("disponible"));
    }
    if ((estado == QLatin1String("disponibles") || estado == QLatin1String("sinDescargar"))
        && direccion != QLatin1String("ambas")) {
        return QStringLiteral("con %1, \"direccion\" solo puede ser ambas (usa disponiblesParcialmente o preparando)")
            .arg(estado);
    }
    return {};
}

// avanzarMes: el estado se aplica a la direccion (ambas por omision);
// disponiblesParcialmente y requiereAtencion piden una sola direccion.
QString validarDireccionAvance(const QString& estado, QString& direccion)
{
    const QStringList validas{QStringLiteral("emitidas"), QStringLiteral("recibidas"), QStringLiteral("ambas")};
    if (!direccion.isEmpty() && !validas.contains(direccion)) {
        return QStringLiteral("\"direccion\" debe ser emitidas, recibidas o ambas");
    }
    if (estado == QLatin1String("disponiblesParcialmente") || estado == QLatin1String("requiereAtencion")) {
        return validarDireccionMes(estado, direccion);
    }
    direccion = direccion.isEmpty() ? QStringLiteral("ambas") : direccion;
    return {};
}

const QStringList& resultadosRespaldo()
{
    static const QStringList k{QStringLiteral("exito"), QStringLiteral("conAvisos"), QStringLiteral("error")};
    return k;
}

bool leerAccion(Lector& l, const QJsonObject& o, Accion& a)
{
    const QString tipo = l.texto(o, "tipo");
    const auto t = tipoAccion(tipo);
    if (!t) {
        return l.fallar(QStringLiteral("accion de tipo desconocido \"%1\"").arg(tipo));
    }
    a.tipo = *t;
    switch (a.tipo) {
    case TipoAccion::Esperar:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("cuadros")})) {
            return false;
        }
        a.cuadros = l.entero(o, "cuadros", 1, 100000, true).value_or(0);
        break;
    case TipoAccion::Mover: {
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("objetivo"), QStringLiteral("cuadros"),
                              QStringLiteral("desplazamiento"), QStringLiteral("hover"), QStringLiteral("ancla")})) {
            return false;
        }
        a.objetivo = l.texto(o, "objetivo");
        const std::optional<int> cuadros = l.entero(o, "cuadros", 1, 600, false);
        a.cuadrosAuto = !cuadros.has_value();
        a.cuadros = cuadros.value_or(kMoverAutoMax);
        a.hover = o.value(QLatin1String("hover")).toBool(false);
        a.ancla = l.texto(o, "ancla", false);
        if (a.ancla.isEmpty()) {
            a.ancla = QStringLiteral("auto");
        }
        if (!QStringList{QStringLiteral("auto"), QStringLiteral("centro"), QStringLiteral("derecha"), QStringLiteral("abajo")}
                 .contains(a.ancla)) {
            return l.fallar(QStringLiteral("\"ancla\" debe ser auto, centro, derecha o abajo"));
        }
        const QJsonValue d = o.value(QLatin1String("desplazamiento"));
        if (!d.isUndefined()) {
            const QJsonArray arr = d.toArray();
            if (!d.isArray() || arr.size() != 2 || !arr.at(0).isDouble() || !arr.at(1).isDouble()) {
                return l.fallar(QStringLiteral("\"desplazamiento\" debe ser [dx, dy]"));
            }
            a.desplazamiento = QPointF(arr.at(0).toDouble(), arr.at(1).toDouble());
        }
        break;
    }
    case TipoAccion::Clic:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("cuadros"), QStringLiteral("sinEfecto"),
                              QStringLiteral("pausa")})) {
            return false;
        }
        // `cuadros`: desde el press; `pausa`: antes del press (cursor quieto, M7).
        a.cuadros = l.entero(o, "cuadros", 4, 120, false).value_or(kCuadrosClic);
        a.pausa = l.entero(o, "pausa", 0, 60, false).value_or(kPausaClic);
        a.sinEfecto = o.value(QLatin1String("sinEfecto")).toBool(false);
        break;
    case TipoAccion::Escribir: {
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("texto"), QStringLiteral("cuadrosPorCaracter")})) {
            return false;
        }
        a.texto = l.texto(o, "texto");
        const int porCaracter = l.entero(o, "cuadrosPorCaracter", 1, 30, false).value_or(kCuadrosPorCaracter);
        if (a.texto.isEmpty()) {
            return l.fallar(QStringLiteral("\"escribir\" sin texto"));
        }
        a.cuadros = porCaracter * int(a.texto.size());
        break;
    }
    case TipoAccion::Tecla:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("tecla"), QStringLiteral("cuadros"),
                              QStringLiteral("mostrar")})) {
            return false;
        }
        a.tecla = l.texto(o, "tecla");
        if (!a.tecla.isEmpty()) {
            QString tecla;
            if (const QString e = leerTecla(a.tecla, tecla, a.modificadores); !e.isEmpty()) {
                return l.fallar(e);
            }
            a.tecla = tecla;
        }
        // Motion 3: keycap con modificadores (o "mostrar": true); sin el,
        // 6 cuadros y el evento en el primero, como antes.
        a.keycap = o.value(QLatin1String("mostrar")).toBool(!a.modificadores.isEmpty());
        a.cuadros = l.entero(o, "cuadros", a.keycap ? kCuadroEventoKeycap + 1 : 1, 120, false)
                        .value_or(a.keycap ? kCuadrosTeclaKeycap : kCuadrosTecla);
        break;
    case TipoAccion::EsperarLinea: {
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("linea"), QStringLiteral("momento")})) {
            return false;
        }
        a.linea = l.texto(o, "linea");
        const QString momento = l.texto(o, "momento", false);
        if (!momento.isEmpty() && momento != QLatin1String("inicio") && momento != QLatin1String("fin")) {
            return l.fallar(QStringLiteral("\"momento\" debe ser inicio o fin"));
        }
        a.alFinal = momento == QLatin1String("fin");
        break;
    }
    case TipoAccion::AvanzarSolicitud:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("solicitud"), QStringLiteral("estado"),
                              QStringLiteral("verificadaEn")})) {
            return false;
        }
        a.solicitud = l.texto(o, "solicitud");
        a.estado = l.texto(o, "estado");
        a.verificadaEn = l.texto(o, "verificadaEn", false);
        if (!a.verificadaEn.isEmpty() && !QDateTime::fromString(a.verificadaEn, Qt::ISODate).isValid()) {
            return l.fallar(QStringLiteral("\"verificadaEn\" debe ser ISO 8601"));
        }
        if (!a.estado.isEmpty() && !estadosSolicitud().contains(a.estado)) {
            return l.fallar(QStringLiteral("estado de solicitud desconocido \"%1\"").arg(a.estado));
        }
        break;
    case TipoAccion::MostrarIlustracion:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("ilustracion"), QStringLiteral("parametros")})) {
            return false;
        }
        a.ilustracion = l.texto(o, "ilustracion");
        if (!a.ilustracion.isEmpty() && !ilustracionesDisponibles().contains(a.ilustracion)) {
            return l.fallar(QStringLiteral("ilustracion desconocida \"%1\" (usa %2)")
                                .arg(a.ilustracion, ilustracionesDisponibles().join(QStringLiteral(", "))));
        }
        a.parametros = o.value(QLatin1String("parametros")).toObject().toVariantMap();
        break;
    case TipoAccion::OcultarIlustracion:
        if (!l.soloClaves(o, {QStringLiteral("tipo")})) {
            return false;
        }
        break;
    case TipoAccion::EsperarDocumento:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("timeoutMs")})) {
            return false;
        }
        a.timeoutMs = l.entero(o, "timeoutMs", 1, kTimeoutDocumentoMaxMs, true).value_or(0);
        break;
    case TipoAccion::EsperarExportacion:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("resultado"), QStringLiteral("timeoutMs")})) {
            return false;
        }
        a.resultadoExportacion = l.texto(o, "resultado", false);
        if (a.resultadoExportacion.isEmpty()) {
            a.resultadoExportacion = video::resultadoExportacionPorOmision();
        } else if (!video::resultadosExportacion().contains(a.resultadoExportacion)) {
            return l.fallar(QStringLiteral("resultado de exportacion desconocido \"%1\" (usa %2)")
                                .arg(a.resultadoExportacion, video::resultadosExportacion().join(QStringLiteral(", "))));
        }
        a.timeoutMs = l.entero(o, "timeoutMs", 1, kTimeoutDocumentoMaxMs, true).value_or(0);
        break;
    case TipoAccion::EsperarConteo:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("objetivo"), QStringLiteral("conteo"),
                              QStringLiteral("timeoutMs")})) {
            return false;
        }
        a.objetivo = l.texto(o, "objetivo");
        a.conteo = l.entero(o, "conteo", 0, 100000, true).value_or(0);
        a.timeoutMs = l.entero(o, "timeoutMs", 1, kTimeoutDocumentoMaxMs, true).value_or(0);
        break;
    case TipoAccion::EscribirContrasena: {
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("longitud"), QStringLiteral("cuadrosPorCaracter")})) {
            return false;
        }
        const int longitud = l.entero(o, "longitud", 1, 64, true).value_or(1);
        const int porCaracter = l.entero(o, "cuadrosPorCaracter", 1, 30, false).value_or(kCuadrosPorCaracter);
        // Valor sintetico fijo: nunca se escribe en el guion ni en logs.
        a.texto = QStringLiteral("demo").repeated(16).left(longitud);
        a.cuadros = porCaracter * longitud;
        break;
    }
    case TipoAccion::CambiarEFirma:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("perfil"), QStringLiteral("efirma"),
                              QStringLiteral("vigenteHasta")})) {
            return false;
        }
        a.perfil = l.texto(o, "perfil");
        a.efirma = l.texto(o, "efirma");
        a.vigenteHasta = l.texto(o, "vigenteHasta", false);
        if (!QStringList{QStringLiteral("lista"), QStringLiteral("porVencer"), QStringLiteral("sinEFirma")}.contains(a.efirma)) {
            return l.fallar(QStringLiteral("efirma debe ser lista, porVencer o sinEFirma"));
        }
        if (!a.vigenteHasta.isEmpty() && !QDateTime::fromString(a.vigenteHasta, Qt::ISODate).isValid()) {
            return l.fallar(QStringLiteral("\"vigenteHasta\" debe ser ISO 8601"));
        }
        break;
    case TipoAccion::SeleccionarArchivoEFirma: {
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("archivo"), QStringLiteral("ruta")})) {
            return false;
        }
        a.archivo = l.texto(o, "archivo");
        a.ruta = l.texto(o, "ruta");
        if (a.archivo != QLatin1String("certificado") && a.archivo != QLatin1String("llave")) {
            return l.fallar(QStringLiteral("\"archivo\" debe ser certificado o llave"));
        }
        // D2: solo rutas sinteticas; el archivo nunca se lee.
        if (!a.ruta.startsWith(QLatin1String("/Users/usuario/"))) {
            return l.fallar(QStringLiteral("\"ruta\" debe estar bajo /Users/usuario/ (sintetica)"));
        }
        break;
    }
    case TipoAccion::CompletarEFirma:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("perfil"), QStringLiteral("vigenteHasta")})) {
            return false;
        }
        a.perfil = l.texto(o, "perfil");
        a.vigenteHasta = l.texto(o, "vigenteHasta", false);
        if (!a.vigenteHasta.isEmpty() && !QDateTime::fromString(a.vigenteHasta, Qt::ISODate).isValid()) {
            return l.fallar(QStringLiteral("\"vigenteHasta\" debe ser ISO 8601"));
        }
        break;
    case TipoAccion::MostrarSolicitudes:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("claves")})) {
            return false;
        }
        for (const QJsonValue& v : o.value(QLatin1String("claves")).toArray()) {
            a.claves.append(v.toString());
        }
        break;
    case TipoAccion::AvanzarMes: {
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("perfil"), QStringLiteral("mes"),
                              QStringLiteral("direccion"), QStringLiteral("estado"), QStringLiteral("causa")})) {
            return false;
        }
        a.perfil = l.texto(o, "perfil");
        a.mes = l.texto(o, "mes");
        a.direccion = l.texto(o, "direccion", false);
        a.estado = l.texto(o, "estado");
        a.causa = l.texto(o, "causa", false);
        if (!l.error.isEmpty()) {
            return false;
        }
        if (!esMes(a.mes)) {
            return l.fallar(QStringLiteral("\"mes\" debe ser yyyy-MM"));
        }
        if (!estadosMes().contains(a.estado)) {
            return l.fallar(QStringLiteral("estado de mes desconocido \"%1\" (usa %2)")
                                .arg(a.estado, estadosMes().join(QStringLiteral(", "))));
        }
        if (const QString e = validarDireccionAvance(a.estado, a.direccion); !e.isEmpty()) {
            return l.fallar(QStringLiteral("avanzarMes: ") + e);
        }
        if (!a.causa.isEmpty() && (a.estado != QLatin1String("requiereAtencion") || !causasAtencion().contains(a.causa))) {
            return l.fallar(QStringLiteral("\"causa\" solo con requiereAtencion y una de %1")
                                .arg(causasAtencion().join(QStringLiteral(", "))));
        }
        break;
    }
    case TipoAccion::ElegirCarpeta:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("destino"), QStringLiteral("ruta")})) {
            return false;
        }
        a.destino = l.texto(o, "destino");
        a.ruta = l.texto(o, "ruta");
        if (a.destino != QLatin1String("exportacion") && a.destino != QLatin1String("respaldo")) {
            return l.fallar(QStringLiteral("\"destino\" debe ser exportacion o respaldo"));
        }
        if (!rutaSintetica(a.ruta)) {
            return l.fallar(QStringLiteral("\"ruta\" debe estar bajo /Users/usuario/ (sintetica)"));
        }
        break;
    case TipoAccion::ProgresoRespaldo: {
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("fase"), QStringLiteral("bytes"),
                              QStringLiteral("total")})) {
            return false;
        }
        a.fase = l.texto(o, "fase");
        a.bytes = l.entero(o, "bytes", 0, 2000000000, true).value_or(0);
        a.total = l.entero(o, "total", 0, 2000000000, false).value_or(0);
        if (!QStringList{QStringLiteral("captura"), QStringLiteral("escritura"), QStringLiteral("publicacion"),
                         QStringLiteral("depuracion")}
                 .contains(a.fase)) {
            return l.fallar(QStringLiteral("\"fase\" debe ser captura, escritura, publicacion o depuracion"));
        }
        if (a.total > 0 && a.bytes > a.total) {
            return l.fallar(QStringLiteral("\"bytes\" no puede pasar de \"total\""));
        }
        break;
    }
    case TipoAccion::ResultadoRespaldo:
    case TipoAccion::EsperarRespaldo: {
        const bool terminar = a.tipo == TipoAccion::ResultadoRespaldo;
        QStringList claves{QStringLiteral("tipo"), QStringLiteral("resultado")};
        if (terminar) {
            claves.append({QStringLiteral("bytes"), QStringLiteral("fecha")});
        } else {
            claves.append(QStringLiteral("timeoutMs"));
        }
        if (!l.soloClaves(o, claves)) {
            return false;
        }
        if (!terminar) {
            a.timeoutMs = l.entero(o, "timeoutMs", 1, kTimeoutDocumentoMaxMs, false).value_or(5000);
        }
        a.resultado = l.texto(o, "resultado", false);
        if (a.resultado.isEmpty()) {
            a.resultado = QStringLiteral("exito");
        }
        if (!resultadosRespaldo().contains(a.resultado)) {
            return l.fallar(QStringLiteral("resultado de respaldo desconocido \"%1\" (usa %2)")
                                .arg(a.resultado, resultadosRespaldo().join(QStringLiteral(", "))));
        }
        if (terminar) {
            a.bytes = l.entero(o, "bytes", 0, 2000000000, false).value_or(12'400'000);
            a.fecha = l.texto(o, "fecha", false);
            if (!a.fecha.isEmpty() && !QDateTime::fromString(a.fecha, Qt::ISODate).isValid()) {
                return l.fallar(QStringLiteral("\"fecha\" debe ser ISO 8601"));
            }
        }
        break;
    }
    case TipoAccion::Desplazar: {
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("objetivo"), QStringLiteral("hasta"),
                              QStringLiteral("delta"), QStringLiteral("cuadros")})) {
            return false;
        }
        a.objetivo = l.texto(o, "objetivo");
        const QJsonValue hasta = o.value(QLatin1String("hasta"));
        const QJsonValue delta = o.value(QLatin1String("delta"));
        if (hasta.isUndefined() == delta.isUndefined() || (!hasta.isUndefined() && !hasta.isDouble())
            || (!delta.isUndefined() && !delta.isDouble())) {
            return l.fallar(QStringLiteral("desplazar: usa \"hasta\" (contentY) o \"delta\", un numero"));
        }
        if (hasta.isDouble()) {
            a.hasta = hasta.toDouble();
        }
        a.delta = delta.toDouble(0);
        // Motion 5: 18 cuadros hasta 200 puntos, 24 si es mas (con `hasta`
        // la distancia se conoce al grabar: se reservan 24 y se usa la que toque).
        a.cuadrosAuto = !o.contains(QLatin1String("cuadros"));
        a.cuadros = l.entero(o, "cuadros", 1, 300, false).value_or(a.hasta || std::abs(a.delta) > 200 ? 24 : 18);
        break;
    }
    case TipoAccion::MostrarBanner:
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("notificaciones")})) {
            return false;
        }
        a.parametros = QVariantMap{{QStringLiteral("notificaciones"),
                                    o.value(QLatin1String("notificaciones")).toArray().toVariantList()}};
        break;
    case TipoAccion::OcultarBanner:
        if (!l.soloClaves(o, {QStringLiteral("tipo")})) {
            return false;
        }
        break;
    }
    return l.error.isEmpty();
}

bool esFecha(const QString& s) { return QDate::fromString(s, Qt::ISODate).isValid(); }

// Contrapartes inventadas para las facturas generadas por conteo.
const QStringList& contrapartesSinteticas()
{
    static const QStringList k{QStringLiteral("Papelería Ejemplo"),        QStringLiteral("Servicios Demo"),
                               QStringLiteral("Transportes Ficticios"),    QStringLiteral("Comercializadora Imaginaria"),
                               QStringLiteral("Arrendamientos Demo"),      QStringLiteral("Distribuidora Imaginaria"),
                               QStringLiteral("Refacciones Ficticias"),    QStringLiteral("Consultoría Muestra")};
    return k;
}

// CFDI de un mes: lista explicita (como en solicitudes) o un conteo; con
// conteo se generan de forma determinista por (mes, direccion, i), con fecha
// dentro del mes y nunca despues de `ahora`.
bool leerCfdis(Lector& l, const QJsonValue& v, const QString& ctx, const QString& mes, bool emitidas,
               const QDateTime& ahora, QList<CfdiGuion>& salida)
{
    static const QRegularExpression kImporte(QStringLiteral("^\\d{1,9}\\.\\d{2}$"));
    if (v.isUndefined()) {
        return true;
    }
    if (v.isDouble()) {
        const double n = v.toDouble();
        if (n != std::floor(n) || n < 0 || n > 500) {
            return l.fallar(ctx + QStringLiteral("el conteo de facturas debe ser un entero de 0 a 500"));
        }
        const QDate inicio = QDate::fromString(mes + QStringLiteral("-01"), Qt::ISODate);
        int ultimoDia = inicio.daysInMonth();
        const QDate hoy = ahora.toUTC().date();
        if (hoy.year() == inicio.year() && hoy.month() == inicio.month()) {
            ultimoDia = std::max(1, hoy.day() - 1);
        }
        const int mesN = inicio.month();
        for (int i = 0; i < int(n); ++i) {
            CfdiGuion c;
            c.tipo = i % 6 == 5 ? QStringLiteral("E") : QStringLiteral("I");
            c.serie = c.tipo == QLatin1String("E") ? QStringLiteral("NC") : (emitidas ? QStringLiteral("A") : QStringLiteral("F"));
            c.folio = QString::number((emitidas ? 1100 : 5300) + 37 * i + mesN);
            c.contraparte = contrapartesSinteticas().at((i + mesN + (emitidas ? 0 : 3)) % contrapartesSinteticas().size());
            const int dia = 1 + (i * 5 + mesN) % ultimoDia;
            c.fecha = QStringLiteral("%1-%2T%3:%4:%5")
                          .arg(mes)
                          .arg(dia, 2, 10, QLatin1Char('0'))
                          .arg(9 + (i * 3) % 9, 2, 10, QLatin1Char('0'))
                          .arg((i * 17 + 5) % 60, 2, 10, QLatin1Char('0'))
                          .arg((i * 31 + 7) % 60, 2, 10, QLatin1Char('0'));
            c.subtotal = QStringLiteral("%1.%2")
                             .arg(450 + (731 * i + 97 * mesN + (emitidas ? 0 : 211)) % 9000)
                             .arg((13 * i + 7) % 100, 2, 10, QLatin1Char('0'));
            c.descripcion = c.tipo == QLatin1String("E") ? QStringLiteral("Devolución de ejemplo")
                                                         : QStringLiteral("Servicio de ejemplo");
            salida.append(c);
        }
        return true;
    }
    if (!v.isArray()) {
        return l.fallar(ctx + QStringLiteral("emitidas/recibidas: un conteo o una lista de CFDI"));
    }
    for (const QJsonValue& c : v.toArray()) {
        const QJsonObject x = c.toObject();
        CfdiGuion cfdi{l.texto(x, "tipo"), l.texto(x, "serie", false), l.texto(x, "folio"), l.texto(x, "contraparte"),
                       l.texto(x, "fecha"), l.texto(x, "subtotal"), l.texto(x, "descripcion")};
        if (!l.error.isEmpty()) {
            return false;
        }
        if (cfdi.tipo != QLatin1String("I") && cfdi.tipo != QLatin1String("E")) {
            return l.fallar(ctx + QStringLiteral("CFDI %1: tipo debe ser I o E").arg(cfdi.folio));
        }
        if (!QDateTime::fromString(cfdi.fecha, Qt::ISODate).isValid() || !kImporte.match(cfdi.subtotal).hasMatch()
            || !cfdi.fecha.startsWith(mes)) {
            return l.fallar(ctx + QStringLiteral("CFDI %1: fecha (del mes %2) o subtotal invalidos").arg(cfdi.folio, mes));
        }
        salida.append(cfdi);
    }
    return true;
}

bool leerMeses(Lector& l, const QJsonValue& v, const QSet<QString>& perfiles, const QDateTime& ahora, DatosGuion& d)
{
    if (v.isUndefined()) {
        return true;
    }
    QSet<QString> vistos;
    for (const QJsonValue& vm : v.toArray()) {
        const QJsonObject o = vm.toObject();
        MesGuion m;
        m.perfil = l.texto(o, "perfil");
        m.mes = l.texto(o, "mes");
        l.contexto = QStringLiteral("datos.meses '%1/%2'").arg(m.perfil, m.mes);
        if (!l.soloClaves(o, {QStringLiteral("perfil"), QStringLiteral("mes"), QStringLiteral("estado"),
                              QStringLiteral("direccion"), QStringLiteral("causa"), QStringLiteral("descargadoEn"),
                              QStringLiteral("emitidas"), QStringLiteral("recibidas")})) {
            return false;
        }
        m.estado = l.texto(o, "estado");
        m.direccion = l.texto(o, "direccion", false);
        m.causa = l.texto(o, "causa", false);
        m.descargadoEn = l.texto(o, "descargadoEn", false);
        if (!l.error.isEmpty()) {
            return false;
        }
        if (!perfiles.contains(m.perfil)) {
            return l.fallar(QStringLiteral("perfil desconocido"));
        }
        if (!esMes(m.mes)) {
            return l.fallar(QStringLiteral("\"mes\" debe ser yyyy-MM"));
        }
        if (QDate::fromString(m.mes + QStringLiteral("-01"), Qt::ISODate) > ahora.toUTC().date()) {
            return l.fallar(QStringLiteral("mes futuro respecto de \"ahora\""));
        }
        if (vistos.contains(m.perfil + QLatin1Char('/') + m.mes)) {
            return l.fallar(QStringLiteral("repetido"));
        }
        vistos.insert(m.perfil + QLatin1Char('/') + m.mes);
        if (!estadosMes().contains(m.estado)) {
            return l.fallar(QStringLiteral("estado de mes desconocido \"%1\" (usa %2)")
                                .arg(m.estado, estadosMes().join(QStringLiteral(", "))));
        }
        if (const QString e = validarDireccionMes(m.estado, m.direccion); !e.isEmpty()) {
            return l.fallar(e);
        }
        if (!m.causa.isEmpty() && !causasAtencion().contains(m.causa)) {
            return l.fallar(QStringLiteral("\"causa\" debe ser una de %1").arg(causasAtencion().join(QStringLiteral(", "))));
        }
        if (!m.descargadoEn.isEmpty() && !QDateTime::fromString(m.descargadoEn, Qt::ISODate).isValid()) {
            return l.fallar(QStringLiteral("\"descargadoEn\" debe ser ISO 8601"));
        }
        if (!leerCfdis(l, o.value(QLatin1String("emitidas")), QString(), m.mes, true, ahora, m.emitidas)
            || !leerCfdis(l, o.value(QLatin1String("recibidas")), QString(), m.mes, false, ahora, m.recibidas)) {
            return false;
        }
        d.meses.append(m);
    }
    l.contexto.clear();
    return true;
}

bool leerDatos(Lector& l, const QJsonObject& o, DatosGuion& d)
{
    l.contexto = QStringLiteral("datos");
    if (!l.soloClaves(o, {QStringLiteral("ahora"), QStringLiteral("perfiles"), QStringLiteral("solicitudes"),
                          QStringLiteral("meses"), QStringLiteral("respaldo")})) {
        return false;
    }
    d.ahora = l.texto(o, "ahora");
    if (!QDateTime::fromString(d.ahora, Qt::ISODate).isValid()) {
        return l.fallar(QStringLiteral("\"ahora\" debe ser una fecha ISO 8601 en UTC"));
    }
    QSet<QString> clavesPerfil;
    for (const QJsonValue& v : o.value(QLatin1String("perfiles")).toArray()) {
        const QJsonObject p = v.toObject();
        PerfilGuion perfil{l.texto(p, "clave"), l.texto(p, "rfc"), l.texto(p, "nombre"), l.texto(p, "efirma"),
                           l.texto(p, "vigenteHasta", false), p.value(QLatin1String("visible")).toBool(true)};
        if (!l.error.isEmpty()) {
            return false;
        }
        if (!kRfcPermitidos.contains(perfil.rfc)) {
            return l.fallar(QStringLiteral("perfil '%1': RFC no sintetico (usa %2)").arg(perfil.clave, kRfcPermitidos.join(QStringLiteral(" o "))));
        }
        if (!QStringList{QStringLiteral("lista"), QStringLiteral("porVencer"), QStringLiteral("sinEFirma")}.contains(perfil.efirma)) {
            return l.fallar(QStringLiteral("perfil '%1': efirma debe ser lista, porVencer o sinEFirma").arg(perfil.clave));
        }
        if (clavesPerfil.contains(perfil.clave)) {
            return l.fallar(QStringLiteral("perfil '%1' repetido").arg(perfil.clave));
        }
        clavesPerfil.insert(perfil.clave);
        d.perfiles.append(perfil);
    }
    if (d.perfiles.isEmpty()) {
        return l.fallar(QStringLiteral("se necesita al menos un perfil"));
    }
    QSet<QString> clavesSolicitud;
    for (const QJsonValue& v : o.value(QLatin1String("solicitudes")).toArray()) {
        const QJsonObject s = v.toObject();
        SolicitudGuion sol{l.texto(s, "clave"), l.texto(s, "perfil"), l.texto(s, "tipo"), l.texto(s, "desde"),
                           l.texto(s, "hasta"), l.texto(s, "estado"), {}};
        if (!l.error.isEmpty()) {
            return false;
        }
        const QString ctx = QStringLiteral("solicitud '%1': ").arg(sol.clave);
        if (!clavesPerfil.contains(sol.perfil)) {
            return l.fallar(ctx + QStringLiteral("perfil desconocido \"%1\"").arg(sol.perfil));
        }
        if (sol.tipo != QLatin1String("emitidos") && sol.tipo != QLatin1String("recibidos")) {
            return l.fallar(ctx + QStringLiteral("tipo debe ser emitidos o recibidos"));
        }
        if (!esFecha(sol.desde) || !esFecha(sol.hasta)) {
            return l.fallar(ctx + QStringLiteral("desde/hasta deben ser yyyy-MM-dd"));
        }
        if (!estadosSolicitud().contains(sol.estado)) {
            return l.fallar(ctx + QStringLiteral("estado desconocido \"%1\"").arg(sol.estado));
        }
        if (clavesSolicitud.contains(sol.clave)) {
            return l.fallar(ctx + QStringLiteral("repetida"));
        }
        clavesSolicitud.insert(sol.clave);
        sol.visible = s.value(QLatin1String("visible")).toBool(true);
        static const QRegularExpression kImporte(QStringLiteral("^\\d{1,9}\\.\\d{2}$"));
        for (const QJsonValue& c : s.value(QLatin1String("cfdis")).toArray()) {
            const QJsonObject x = c.toObject();
            CfdiGuion cfdi{l.texto(x, "tipo"), l.texto(x, "serie", false), l.texto(x, "folio"), l.texto(x, "contraparte"),
                           l.texto(x, "fecha"), l.texto(x, "subtotal"), l.texto(x, "descripcion")};
            if (!l.error.isEmpty()) {
                return false;
            }
            if (cfdi.tipo != QLatin1String("I") && cfdi.tipo != QLatin1String("E")) {
                return l.fallar(ctx + QStringLiteral("CFDI %1: tipo debe ser I o E").arg(cfdi.folio));
            }
            if (!QDateTime::fromString(cfdi.fecha, Qt::ISODate).isValid() || !kImporte.match(cfdi.subtotal).hasMatch()) {
                return l.fallar(ctx + QStringLiteral("CFDI %1: fecha o subtotal invalidos").arg(cfdi.folio));
            }
            sol.cfdis.append(cfdi);
        }
        d.solicitudes.append(sol);
    }
    if (!leerMeses(l, o.value(QLatin1String("meses")), clavesPerfil, QDateTime::fromString(d.ahora, Qt::ISODate), d)) {
        return false;
    }
    if (const QJsonValue r = o.value(QLatin1String("respaldo")); !r.isUndefined()) {
        const QJsonObject x = r.toObject();
        l.contexto = QStringLiteral("datos.respaldo");
        if (!r.isObject() || !l.soloClaves(x, {QStringLiteral("carpeta"), QStringLiteral("contrasena"),
                                               QStringLiteral("diario"), QStringLiteral("ultimoExito")})) {
            return l.fallar(QStringLiteral("debe ser un objeto con carpeta, contrasena, diario y ultimoExito"));
        }
        d.respaldo.declarado = true;
        d.respaldo.carpeta = x.value(QLatin1String("carpeta")).toBool(false);
        d.respaldo.contrasena = x.value(QLatin1String("contrasena")).toBool(false);
        d.respaldo.diario = x.value(QLatin1String("diario")).toBool(false);
        d.respaldo.ultimoExito = l.texto(x, "ultimoExito", false);
        if (!l.error.isEmpty()) {
            return false;
        }
        if (!d.respaldo.ultimoExito.isEmpty() && !QDateTime::fromString(d.respaldo.ultimoExito, Qt::ISODate).isValid()) {
            return l.fallar(QStringLiteral("\"ultimoExito\" debe ser ISO 8601"));
        }
        if (d.respaldo.diario && !(d.respaldo.carpeta && d.respaldo.contrasena)) {
            return l.fallar(QStringLiteral("el respaldo diario requiere carpeta y contrasena"));
        }
    }
    l.contexto.clear();
    return true;
}

} // namespace

const QStringList& estadosSolicitud()
{
    static const QStringList k{QStringLiteral("creada"),     QStringLiteral("enviando"),   QStringLiteral("enviada"),
                               QStringLiteral("enProceso"),  QStringLiteral("error"),      QStringLiteral("terminada"),
                               QStringLiteral("descargando"), QStringLiteral("descargada")};
    return k;
}

const QStringList& ilustracionesDisponibles()
{
    static const QStringList k{QStringLiteral("menuBar"),          QStringLiteral("iconoMenuBar"),
                               QStringLiteral("finder"),           QStringLiteral("selectorArchivos"),
                               QStringLiteral("selectorCarpeta"), QStringLiteral("notificacion")};
    return k;
}

const QStringList& estadosMes()
{
    static const QStringList k{QStringLiteral("sinDescargar"), QStringLiteral("preparando"),
                               QStringLiteral("disponibles"), QStringLiteral("disponiblesParcialmente"),
                               QStringLiteral("requiereAtencion")};
    return k;
}

QString vocabularioProhibido(const QString& texto)
{
    // Sin acentos ni mayusculas (NFD sin marcas combinantes) y por palabra
    // completa: "solicitud(es)" y "paquete(s)" (T024 D3).
    QString plano;
    for (const QChar c : texto.normalized(QString::NormalizationForm_D)) {
        if (c.category() != QChar::Mark_NonSpacing) {
            plano.append(c.toLower());
        }
    }
    static const QRegularExpression kProhibidas(QStringLiteral("\\b(solicitud(es)?|paquetes?)\\b"),
                                                QRegularExpression::UseUnicodePropertiesOption);
    const QRegularExpressionMatch m = kProhibidas.match(plano);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString nombreEscena(const Capitulo& c, const Escena& e)
{
    return QStringLiteral("escena '%1/%2'").arg(c.id, e.id);
}

QString leerGuion(const QString& ruta, Guion& guion)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) {
        return QStringLiteral("no se pudo abrir el guion %1").arg(ruta);
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        return QStringLiteral("guion JSON invalido (byte %1): %2").arg(pe.offset).arg(pe.errorString());
    }
    const QJsonObject raiz = doc.object();
    Lector l;
    if (!l.soloClaves(raiz, {QStringLiteral("esquema"), QStringLiteral("tipo"), QStringLiteral("titulo"),
                             QStringLiteral("presupuestoTotal"), QStringLiteral("cuadrosTitulo"),
                             QStringLiteral("rotuloHerramienta"), QStringLiteral("pausas"), QStringLiteral("montaje"),
                             QStringLiteral("musica"), QStringLiteral("composicion"), QStringLiteral("ventana"),
                             QStringLiteral("portada"), QStringLiteral("cierre"),
                             QStringLiteral("voz"), QStringLiteral("datos"), QStringLiteral("capitulos")})) {
        return l.error;
    }
    // D16 (T024.4): huella del archivo; la llevan la linea de tiempo, el MP4 y
    // el manifiesto de verificacion.
    f.seek(0);
    guion.huella = QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
    if (const QJsonValue v = raiz.value(QLatin1String("ventana")); !v.isUndefined()) {
        // T024.4: ventana logica de la app (motion: 960x540 o 960x600). Debe
        // caber en el lienzo de 1280x720 logicos bajo la barra de titulo.
        const QJsonObject o = v.toObject();
        Lector lv;
        lv.contexto = QStringLiteral("ventana");
        if (!v.isObject() || !lv.soloClaves(o, {QStringLiteral("ancho"), QStringLiteral("alto")})) {
            return lv.error.isEmpty() ? QStringLiteral("ventana: debe ser un objeto {ancho, alto}") : lv.error;
        }
        guion.ventanaAncho = lv.entero(o, "ancho", 800, 1200, true).value_or(0);
        guion.ventanaAlto = lv.entero(o, "alto", 450, 640, true).value_or(0);
        if (!lv.error.isEmpty()) {
            return lv.error;
        }
    }
    guion.esquema = l.entero(raiz, "esquema", 0, 1000, true).value_or(0);
    if (l.error.isEmpty() && guion.esquema != kEsquema) {
        return QStringLiteral("esquema %1 no soportado (se espera %2)").arg(guion.esquema).arg(kEsquema);
    }
    guion.tipo = l.texto(raiz, "tipo");
    guion.titulo = l.texto(raiz, "titulo");
    guion.cuadrosTitulo = l.entero(raiz, "cuadrosTitulo", 0, 300, false).value_or(75);
    if (const QJsonValue v = raiz.value(QLatin1String("voz")); !v.isUndefined()) {
        // Voz del paso TTS (D9): fija modelo, voz, instrucciones, formato y
        // velocidad. Nada de valores por omision silenciosos por una clave mal
        // escrita o un tipo erroneo: error de validacion.
        if (!v.isObject()) {
            return QStringLiteral("voz: debe ser un objeto");
        }
        const QJsonObject o = v.toObject();
        Lector lv;
        lv.contexto = QStringLiteral("voz");
        if (!lv.soloClaves(o, {QStringLiteral("modelo"), QStringLiteral("voz"), QStringLiteral("instrucciones"),
                               QStringLiteral("formato"), QStringLiteral("velocidad")})) {
            return lv.error;
        }
        for (const char* clave : {"modelo", "voz", "instrucciones", "formato"}) {
            const QJsonValue t = o.value(QLatin1String(clave));
            if (!t.isUndefined() && (!t.isString() || t.toString().trimmed().isEmpty())) {
                return QStringLiteral("voz: \"%1\" debe ser un texto no vacio").arg(QLatin1String(clave));
            }
        }
        if (const QJsonValue formato = o.value(QLatin1String("formato"));
            !formato.isUndefined() && formato.toString() != QLatin1String("wav")) {
            return QStringLiteral("voz: \"formato\" solo admite \"wav\"");
        }
        if (const QJsonValue n = o.value(QLatin1String("velocidad"));
            !n.isUndefined() && (!n.isDouble() || n.toDouble() < 0.25 || n.toDouble() > 4.0)) {
            return QStringLiteral("voz: \"velocidad\" debe ser un numero entre 0.25 y 4");
        }
        guion.voz = o.toVariantMap();
    }
    guion.rotuloHerramienta = raiz.value(QLatin1String("rotuloHerramienta")).toBool(false);
    for (const auto& [clave, destino] : {std::pair{QLatin1String("portada"), &guion.portada},
                                         std::pair{QLatin1String("cierre"), &guion.cierre}}) {
        const QJsonValue v = raiz.value(clave);
        if (!v.isUndefined() && !v.isBool()) {
            return QStringLiteral("\"%1\" debe ser true o false").arg(clave);
        }
        *destino = v.toBool(false);
    }
    if (!l.error.isEmpty()) {
        return l.error;
    }
    {
        // D19: pausas minimas (ms) dentro de sus rangos.
        const QJsonObject p = raiz.value(QLatin1String("pausas")).toObject();
        l.contexto = QStringLiteral("pausas");
        if (!l.soloClaves(p, {QStringLiteral("bloque"), QStringLiteral("escena"), QStringLiteral("capitulo")})) {
            return l.error;
        }
        guion.pausaBloqueMs = l.entero(p, "bloque", 250, 400, false).value_or(guion.pausaBloqueMs);
        guion.pausaEscenaMs = l.entero(p, "escena", 500, 800, false).value_or(guion.pausaEscenaMs);
        guion.pausaCapituloMs = l.entero(p, "capitulo", 800, 1200, false).value_or(guion.pausaCapituloMs);
        if (!l.error.isEmpty()) {
            return l.error;
        }
        l.contexto.clear();
    }
    {
        const QJsonObject m = raiz.value(QLatin1String("montaje")).toObject();
        if (!l.soloClaves(m, {QStringLiteral("tempo"), QStringLiteral("pausaMaxima")})) {
            return QStringLiteral("montaje: ") + l.error;
        }
        const QJsonValue t = m.value(QLatin1String("tempo"));
        if (!t.isUndefined()) {
            if (!t.isDouble() || t.toDouble() < 0.8 || t.toDouble() > 1.25) {
                return QStringLiteral("montaje: \"tempo\" debe ser un numero entre 0.8 y 1.25");
            }
            guion.tempo = t.toDouble();
        }
        const QJsonValue pm = m.value(QLatin1String("pausaMaxima"));
        if (!pm.isUndefined()) {
            if (!pm.isDouble() || pm.toDouble() != std::floor(pm.toDouble()) || pm.toInt() < 100 || pm.toInt() > 1000) {
                return QStringLiteral("montaje: \"pausaMaxima\" debe ser un entero de 100 a 1000 ms");
            }
            guion.pausaMaxima = pm.toInt();
        }
    }
    if (const QJsonValue v = raiz.value(QLatin1String("musica")); !v.isUndefined()) {
        // R4: musica de fondo. Tipos, campos conocidos y rangos de
        // 08-audio-musica.md; los fundidos se comparan con la duracion al
        // planificar la mezcla.
        if (!v.isObject()) {
            return QStringLiteral("musica: debe ser un objeto");
        }
        const QJsonObject m = v.toObject();
        Lector lm;
        lm.contexto = QStringLiteral("musica");
        if (!lm.soloClaves(m, {QStringLiteral("activa"), QStringLiteral("preset"), QStringLiteral("nivelLufs"),
                               QStringLiteral("duckDb"), QStringLiteral("ataqueMs"), QStringLiteral("retencionMs"),
                               QStringLiteral("liberacionMs"), QStringLiteral("entradaMs"), QStringLiteral("salidaMs")})) {
            return lm.error;
        }
        ConfigMusica& c = guion.musica;
        if (const QJsonValue a = m.value(QLatin1String("activa")); !a.isUndefined()) {
            if (!a.isBool()) {
                return QStringLiteral("musica: \"activa\" debe ser true o false");
            }
            c.activa = a.toBool();
        }
        if (const QJsonValue p = m.value(QLatin1String("preset")); !p.isUndefined()) {
            if (!p.isString() || p.toString() != QLatin1String("pad-fiscal-v1")) {
                return QStringLiteral("musica: \"preset\" solo admite \"pad-fiscal-v1\"");
            }
        }
        const auto numero = [&](const char* clave, double minimo, double maximo, double& destino) -> QString {
            const QJsonValue n = m.value(QLatin1String(clave));
            if (n.isUndefined()) {
                return {};
            }
            if (!n.isDouble() || !std::isfinite(n.toDouble()) || n.toDouble() < minimo || n.toDouble() > maximo) {
                return QStringLiteral("musica: \"%1\" debe ser un numero entre %2 y %3")
                    .arg(QLatin1String(clave))
                    .arg(minimo)
                    .arg(maximo);
            }
            destino = n.toDouble();
            return {};
        };
        for (const QString& e : {numero("nivelLufs", -45, -30, c.nivelLufs), numero("duckDb", -12, -4, c.duckDb)}) {
            if (!e.isEmpty()) {
                return e;
            }
        }
        c.ataqueMs = lm.entero(m, "ataqueMs", 50, 300, false).value_or(c.ataqueMs);
        c.retencionMs = lm.entero(m, "retencionMs", 100, 500, false).value_or(c.retencionMs);
        c.liberacionMs = lm.entero(m, "liberacionMs", 500, 2000, false).value_or(c.liberacionMs);
        c.entradaMs = lm.entero(m, "entradaMs", 500, 5000, false).value_or(c.entradaMs);
        c.salidaMs = lm.entero(m, "salidaMs", 500, 5000, false).value_or(c.salidaMs);
        if (!lm.error.isEmpty()) {
            return lm.error;
        }
    }
    {
        // D8/D21/D22: composicion (todo opcional; por omision la propuesta de
        // motion design).
        const QJsonObject c = raiz.value(QLatin1String("composicion")).toObject();
        Lector lc;
        lc.contexto = QStringLiteral("composicion");
        if (!lc.soloClaves(c, {QStringLiteral("lienzo"), QStringLiteral("fondo"), QStringLiteral("transcripcion")})) {
            return lc.error;
        }
        const QJsonValue lienzo = c.value(QLatin1String("lienzo"));
        if (!lienzo.isUndefined()
            && (lienzo.toArray().size() != 2 || lienzo.toArray().at(0).toInt() != 2560 || lienzo.toArray().at(1).toInt() != 1440)) {
            return QStringLiteral("composicion: \"lienzo\" solo admite [2560, 1440] (D8)");
        }
        const QJsonObject fondo = c.value(QLatin1String("fondo")).toObject();
        lc.contexto = QStringLiteral("composicion.fondo");
        if (!lc.soloClaves(fondo, {QStringLiteral("semilla"), QStringLiteral("paleta"), QStringLiteral("velocidad")})) {
            return lc.error;
        }
        guion.composicion.semilla = lc.entero(fondo, "semilla", 0, 1000000, false).value_or(guion.composicion.semilla);
        if (!lc.error.isEmpty()) {
            return lc.error;
        }
        const QJsonValue paleta = fondo.value(QLatin1String("paleta"));
        if (!paleta.isUndefined()) {
            static const QRegularExpression kColor(QStringLiteral("^#[0-9A-Fa-f]{6}$"));
            QStringList colores;
            for (const QJsonValue& v : paleta.toArray()) {
                colores.append(v.toString());
            }
            if (colores.size() != 5 || !std::all_of(colores.cbegin(), colores.cend(), [](const QString& x) {
                    return kColor.match(x).hasMatch();
                })) {
                return QStringLiteral("composicion.fondo: \"paleta\" son 5 colores #RRGGBB (base, Santos, luz, sombra, "
                                      "acento)");
            }
            guion.composicion.paleta = colores;
        }
        const QJsonValue velocidad = fondo.value(QLatin1String("velocidad"));
        if (!velocidad.isUndefined()) {
            if (!velocidad.isDouble() || velocidad.toDouble() < 0 || velocidad.toDouble() > 0.2) {
                return QStringLiteral("composicion.fondo: \"velocidad\" entre 0 y 0.2");
            }
            guion.composicion.velocidad = velocidad.toDouble();
        }
        const QJsonObject tr = c.value(QLatin1String("transcripcion")).toObject();
        lc.contexto = QStringLiteral("composicion.transcripcion");
        if (!lc.soloClaves(tr, {QStringLiteral("lineas")})) {
            return lc.error;
        }
        guion.composicion.lineasTranscripcion = lc.entero(tr, "lineas", 1, 2, false).value_or(2);
        if (!lc.error.isEmpty()) {
            return lc.error;
        }
    }
    if (guion.tipo == QLatin1String("tutorial")) {
        // D13 (T024.4): el tutorial definitivo siempre de 5 a 8 minutos.
        guion.totalMin = kTotalTutorialMin;
        guion.totalMax = kTotalTutorialMax;
    } else if (guion.tipo == QLatin1String("ejemplo")) {
        const QJsonObject p = raiz.value(QLatin1String("presupuestoTotal")).toObject();
        l.contexto = QStringLiteral("presupuestoTotal");
        guion.totalMin = l.entero(p, "min", 1, 1000000, true).value_or(0);
        guion.totalMax = l.entero(p, "max", 1, 1000000, true).value_or(0);
        if (!l.error.isEmpty()) {
            return l.error;
        }
        l.contexto.clear();
    } else {
        return QStringLiteral("\"tipo\" debe ser tutorial o ejemplo");
    }
    if (!leerDatos(l, raiz.value(QLatin1String("datos")).toObject(), guion.datos)) {
        return l.error;
    }
    QSet<QString> clavesSolicitud;
    for (const SolicitudGuion& s : guion.datos.solicitudes) {
        clavesSolicitud.insert(s.clave);
    }
    QSet<QString> clavesPerfil;
    for (const PerfilGuion& p : guion.datos.perfiles) {
        clavesPerfil.insert(p.clave);
    }

    QSet<QString> idsCapitulo;
    for (const QJsonValue& vc : raiz.value(QLatin1String("capitulos")).toArray()) {
        const QJsonObject oc = vc.toObject();
        Capitulo cap;
        cap.id = l.texto(oc, "id");
        cap.titulo = l.texto(oc, "titulo");
        l.contexto = QStringLiteral("capitulo '%1'").arg(cap.id);
        if (!l.error.isEmpty() || !l.soloClaves(oc, {QStringLiteral("id"), QStringLiteral("titulo"), QStringLiteral("escenas")})) {
            return l.error;
        }
        if (idsCapitulo.contains(cap.id)) {
            return l.contexto + QStringLiteral(": id repetido");
        }
        idsCapitulo.insert(cap.id);
        QSet<QString> idsEscena;
        for (const QJsonValue& ve : oc.value(QLatin1String("escenas")).toArray()) {
            const QJsonObject oe = ve.toObject();
            Escena esc;
            esc.id = oe.value(QLatin1String("id")).toString();
            l.contexto = nombreEscena(cap, esc);
            if (esc.id.isEmpty()) {
                return l.contexto + QStringLiteral(": falta \"id\"");
            }
            if (idsEscena.contains(esc.id)) {
                return l.contexto + QStringLiteral(": id repetido");
            }
            idsEscena.insert(esc.id);
            if (!l.soloClaves(oe, {QStringLiteral("id"), QStringLiteral("presupuesto"), QStringLiteral("colchon"),
                                   QStringLiteral("separacionLineas"), QStringLiteral("acciones"), QStringLiteral("lineas")})) {
                return l.error;
            }
            const QJsonObject p = oe.value(QLatin1String("presupuesto")).toObject();
            esc.presupuestoMin = l.entero(p, "min", 1, 100000, true).value_or(0);
            esc.presupuestoMax = l.entero(p, "max", 1, 100000, true).value_or(0);
            esc.colchon = l.entero(oe, "colchon", 0, 300, false).value_or(15);
            esc.separacionLineas = l.entero(oe, "separacionLineas", 0, 300, false).value_or(6);
            if (!l.error.isEmpty()) {
                return l.error;
            }
            if (esc.presupuestoMin > esc.presupuestoMax) {
                return l.contexto + QStringLiteral(": presupuesto min > max");
            }
            QSet<QString> idsLinea;
            for (const QJsonValue& vl : oe.value(QLatin1String("lineas")).toArray()) {
                const QJsonObject ol = vl.toObject();
                if (!l.soloClaves(ol, {QStringLiteral("id"), QStringLiteral("texto"), QStringLiteral("voz"), QStringLiteral("inicio"),
                                       QStringLiteral("nuevoBloque")})) {
                    return l.error;
                }
                Linea lin;
                lin.id = l.texto(ol, "id");
                lin.texto = l.texto(ol, "texto").trimmed();
                lin.voz = l.texto(ol, "voz", false).trimmed();
                lin.inicioPedido = l.entero(ol, "inicio", 0, 100000, false);
                lin.nuevoBloque = ol.value(QLatin1String("nuevoBloque")).toBool(false);
                if (!l.error.isEmpty()) {
                    return l.error;
                }
                // D19: cada linea termina en puntuacion de fin de frase (la
                // alineacion cuenta frases dentro del bloque).
                static const QRegularExpression kFinFrase(QStringLiteral("[.?!:]$"));
                if (!kFinFrase.match(lin.texto).hasMatch() || !kFinFrase.match(lin.textoHablado()).hasMatch()) {
                    return l.contexto + QStringLiteral(": la linea '%1' debe terminar en . ? ! o : (texto y voz)").arg(lin.id);
                }
                // D13: toda linea tiene texto (un cue y un clip).
                if (lin.texto.isEmpty()) {
                    return l.contexto + QStringLiteral(": linea '%1' sin texto").arg(lin.id);
                }
                if (idsLinea.contains(lin.id)) {
                    return l.contexto + QStringLiteral(": linea '%1' repetida").arg(lin.id);
                }
                idsLinea.insert(lin.id);
                esc.lineas.append(lin);
            }
            for (const QJsonValue& va : oe.value(QLatin1String("acciones")).toArray()) {
                Accion a;
                if (!leerAccion(l, va.toObject(), a)) {
                    return l.error;
                }
                if (a.tipo == TipoAccion::EsperarLinea && !idsLinea.contains(a.linea)) {
                    return l.contexto + QStringLiteral(": esperarLinea a la linea desconocida '%1'").arg(a.linea);
                }
                // "nuevaN": la N-esima solicitud creada desde la UI durante la grabacion.
                static const QRegularExpression kNueva(QStringLiteral("^nueva[1-9]$"));
                if (a.tipo == TipoAccion::AvanzarSolicitud && !clavesSolicitud.contains(a.solicitud)
                    && !kNueva.match(a.solicitud).hasMatch()) {
                    return l.contexto + QStringLiteral(": solicitud desconocida '%1'").arg(a.solicitud);
                }
                if ((a.tipo == TipoAccion::CambiarEFirma || a.tipo == TipoAccion::CompletarEFirma
                     || a.tipo == TipoAccion::AvanzarMes)
                    && !clavesPerfil.contains(a.perfil)) {
                    return l.contexto + QStringLiteral(": perfil desconocido '%1'").arg(a.perfil);
                }
                if (a.tipo == TipoAccion::AvanzarMes
                    && std::none_of(guion.datos.meses.cbegin(), guion.datos.meses.cend(), [&](const MesGuion& m) {
                           return m.perfil == a.perfil && m.mes == a.mes;
                       })) {
                    return l.contexto
                           + QStringLiteral(": avanzarMes de '%1/%2', que no esta en datos.meses").arg(a.perfil, a.mes);
                }
                if ((a.tipo == TipoAccion::MostrarIlustracion && a.ilustracion == QLatin1String("notificacion"))
                    || a.tipo == TipoAccion::MostrarBanner) {
                    const QVariantList ns = a.parametros.value(QStringLiteral("notificaciones")).toList();
                    if (ns.isEmpty()) {
                        return l.contexto + QStringLiteral(": la ilustracion notificacion requiere parametros.notificaciones");
                    }
                    for (const QVariant& v : ns) {
                        const QVariantMap n = v.toMap();
                        const QString tipo = n.value(QStringLiteral("tipo")).toString();
                        const QString perfilN = n.value(QStringLiteral("perfil")).toString();
                        const QString mesN = n.value(QStringLiteral("mes")).toString();
                        // T024.4: facturasDescargadas = descarga guiada de un
                        // mes declarado en datos.meses, por direccion.
                        const bool mesDeclarado =
                            std::any_of(guion.datos.meses.cbegin(), guion.datos.meses.cend(), [&](const MesGuion& m) {
                                return m.perfil == perfilN && m.mes == mesN;
                            });
                        const bool ok = (tipo == QLatin1String("descargaCompleta")
                                         && clavesSolicitud.contains(n.value(QStringLiteral("solicitud")).toString()))
                                        || (tipo == QLatin1String("efirmaPorVencer") && clavesPerfil.contains(perfilN))
                                        || (tipo == QLatin1String("facturasDescargadas") && mesDeclarado
                                            && QStringList{QStringLiteral("emitidas"), QStringLiteral("recibidas")}.contains(
                                                n.value(QStringLiteral("direccion")).toString()));
                        if (!ok) {
                            return l.contexto
                                   + QStringLiteral(": notificacion invalida (tipo descargaCompleta con solicitud, "
                                                    "efirmaPorVencer con perfil, o facturasDescargadas con perfil, mes "
                                                    "de datos.meses y direccion emitidas|recibidas)");
                        }
                    }
                }
                for (const QString& c : a.claves) {
                    if (!clavesSolicitud.contains(c) && !kNueva.match(c).hasMatch()) {
                        return l.contexto + QStringLiteral(": solicitud desconocida '%1'").arg(c);
                    }
                }
                esc.acciones.append(a);
            }
            for (qsizetype i = 0; i + 1 < esc.acciones.size(); ++i) {
                esc.acciones[i].seguidoDeClic = esc.acciones.at(i + 1).tipo == TipoAccion::Clic;
            }
            // Bloques de narracion (D19).
            for (int i = 0; i < esc.lineas.size(); ++i) {
                if (i == 0 || esc.lineas.at(i).nuevoBloque) {
                    Bloque b;
                    b.id = i == 0 ? idEscena(cap, esc) : idEscena(cap, esc) + QLatin1Char('/') + esc.lineas.at(i).id;
                    b.primera = i;
                    esc.bloques.append(b);
                }
                Bloque& b = esc.bloques.last();
                ++b.cantidad;
                b.textosLineas.append(esc.lineas.at(i).textoHablado());
                b.textoHablado = b.textosLineas.join(QLatin1Char(' '));
                esc.lineas[i].bloque = int(esc.bloques.size()) - 1;
            }
            cap.escenas.append(esc);
        }
        if (cap.escenas.isEmpty()) {
            return QStringLiteral("capitulo '%1': sin escenas").arg(cap.id);
        }
        guion.capitulos.append(cap);
    }
    if (guion.capitulos.isEmpty()) {
        return QStringLiteral("el guion no tiene capitulos");
    }
    if (guion.tipo == QLatin1String("tutorial")) {
        // T024 D7: exactamente 7 capitulos. T024 D3: sin "solicitud(es)" ni
        // "paquete(s)" en lo que se ve (texto) ni en lo que se oye (voz).
        if (guion.capitulos.size() != kCapitulosTutorial) {
            return QStringLiteral("el tutorial debe tener exactamente %1 capitulos (tiene %2)")
                .arg(kCapitulosTutorial)
                .arg(guion.capitulos.size());
        }
        for (const Capitulo& c : guion.capitulos) {
            if (const QString p = vocabularioProhibido(c.titulo); !p.isEmpty()) {
                return QStringLiteral("capitulo '%1': el titulo usa \"%2\" (T024 D3: vocabulario retirado)").arg(c.id, p);
            }
            for (const Escena& e : c.escenas) {
                for (const Linea& lin : e.lineas) {
                    for (const auto& [campo, valor] : {std::pair{"texto", lin.texto}, std::pair{"voz", lin.voz}}) {
                        if (const QString p = vocabularioProhibido(valor); !p.isEmpty()) {
                            return nombreEscena(c, e)
                                   + QStringLiteral(": la linea '%1' usa \"%2\" en %3 (T024 D3: vocabulario retirado)")
                                         .arg(lin.id, p, QLatin1String(campo));
                        }
                    }
                }
            }
        }
    }
    return {};
}

QString idEscena(const Capitulo& c, const Escena& e)
{
    return c.id + QLatin1Char('/') + e.id;
}

Guion recortarGuion(const Guion& guion, const QSet<QString>& escenas)
{
    Guion r = guion;
    r.capitulos.clear();
    r.cuadrosPortada = 0; // el recorte solo lleva las escenas pedidas
    r.cuadrosCierre = 0;
    int cuadro = 0;
    for (const Capitulo& c : guion.capitulos) {
        Capitulo copia = c;
        copia.escenas.clear();
        copia.inicio = cuadro;
        for (const Escena& e : c.escenas) {
            if (escenas.contains(idEscena(c, e))) {
                Escena x = e;
                x.inicio = cuadro;
                cuadro += x.duracion;
                copia.escenas.append(x);
            }
        }
        copia.fin = cuadro;
        if (!copia.escenas.isEmpty()) {
            r.capitulos.append(copia);
        }
    }
    r.totalCuadros = cuadro;
    return r;
}

AudioBloque estimarAudioBloque(const Bloque& b)
{
    // 15 caracteres/s, con el silencio de extremos que deja el recorte.
    qsizetype total = 0;
    for (const QString& t : b.textosLineas) {
        total += t.size();
    }
    const qint64 voz = std::max<qint64>(muestrasDeMs(1000), qint64(total) * kMuestrasPorSegundo / kCaracteresPorSegundo);
    AudioBloque a;
    a.muestras = muestrasDeMs(kCabezaMs) + voz + muestrasDeMs(kColaMs);
    qsizetype acumulado = 0;
    for (const QString& t : b.textosLineas) {
        const qint64 inicioVoz = muestrasDeMs(kCabezaMs) + (total > 0 ? voz * acumulado / total : 0);
        a.marcas.append(a.marcas.isEmpty() ? 0 : std::max<qint64>(0, inicioVoz - muestrasDeMs(kAdelantoMarcaMs)));
        acumulado += t.size();
    }
    return a;
}

namespace {

int buscarLinea(const Escena& esc, const QString& id)
{
    for (int i = 0; i < esc.lineas.size(); ++i) {
        if (esc.lineas.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

qint64 alCuadroSiguiente(qint64 muestra)
{
    return qint64(cuadrosDeMuestras(muestra)) * kMuestrasPorCuadro;
}

} // namespace

QString planificar(Guion& guion, const OpcionesPlan& opciones)
{
    const auto escalar = [&](int cuadros) {
        if (cuadros <= 0 || opciones.escala >= 1.0) {
            return cuadros;
        }
        return std::max(1, int(std::lround(cuadros * opciones.escala)));
    };
    const qint64 silencioExistente = muestrasDeMs(kCabezaMs + kColaMs);
    // T024.4: la portada va antes del capitulo 1 (sin voz) y el cierre al final.
    guion.cuadrosPortada = guion.portada ? escalar(kCuadrosPortada) : 0;
    guion.cuadrosCierre = guion.cierre ? escalar(kCuadrosCierre) : 0;
    int cuadro = guion.cuadrosPortada;
    std::optional<qint64> finVozGlobal; // muestras globales donde termino la ultima voz
    for (qsizetype ci = 0; ci < guion.capitulos.size(); ++ci) {
        Capitulo& cap = guion.capitulos[ci];
        cap.inicio = cuadro;
        for (qsizetype ei = 0; ei < cap.escenas.size(); ++ei) {
            Escena& esc = cap.escenas[ei];
            esc.inicio = cuadro;

            // Audio de cada bloque (real o estimado).
            for (Bloque& b : esc.bloques) {
                std::optional<AudioBloque> audio =
                    opciones.audioBloque ? opciones.audioBloque(cap, esc, b) : std::optional<AudioBloque>(estimarAudioBloque(b));
                if (!audio || audio->muestras <= 0 || audio->marcas.size() != b.cantidad) {
                    return nombreEscena(cap, esc)
                           + QStringLiteral(", bloque '%1': falta su audio de voz alineado (ejecuta el paso de voz)").arg(b.id);
                }
                if (opciones.escala < 1.0) {
                    audio->muestras = std::max<qint64>(kMuestrasPorCuadro, qint64(double(audio->muestras) * opciones.escala));
                    for (qint64& m : audio->marcas) {
                        m = qint64(double(m) * opciones.escala);
                    }
                }
                b.muestras = audio->muestras;
                b.marcas = audio->marcas;
            }

            // D19: pausa minima antes de la primera voz de la escena.
            qint64 minimoVoz = 0;
            if (finVozGlobal) {
                const int pausaMs = ei == 0 ? guion.pausaCapituloMs : guion.pausaEscenaMs;
                minimoVoz = std::max<qint64>(0, *finVozGlobal + muestrasDeMs(pausaMs) - silencioExistente
                                                    - qint64(esc.inicio) * kMuestrasPorCuadro);
            }

            // Duraciones fijas de las acciones (todas salvo esperarLinea).
            for (Accion& a : esc.acciones) {
                if (a.tipo == TipoAccion::Clic) {
                    // El clic necesita sus cuadros de pulsacion aun en modo
                    // rapido; `cuadros` planificado incluye la pausa previa.
                    a.pausa = escalar(a.pausa);
                    a.cuadros = a.pausa + std::max(4, escalar(a.cuadros));
                } else if (a.tipo == TipoAccion::Tecla && a.keycap) {
                    // T024.4: el evento sale en el cuadro 6 del keycap, tambien
                    // en modo rapido.
                    a.cuadros = std::max(kCuadroEventoKeycap + 1, escalar(a.cuadros));
                } else if (a.tipo != TipoAccion::EsperarLinea) {
                    a.cuadros = escalar(a.cuadros);
                }
            }

            // Pausas extra (muestras) antes de cada linea, para que las
            // acciones alcancen a la voz: solo en fronteras entre lineas.
            QList<qint64> extra(esc.lineas.size(), 0);
            int t = 0;
            qint64 finVoz = -1;
            bool estable = false;
            for (int iter = 0; iter < 500 && !estable; ++iter) {
                // Voz: bloques continuos; pausas solo en fronteras.
                finVoz = -1;
                qint64 finBloque = -1;
                for (Bloque& b : esc.bloques) {
                    qint64 base = finBloque < 0 ? minimoVoz
                                                : finBloque + muestrasDeMs(guion.pausaBloqueMs) - silencioExistente;
                    const Linea& primera = esc.lineas.at(b.primera);
                    base = std::max(base, qint64(escalar(primera.inicioPedido.value_or(0))) * kMuestrasPorCuadro);
                    base = alCuadroSiguiente(base + extra.at(b.primera));
                    b.inicioMuestra = base;
                    qint64 acumulado = 0;
                    for (int j = 0; j < b.cantidad; ++j) {
                        Linea& lin = esc.lineas[b.primera + j];
                        qint64 inicio = base + b.marcas.at(j) + acumulado;
                        if (j > 0) {
                            inicio += extra.at(b.primera + j);
                            acumulado += extra.at(b.primera + j);
                            const qint64 pedido = qint64(escalar(lin.inicioPedido.value_or(0))) * kMuestrasPorCuadro;
                            if (inicio < pedido) {
                                acumulado += pedido - inicio;
                                inicio = pedido;
                            }
                        }
                        lin.inicioMuestra = inicio;
                        lin.origenInicio = b.marcas.at(j);
                        lin.origenFin = j + 1 < b.cantidad ? b.marcas.at(j + 1) : b.muestras;
                        const qint64 fin = inicio + (lin.origenFin - lin.origenInicio);
                        lin.inicio = int(inicio / kMuestrasPorCuadro);
                        lin.duracion = std::max(1, cuadrosDeMuestras(fin) - lin.inicio);
                        finBloque = fin;
                    }
                }
                finVoz = finBloque;
                // Un cue no se encima con el siguiente (cuantizacion a cuadros).
                for (int i = 0; i + 1 < esc.lineas.size(); ++i) {
                    Linea& lin = esc.lineas[i];
                    lin.duracion = std::max(1, std::min(lin.duracion, esc.lineas.at(i + 1).inicio - lin.inicio));
                }

                // Acciones: secuenciales. Si una accion llega tarde a la
                // linea que espera, la voz se desplaza desde esa frontera.
                estable = true;
                t = 0;
                for (Accion& a : esc.acciones) {
                    a.inicio = t;
                    if (a.tipo == TipoAccion::EsperarLinea) {
                        const int x = buscarLinea(esc, a.linea);
                        const Linea& lx = esc.lineas.at(x);
                        const int momento = a.alFinal ? lx.inicio + lx.duracion : lx.inicio;
                        // Llegar tarde al inicio de X desplaza X; llegar tarde
                        // al fin de X desplaza la linea siguiente (la voz de X
                        // ya termino). Nunca se corta la voz.
                        const int empujar = a.alFinal ? x + 1 : x;
                        if (empujar < esc.lineas.size() && t > esc.lineas.at(empujar).inicio
                            && (!a.alFinal || t > momento)) {
                            extra[empujar] += qint64(t - esc.lineas.at(empujar).inicio) * kMuestrasPorCuadro;
                            estable = false;
                            break;
                        }
                        a.cuadros = std::max(0, momento - t);
                    }
                    t += a.cuadros;
                }
            }
            if (!estable) {
                return nombreEscena(cap, esc) + QStringLiteral(": la voz y las acciones no convergen");
            }
            const int finVozCuadros = finVoz < 0 ? 0 : cuadrosDeMuestras(finVoz);
            esc.duracion = std::max(1, std::max(t, finVozCuadros) + escalar(esc.colchon));
            if (opciones.validarPresupuestos && (esc.duracion < esc.presupuestoMin || esc.duracion > esc.presupuestoMax)) {
                return nombreEscena(cap, esc)
                       + QStringLiteral(": dura %1 cuadros, fuera de su presupuesto [%2, %3]")
                             .arg(esc.duracion)
                             .arg(esc.presupuestoMin)
                             .arg(esc.presupuestoMax);
            }
            if (finVoz >= 0) {
                finVozGlobal = qint64(esc.inicio) * kMuestrasPorCuadro + finVoz;
            }
            cuadro += esc.duracion;
        }
        cap.fin = cuadro;
    }
    cuadro += guion.cuadrosCierre;
    guion.totalCuadros = cuadro;
    if (opciones.validarPresupuestos && (cuadro < guion.totalMin || cuadro > guion.totalMax)) {
        return QStringLiteral("duracion total %1 cuadros (%2 s), fuera de [%3, %4] cuadros")
            .arg(cuadro)
            .arg(cuadro / kFps)
            .arg(guion.totalMin)
            .arg(guion.totalMax);
    }
    return {};
}

} // namespace video
