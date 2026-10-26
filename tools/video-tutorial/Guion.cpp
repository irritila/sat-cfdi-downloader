#include "Guion.h"

#include "ResultadosExportacion.h"

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
    };
    const auto it = kTipos.constFind(t);
    if (it == kTipos.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

const QStringList& teclas()
{
    static const QStringList k{QStringLiteral("Enter"), QStringLiteral("Escape"), QStringLiteral("Tab"),
                               QStringLiteral("Abajo"), QStringLiteral("Arriba")};
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
        if (!l.soloClaves(o, {QStringLiteral("tipo"), QStringLiteral("tecla"), QStringLiteral("cuadros")})) {
            return false;
        }
        a.tecla = l.texto(o, "tecla");
        a.cuadros = l.entero(o, "cuadros", 1, 120, false).value_or(kCuadrosTecla);
        if (!a.tecla.isEmpty() && !teclas().contains(a.tecla)) {
            return l.fallar(QStringLiteral("tecla desconocida \"%1\" (usa %2)").arg(a.tecla, teclas().join(QStringLiteral(", "))));
        }
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
    }
    return l.error.isEmpty();
}

bool esFecha(const QString& s) { return QDate::fromString(s, Qt::ISODate).isValid(); }

bool leerDatos(Lector& l, const QJsonObject& o, DatosGuion& d)
{
    l.contexto = QStringLiteral("datos");
    if (!l.soloClaves(o, {QStringLiteral("ahora"), QStringLiteral("perfiles"), QStringLiteral("solicitudes")})) {
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
    static const QStringList k{QStringLiteral("menuBar"), QStringLiteral("iconoMenuBar"), QStringLiteral("finder"),
                               QStringLiteral("selectorArchivos"), QStringLiteral("notificacion")};
    return k;
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
                             QStringLiteral("musica"), QStringLiteral("composicion"),
                             QStringLiteral("voz"), QStringLiteral("datos"), QStringLiteral("capitulos")})) {
        return l.error;
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
        // D13: el tutorial definitivo siempre de 5 a 7 minutos.
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
                if ((a.tipo == TipoAccion::CambiarEFirma || a.tipo == TipoAccion::CompletarEFirma)
                    && !clavesPerfil.contains(a.perfil)) {
                    return l.contexto + QStringLiteral(": perfil desconocido '%1'").arg(a.perfil);
                }
                if (a.tipo == TipoAccion::MostrarIlustracion && a.ilustracion == QLatin1String("notificacion")) {
                    const QVariantList ns = a.parametros.value(QStringLiteral("notificaciones")).toList();
                    if (ns.isEmpty()) {
                        return l.contexto + QStringLiteral(": la ilustracion notificacion requiere parametros.notificaciones");
                    }
                    for (const QVariant& v : ns) {
                        const QVariantMap n = v.toMap();
                        const QString tipo = n.value(QStringLiteral("tipo")).toString();
                        const bool ok = (tipo == QLatin1String("descargaCompleta")
                                         && clavesSolicitud.contains(n.value(QStringLiteral("solicitud")).toString()))
                                        || (tipo == QLatin1String("efirmaPorVencer")
                                            && clavesPerfil.contains(n.value(QStringLiteral("perfil")).toString()));
                        if (!ok) {
                            return l.contexto
                                   + QStringLiteral(": notificacion invalida (tipo descargaCompleta con solicitud, o "
                                                    "efirmaPorVencer con perfil)");
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
    int cuadro = 0;
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
