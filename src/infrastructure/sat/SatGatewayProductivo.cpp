#include "infrastructure/sat/SatGatewayProductivo.h"

#include "domain/common/Rfc.h"
#include "infrastructure/sat/ClienteHttpSat.h"
#include "infrastructure/sat/RespuestasSat.h"
#include "infrastructure/sat/SaneadoRespuestaSat.h"
#include "infrastructure/sat/SobresSat.h"

#include <QEventLoop>
#include <QFutureWatcher>
#include <QRegularExpression>
#include <QTimeZone>
#include <QTimer>

#include <utility>

namespace satcfdi {

namespace {

constexpr qsizetype kChunkPaquete = 64 * 1024;

// Fuente por chunks sobre el ZIP ya decodificado (sin copias: mid() comparte).
class FuenteBytes final : public FuenteZipPorChunks {
public:
    explicit FuenteBytes(const QByteArray& datos) : m_datos(datos) {}
    Resultado<std::optional<QByteArray>, ErrorFuenteZip> siguiente() override
    {
        using R = Resultado<std::optional<QByteArray>, ErrorFuenteZip>;
        if (m_posicion >= m_datos.size()) {
            return R::exito(std::nullopt);
        }
        const QByteArray chunk = m_datos.mid(m_posicion, kChunkPaquete);
        m_posicion += chunk.size();
        return R::exito(chunk);
    }

private:
    const QByteArray& m_datos;
    qsizetype m_posicion = 0;
};

ErrorSatGateway preparacion(QString diagnostico)
{
    ErrorSatGateway e;
    e.fase = FaseSatGateway::Preparacion;
    e.diagnosticoSanitizado = std::move(diagnostico);
    return e;
}

bool esRfc(const QString& rfc)
{
    return !rfc.isEmpty() && rfc::normalizar(rfc) == rfc && rfc::esValido(rfc);
}

std::optional<QDateTime> fechaSat(const QString& texto)
{
    // xs:dateTime sin zona: digitos tal cual. Se construye en UTC para que
    // ningun hueco de horario de verano local invalide la fecha.
    const QDate fecha = QDate::fromString(texto.left(10), QStringLiteral("yyyy-MM-dd"));
    const QTime hora = QTime::fromString(texto.mid(11), QStringLiteral("HH:mm:ss"));
    if (texto.size() != 19 || texto.at(10) != QLatin1Char('T') || !fecha.isValid() || !hora.isValid()) {
        return std::nullopt;
    }
    return QDateTime(fecha, hora, QTimeZone::UTC);
}

// INFERIDO (T006 no observo un token rechazado): faultcodes WS-Security.
// Recibe el faultcode YA permitido (saneado::faultcodePermitido).
bool esFaultDeSeguridad(const QString& codigo)
{
    return codigo.contains(QStringLiteral("InvalidSecurity")) || codigo.contains(QStringLiteral("FailedAuthentication"))
           || codigo.contains(QStringLiteral("SecurityTokenUnavailable"))
           || codigo.contains(QStringLiteral("MessageExpired"));
}

class Llamada {
public:
    Llamada(const SatGatewayOptions& opciones, sat::Operacion operacion)
        : m_opciones(opciones), m_operacion(operacion)
    {
        const auto it = m_opciones.endpoints.find(m_operacion);
        m_url = it != m_opciones.endpoints.end() ? it->second : sat::descriptor(m_operacion).endpoint;
    }

    // Produccion: solo https://*.clouda.sat.gob.mx. Un endpoint distinto
    // requiere permitirEndpointsDePrueba (solo pruebas; el root nunca lo activa).
    std::optional<ErrorSatGateway> errorEndpoint() const
    {
        if (m_opciones.permitirEndpointsDePrueba || sat::saneado::esEndpointOficial(m_url)) {
            return std::nullopt;
        }
        return preparacion(nombre() + QStringLiteral(": endpoint no permitido"));
    }

    QString nombre() const { return sat::descriptor(m_operacion).nombre; }

    // Envia y espera en un event loop local del hilo actual.
    sat::ResultadoHttp enviar(const QByteArray& sobre, const TokenSat* token, std::chrono::milliseconds timeout,
                              const Cancelacion& cancelacion) const
    {
        if (cancelacion.solicitada()) {
            sat::ResultadoHttp r;
            r.fase = sat::FaseResultado::AntesDeEnvio;
            r.cancelado = true;
            r.diagnostico = QStringLiteral("cancelada antes de enviar");
            return r;
        }
        sat::PeticionSat peticion;
        peticion.url = m_url;
        peticion.soapAction = sat::descriptor(m_operacion).soapAction;
        peticion.sobre = sobre;

        sat::ClienteHttpSat cliente;
        cliente.establecerDeadline(timeout);
        QFuture<sat::ResultadoHttp> futuro = cliente.enviar(peticion, token);
        peticion.sobre = QByteArray(); // el sobre lleva firma y certificado: no se retiene
        QEventLoop loop;
        QFutureWatcher<sat::ResultadoHttp> observador;
        QObject::connect(&observador, &QFutureWatcherBase::finished, &loop, &QEventLoop::quit);
        QTimer sondeo;
        sondeo.setInterval(m_opciones.intervaloCancelacion);
        QObject::connect(&sondeo, &QTimer::timeout, &loop, [&]() {
            if (futuro.isFinished()) {
                loop.quit();
            } else if (cancelacion.solicitada()) {
                cliente.cancelarEnCurso();
            }
        });
        observador.setFuture(futuro);
        sondeo.start();
        while (!futuro.isFinished()) {
            loop.exec();
        }
        sondeo.stop();
        return futuro.result();
    }

    // Error de transporte (sin HTTP completo).
    ErrorSatGateway errorTransporte(const sat::ResultadoHttp& r) const
    {
        ErrorSatGateway e;
        e.fase = r.fase == sat::FaseResultado::DespuesDeEnvio ? FaseSatGateway::DespuesDeEnvio
                                                               : FaseSatGateway::AntesDeEnvio;
        e.cancelada = r.cancelado;
        e.deadlineVencido = r.deadlineVencido;
        e.diagnosticoSanitizado = QStringLiteral("%1: %2 (%3)%4%5")
                                      .arg(nombre(), claveEstable(e.fase), r.diagnostico,
                                           r.deadlineVencido ? QStringLiteral("; deadline") : QString(),
                                           r.cancelado ? QStringLiteral("; cancelada") : QString());
        return e;
    }

    // HTTP completo sin resultado utilizable.
    ErrorSatGateway errorExplicito(const sat::ResultadoHttp& r, const std::optional<sat::FaultSoap>& fault,
                                   const QString& motivo, bool usaToken) const
    {
        ErrorSatGateway e;
        e.fase = FaseSatGateway::RespuestaExplicita;
        e.estadoHttp = r.estadoHttp;
        // Lista permitida: nunca faultstring, detail ni el faultcode crudo.
        const std::optional<QString> codigo =
            fault ? std::optional<QString>(sat::saneado::faultcodePermitido(fault->codigo)) : std::nullopt;
        e.codigoSat = codigo;
        e.tokenRechazado = usaToken && (r.estadoHttp == 401 || (codigo && esFaultDeSeguridad(*codigo)));
        // Plantilla fija: operacion, HTTP, codigo permitido y clave tecnica.
        e.diagnosticoSanitizado = QStringLiteral("%1: HTTP %2%3; %4%5")
                                      .arg(nombre(), QString::number(r.estadoHttp.value_or(0)),
                                           codigo ? QStringLiteral("; Fault ") + *codigo : QString(), motivo,
                                           e.tokenRechazado ? QStringLiteral("; token rechazado (inferido)")
                                                            : QString());
        return e;
    }

    // Parsea un HTTP completo: 200 + resultado -> valor; lo demas -> error.
    template <typename T, typename Parser>
    Resultado<T, ErrorSatGateway> interpretar(sat::ResultadoHttp& r, Parser parser, bool usaToken) const
    {
        using R = Resultado<T, ErrorSatGateway>;
        if (r.fase != sat::FaseResultado::RespuestaExplicita) {
            return R::fallo(errorTransporte(r));
        }
        if (r.estadoHttp != 200) {
            const auto fault = sat::parsearFault(r.cuerpo);
            r.cuerpo = QByteArray();
            return R::fallo(errorExplicito(r, fault, QStringLiteral("HTTP no 200"), usaToken));
        }
        auto parseado = parser(r.cuerpo);
        r.cuerpo = QByteArray();
        if (!parseado) {
            const sat::ErrorRespuesta& er = parseado.error();
            return R::fallo(errorExplicito(r, er.fault, er.diagnostico, usaToken));
        }
        return R::exito(std::move(parseado).valor());
    }

private:
    const SatGatewayOptions& m_opciones;
    sat::Operacion m_operacion;
    QUrl m_url;
};

} // namespace

SatGatewayProductivo::SatGatewayProductivo(SatGatewayOptions opciones)
    : m_opciones(std::move(opciones))
{
    if (!m_opciones.reloj) {
        m_opciones.reloj = [] { return QDateTime::currentDateTimeUtc(); };
    }
}

Resultado<TokenSat, ErrorSatGateway> SatGatewayProductivo::autenticar(const MaterialFirma& material,
                                                                      const Cancelacion& cancelacion)
{
    using R = Resultado<TokenSat, ErrorSatGateway>;
    const Llamada llamada(m_opciones, sat::Operacion::Autentica);
    if (auto e = llamada.errorEndpoint()) {
        return R::fallo(*e);
    }
    sat::ContextoSobre contexto;
    contexto.ahoraUtc = m_opciones.reloj();
    auto sobre = sat::construirAutentica(material, contexto);
    if (!sobre) {
        return R::fallo(preparacion(QStringLiteral("Autentica: sobre: ") + sobre.error().diagnostico));
    }
    sat::ResultadoHttp r = llamada.enviar(sobre.valor().xml, nullptr, m_opciones.timeoutAutenticacion, cancelacion);
    if (r.fase != sat::FaseResultado::RespuestaExplicita) {
        return R::fallo(llamada.errorTransporte(r));
    }
    if (r.estadoHttp != 200) {
        const auto fault = sat::parsearFault(r.cuerpo);
        r.cuerpo = QByteArray();
        return R::fallo(llamada.errorExplicito(r, fault, QStringLiteral("HTTP no 200"), false));
    }
    // El cuerpo contiene el token: se parsea y se limpia en el mismo paso.
    auto parseado = sat::parsearAutenticaYLimpiar(r.cuerpo);
    if (!parseado) {
        return R::fallo(llamada.errorExplicito(r, parseado.error().fault, parseado.error().diagnostico, false));
    }
    return R::exito(std::move(std::move(parseado).valor().token));
}

Resultado<RespuestaCreacion, ErrorSatGateway> SatGatewayProductivo::crearSolicitud(const TokenSat& token,
                                                                                   const SolicitudSat& solicitud,
                                                                                   const MaterialFirma& material,
                                                                                   const Cancelacion& cancelacion)
{
    using R = Resultado<RespuestaCreacion, ErrorSatGateway>;
    const bool emitidos = solicitud.tipo == TipoDescarga::Emitidos;
    const sat::Operacion operacion =
        emitidos ? sat::Operacion::SolicitaDescargaEmitidos : sat::Operacion::SolicitaDescargaRecibidos;
    const Llamada llamada(m_opciones, operacion);
    if (auto e = llamada.errorEndpoint()) {
        return R::fallo(*e);
    }
    const auto inicial = fechaSat(solicitud.fechaInicial);
    const auto final = fechaSat(solicitud.fechaFinal);
    bool contrapartesValidas = emitidos || solicitud.contrapartes.size() <= 1;
    for (const QString& c : solicitud.contrapartes) {
        contrapartesValidas = contrapartesValidas && esRfc(c);
    }
    if (!esRfc(solicitud.rfcSolicitante) || !inicial || !final || *final < *inicial || !contrapartesValidas) {
        return R::fallo(preparacion(QStringLiteral("SolicitaDescarga: parametros invalidos")));
    }
    // ADR 0013: atributos efectivos por operacion.
    sat::ParametrosSolicitud p;
    p.rfcSolicitante = solicitud.rfcSolicitante;
    p.fechaInicial = *inicial;
    p.fechaFinal = *final;
    p.tipoSolicitud = QStringLiteral("CFDI");
    p.estadoComprobante = QStringLiteral("Vigente");
    p.tipoComprobante = solicitud.tipoComprobante.value_or(QString());
    p.complemento = solicitud.complemento.value_or(QString());
    if (emitidos) {
        p.rfcEmisor = solicitud.rfcSolicitante;
        p.rfcReceptores = solicitud.contrapartes;
    } else {
        p.rfcReceptor = solicitud.rfcSolicitante;
        p.rfcEmisor = solicitud.contrapartes.value(0);
    }
    auto sobre = emitidos ? sat::construirSolicitudEmitidos(p, material) : sat::construirSolicitudRecibidos(p, material);
    if (!sobre) {
        return R::fallo(preparacion(sat::descriptor(operacion).nombre + QStringLiteral(": sobre: ")
                                    + sobre.error().diagnostico));
    }
    sat::ResultadoHttp r = llamada.enviar(sobre.valor().xml, &token, m_opciones.timeoutCreacion, cancelacion);
    auto parseado = llamada.interpretar<sat::RespuestaSolicitud>(r, sat::parsearSolicitud, true);
    if (!parseado) {
        return R::fallo(parseado.error());
    }
    const sat::RespuestaSolicitud& s = parseado.valor();
    RespuestaCreacion c;
    c.codEstatus = sat::saneado::codigoSatPermitido(s.codEstatus);
    c.mensaje = sat::saneado::mensajePermitido(s.mensaje);
    // Un IdSolicitud con forma inesperada se trata como ausente (-> incierto).
    if (sat::saneado::esIdSolicitud(s.idSolicitud)) {
        c.idSolicitud = s.idSolicitud;
    }
    return R::exito(std::move(c));
}

Resultado<RespuestaVerificacion, ErrorSatGateway>
SatGatewayProductivo::verificarSolicitud(const TokenSat& token, const ConsultaSolicitudSat& consulta,
                                         const MaterialFirma& material, const Cancelacion& cancelacion)
{
    using R = Resultado<RespuestaVerificacion, ErrorSatGateway>;
    const Llamada llamada(m_opciones, sat::Operacion::VerificaSolicitudDescarga);
    if (auto e = llamada.errorEndpoint()) {
        return R::fallo(*e);
    }
    // Un IdSolicitud invalido produce 301 en el SAT (T006): se rechaza antes.
    if (!sat::saneado::esIdSolicitud(consulta.idSolicitud) || !esRfc(consulta.rfcSolicitante)) {
        return R::fallo(preparacion(QStringLiteral("VerificaSolicitudDescarga: parametros invalidos")));
    }
    auto sobre = sat::construirVerificacion(consulta.idSolicitud, consulta.rfcSolicitante, material);
    if (!sobre) {
        return R::fallo(preparacion(QStringLiteral("VerificaSolicitudDescarga: sobre: ") + sobre.error().diagnostico));
    }
    sat::ResultadoHttp r = llamada.enviar(sobre.valor().xml, &token, m_opciones.timeoutVerificacion, cancelacion);
    auto parseado = llamada.interpretar<sat::RespuestaVerificacion>(r, sat::parsearVerificacion, true);
    if (!parseado) {
        return R::fallo(parseado.error());
    }
    const sat::RespuestaVerificacion& v = parseado.valor();
    // Ids con forma inesperada: la respuesta no es utilizable (no se descartan
    // en silencio paquetes ni se exponen valores arbitrarios).
    for (const QString& id : v.idsPaquetes) {
        if (!sat::saneado::esIdPaquete(id)) {
            ErrorSatGateway e;
            e.fase = FaseSatGateway::RespuestaExplicita;
            e.estadoHttp = 200;
            e.diagnosticoSanitizado = QStringLiteral("VerificaSolicitudDescarga: HTTP 200; ids_paquete_invalidos");
            return R::fallo(e);
        }
    }
    RespuestaVerificacion out;
    out.codEstatus = sat::saneado::codigoSatPermitido(v.codEstatus);
    if (v.estadoSolicitud && *v.estadoSolicitud >= 0 && *v.estadoSolicitud <= 6) {
        out.estadoSolicitud = v.estadoSolicitud;
    }
    if (!v.codigoEstadoSolicitud.isEmpty()) {
        out.codigoEstadoSolicitud = sat::saneado::codigoSatPermitido(v.codigoEstadoSolicitud);
    }
    out.mensaje = sat::saneado::mensajePermitido(v.mensaje);
    if (v.numeroCfdis && *v.numeroCfdis >= 0) {
        out.numeroCfdi = *v.numeroCfdis;
    }
    out.idsPaquete = v.idsPaquetes;
    return R::exito(std::move(out));
}

Resultado<RespuestaDescarga, ErrorSatGateway>
SatGatewayProductivo::descargarPaquete(const TokenSat& token, const ConsultaPaqueteSat& consulta,
                                       const MaterialFirma& material, ReceptorPaqueteSat& receptor,
                                       const Cancelacion& cancelacion)
{
    using R = Resultado<RespuestaDescarga, ErrorSatGateway>;
    const Llamada llamada(m_opciones, sat::Operacion::Descargar);
    if (auto e = llamada.errorEndpoint()) {
        return R::fallo(*e);
    }
    if (!sat::saneado::esIdPaquete(consulta.idPaquete) || !esRfc(consulta.rfcSolicitante)) {
        return R::fallo(preparacion(QStringLiteral("Descargar: parametros invalidos")));
    }
    auto sobre = sat::construirDescarga(consulta.idPaquete, consulta.rfcSolicitante, material);
    if (!sobre) {
        return R::fallo(preparacion(QStringLiteral("Descargar: sobre: ") + sobre.error().diagnostico));
    }
    sat::ResultadoHttp r = llamada.enviar(sobre.valor().xml, &token, m_opciones.timeoutDescarga, cancelacion);
    auto parseado = llamada.interpretar<sat::RespuestaDescarga>(r, sat::parsearDescarga, true);
    if (!parseado) {
        return R::fallo(parseado.error());
    }
    sat::RespuestaDescarga d = std::move(parseado).valor();
    RespuestaDescarga out;
    out.codEstatus = sat::saneado::codigoSatPermitido(d.codEstatus);
    out.mensaje = sat::saneado::mensajePermitido(d.mensaje);
    if (out.codEstatus == QStringLiteral("5000")) {
        if (d.paquete.isEmpty()) {
            ErrorSatGateway e;
            e.fase = FaseSatGateway::RespuestaExplicita;
            e.estadoHttp = 200;
            e.codigoSat = out.codEstatus;
            e.diagnosticoSanitizado = QStringLiteral("Descargar: HTTP 200; Paquete vacio");
            return R::fallo(e);
        }
        FuenteBytes fuente(d.paquete);
        receptor.recibir(fuente);
        out.paqueteEntregado = true;
        out.bytesPaquete = d.paquete.size();
    }
    d.paquete = QByteArray();
    return R::exito(std::move(out));
}

} // namespace satcfdi
