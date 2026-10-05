#include "application/operaciones/OperacionesSatProductivo.h"

#include "application/operaciones/MensajesOperacionSat.h"
#include "application/operaciones/SaneamientoOperacion.h"
#include "application/profiles/AccesoCredencialSat.h"
#include "ports/PackageStorage.h"
#include "ports/SatGateway.h"

#include <QMutexLocker>

#include <optional>
#include <utility>

namespace satcfdi {

namespace {

namespace m = mensajessat;

const QString kCodigoExito = QStringLiteral("5000");

// "<mensaje D10>[ [<detalle tecnico saneado>]]" (ver desglosarUltimoError).
QString conDetalle(const QString& mensaje, const QString& tecnico)
{
    if (tecnico.isEmpty()) {
        return mensaje;
    }
    return mensaje + QStringLiteral(" [") + tecnico + QLatin1Char(']');
}

// Codigo y detalle tecnico pasan por el saneamiento defensivo (el ejecutor
// lo repite): un faultcode o diagnostico del gateway nunca llega crudo.
FallaOperacion falla(FaseOperacion fase, std::optional<QString> codigo, const QString& mensaje,
                     const QString& tecnico = {}, bool cancelada = false)
{
    FallaOperacion f = FallaOperacion::de(fase, saneamiento::codigoSeguro(codigo),
                                          conDetalle(mensaje, tecnico.isEmpty() ? QString()
                                                                                : saneamiento::textoSeguro(tecnico)));
    f.cancelada = cancelada;
    return f;
}

FallaOperacion canceladaAntesDeRed()
{
    return falla(FaseOperacion::Preparacion, QStringLiteral("cancelada"), QStringLiteral("Operacion cancelada."), {},
                 true);
}

// --- Credencial (D8 fila 1; D10 Credencial) ---------------------------------

bool esLlaveroNoDisponible(ErrorSecretStore::Categoria c)
{
    using C = ErrorSecretStore::Categoria;
    return c == C::AlmacenBloqueado || c == C::AccesoDenegado || c == C::CanceladoPorUsuario
           || c == C::AlmacenNoDisponible || c == C::AlmacenMalConfigurado;
}

FallaOperacion fallaCredencial(const ErrorCredencialSat& e)
{
    using C = ErrorSecretStore::Categoria;
    QString codigo;
    QString mensaje = m::credencialIlegible();
    switch (e.tipo) {
    case ErrorCredencialSat::Tipo::Almacen: {
        const C categoria = e.categoria.value_or(C::Interno);
        codigo = QStringLiteral("credencial:") + claveEstable(categoria);
        if (esLlaveroNoDisponible(categoria)) {
            mensaje = m::llaveroBloqueado();
        } else if (categoria == C::Vencida) {
            mensaje = m::credencialVencida();
        } else if (categoria == C::NoVigenteAun) {
            mensaje = m::credencialNoVigenteAun();
        }
        break;
    }
    case ErrorCredencialSat::Tipo::PerfilInvalido: codigo = QStringLiteral("credencial:perfil_invalido"); break;
    case ErrorCredencialSat::Tipo::Persistencia: codigo = QStringLiteral("credencial:persistencia"); break;
    case ErrorCredencialSat::Tipo::HiloNoPermitido: codigo = QStringLiteral("credencial:hilo_no_permitido"); break;
    case ErrorCredencialSat::Tipo::CredencialExistente: codigo = QStringLiteral("credencial:existente"); break;
    }
    return falla(FaseOperacion::Preparacion, codigo, mensaje);
}

FallaOperacion fallaEstadoCredencial(EstadoCredencial estado)
{
    QString mensaje = m::credencialIlegible();
    if (estado == EstadoCredencial::Vencida) {
        mensaje = m::credencialVencida();
    } else if (estado == EstadoCredencial::NoVigenteAun) {
        mensaje = m::credencialNoVigenteAun();
    }
    return falla(FaseOperacion::Preparacion, QStringLiteral("credencial:") + claveEstable(estado), mensaje);
}

// --- Gateway -----------------------------------------------------------------

FaseOperacion faseDe(FaseSatGateway fase)
{
    switch (fase) {
    case FaseSatGateway::Preparacion: return FaseOperacion::Preparacion;
    case FaseSatGateway::AntesDeEnvio: return FaseOperacion::AntesDeEnvio;
    case FaseSatGateway::DespuesDeEnvio: return FaseOperacion::DespuesDeEnvio;
    case FaseSatGateway::RespuestaExplicita: return FaseOperacion::RespuestaExplicita;
    }
    return FaseOperacion::RespuestaExplicita;
}

// CodEstatus/faultcode o, sin el, el HTTP (solo RespuestaExplicita).
std::optional<QString> codigoDe(const ErrorSatGateway& e)
{
    if (e.fase != FaseSatGateway::RespuestaExplicita) {
        return std::nullopt;
    }
    if (e.codigoSat && !e.codigoSat->trimmed().isEmpty()) {
        return e.codigoSat->trimmed();
    }
    if (e.estadoHttp) {
        return QString::number(*e.estadoHttp);
    }
    return std::nullopt;
}

QString tecnicoDe(const ErrorSatGateway& e)
{
    QString t = e.diagnosticoSanitizado;
    if (e.deadlineVencido && !t.contains(QStringLiteral("timeout"))) {
        t += t.isEmpty() ? QStringLiteral("timeout") : QStringLiteral("; timeout");
    }
    return t;
}

// Cualquier falla de Autentica -> Autenticacion (D8 fila 2).
FallaOperacion fallaAutenticacion(const ErrorSatGateway& e)
{
    QString mensaje = m::autenticacionSinConexion();
    if (e.fase == FaseSatGateway::RespuestaExplicita) {
        mensaje = m::autenticacionRechazada();
    } else if (e.fase == FaseSatGateway::Preparacion) {
        mensaje = m::credencialIlegible();
    }
    return falla(FaseOperacion::Autenticacion, codigoDe(e), mensaje, tecnicoDe(e), e.cancelada);
}

FallaOperacion fallaEnvio(const ErrorSatGateway& e)
{
    const FaseOperacion fase = faseDe(e.fase);
    const bool noEnviada = fase == FaseOperacion::Preparacion || fase == FaseOperacion::AntesDeEnvio;
    return falla(fase, codigoDe(e), noEnviada ? m::envioNoIniciado() : m::envioIncierto(), tecnicoDe(e), e.cancelada);
}

FallaOperacion fallaVerificacion(const ErrorSatGateway& e)
{
    return falla(faseDe(e.fase), codigoDe(e), m::verificacionTransitoria(), tecnicoDe(e), e.cancelada);
}

FallaOperacion fallaDescarga(const ErrorSatGateway& e)
{
    return falla(faseDe(e.fase), codigoDe(e), m::descargaFallida(), tecnicoDe(e), e.cancelada);
}

// Mensaje visible de la respuesta de creacion (ResultadoEnvio::mensaje): el
// Mensaje SAT crudo no sale del adaptador (D10).
QString mensajeCreacion(const RespuestaCreacion& r)
{
    const QString c = saneamiento::codigoSeguro(QStringView(r.codEstatus));
    if (c == kCodigoExito) {
        return r.idSolicitud ? m::solicitudAceptada() : m::envioIncierto();
    }
    if (c == QStringLiteral("5001")) {
        return m::solicitudNoAutorizada();
    }
    if (c == QStringLiteral("5002")) {
        return m::limiteCriterio5002();
    }
    if (c == QStringLiteral("5003")) {
        return m::limiteMaximo5003();
    }
    if (c == QStringLiteral("5005")) {
        return m::solicitudActiva5005();
    }
    if (c == QStringLiteral("5011")) {
        return m::limiteDiario5011();
    }
    static const QStringList kRechazos = {QStringLiteral("300"), QStringLiteral("301"), QStringLiteral("302"),
                                          QStringLiteral("303"), QStringLiteral("304"), QStringLiteral("305")};
    if (kRechazos.contains(c)) {
        return m::solicitudRechazada(c);
    }
    return m::envioIncierto(); // 5006, no documentado
}

std::optional<EstadoSolicitudSat> estadoSatDesdeEntero(int v)
{
    switch (v) {
    case 1: return EstadoSolicitudSat::Aceptada;
    case 2: return EstadoSolicitudSat::EnProceso;
    case 3: return EstadoSolicitudSat::Terminada;
    case 4: return EstadoSolicitudSat::Error;
    case 5: return EstadoSolicitudSat::Rechazada;
    case 6: return EstadoSolicitudSat::Vencida;
    default: return std::nullopt;
    }
}

// VerificaSolicitudDescarga con CodEstatus distinto de 5000 (D8).
FallaOperacion fallaCodigoVerificacion(const QString& codigo)
{
    if (codigo == QStringLiteral("304")) {
        return falla(FaseOperacion::Preparacion, codigo, m::credencialVencida());
    }
    if (codigo == QStringLiteral("305")) {
        return falla(FaseOperacion::Preparacion, codigo, m::credencialIlegible());
    }
    QString mensaje = m::verificacionTransitoria();
    if (codigo == QStringLiteral("5004")) {
        mensaje = m::solicitudNoEncontrada();
    } else if (codigo == QStringLiteral("5011")) {
        mensaje = m::limiteDiario5011();
    }
    return falla(FaseOperacion::RespuestaExplicita, codigo, mensaje);
}

FallaOperacion fallaAlmacenamiento(const ErrorGuardarZip& e)
{
    if (e.origen == ErrorGuardarZip::Origen::Fuente) {
        // Fuente invalida o truncada: como Paquete invalido (D8).
        return falla(FaseOperacion::RespuestaExplicita, kCodigoExito, m::descargaFallida(), e.diagnostico);
    }
    FallaOperacion f;
    switch (e.almacenamiento) {
    case ErrorAlmacenamiento::Cancelada:
        f = falla(FaseOperacion::Almacenamiento, std::nullopt, m::descargaFallida(), claveEstable(e.almacenamiento),
                  true);
        return f;
    case ErrorAlmacenamiento::ColisionDestino:
        f = falla(FaseOperacion::Almacenamiento, std::nullopt, m::colisionDestino(), e.diagnostico);
        f.causaAlmacenamiento = CausaAlmacenamiento::ColisionDestino;
        return f;
    case ErrorAlmacenamiento::SinEspacio:
        f = falla(FaseOperacion::Almacenamiento, std::nullopt, m::sinEspacio(), e.diagnostico);
        f.causaAlmacenamiento = CausaAlmacenamiento::EspacioInsuficiente;
        return f;
    case ErrorAlmacenamiento::EntradaInvalida:
    case ErrorAlmacenamiento::Permiso:
    case ErrorAlmacenamiento::Escritura:
    case ErrorAlmacenamiento::Durabilidad:
    case ErrorAlmacenamiento::Promocion:
    case ErrorAlmacenamiento::LecturaRaiz:
        break;
    }
    f = falla(FaseOperacion::Almacenamiento, std::nullopt, m::sinPermisoEscritura(),
              e.diagnostico.isEmpty() ? claveEstable(e.almacenamiento) : e.diagnostico);
    f.causaAlmacenamiento = CausaAlmacenamiento::EscrituraFallida;
    return f;
}

// Solicitud de dominio -> SolicitudSat (ADR 0013; mapeo DC2 de T001).
SolicitudSat solicitudSatDe(const SolicitudPersistida& s)
{
    SolicitudSat r;
    r.tipo = s.tipoCfdi;
    r.rfcSolicitante = s.rfcSolicitante;
    r.fechaInicial = s.fechaInicialSat;
    r.fechaFinal = s.fechaFinalSat;
    if (s.tipoCfdi == TipoDescarga::Emitidos) {
        r.contrapartes = s.rfcReceptores;
    } else if (s.rfcEmisor) {
        r.contrapartes = {*s.rfcEmisor};
    }
    r.tipoComprobante = s.tipoComprobante;
    r.complemento = s.complemento;
    return r;
}

Cancelacion cancelacionDe(const SenalCancelacion& senal)
{
    return Cancelacion([senal] { return senal.solicitada(); });
}

// Receptor del ZIP: lo guarda en su destino final (T008).
class ReceptorAlmacenamiento final : public ReceptorPaqueteSat {
public:
    ReceptorAlmacenamiento(PackageStorage& almacenamiento, UbicacionPaquete ubicacion, Cancelacion cancelacion)
        : m_almacenamiento(almacenamiento)
        , m_ubicacion(std::move(ubicacion))
        , m_cancelacion(std::move(cancelacion))
    {
    }

    void recibir(FuenteZipPorChunks& paquete) override
    {
        resultado.emplace(m_almacenamiento.guardarAtomico(m_ubicacion, paquete, m_cancelacion));
    }

    std::optional<Resultado<ArchivoFinal, ErrorGuardarZip>> resultado;

private:
    PackageStorage& m_almacenamiento;
    UbicacionPaquete m_ubicacion;
    Cancelacion m_cancelacion;
};

} // namespace

// --- Sesion ------------------------------------------------------------------

struct OperacionesSatProductivo::Preparada {
    MaterialFirma material;
    std::shared_ptr<const TokenSat> token;
};

OperacionesSatProductivo::OperacionesSatProductivo(SatGateway& gateway, AccesoCredencialSat& credenciales,
                                                   PackageStorage& almacenamiento, RelojUtc reloj,
                                                   OpcionesOperacionesSat opciones)
    : m_gateway(gateway)
    , m_credenciales(credenciales)
    , m_almacenamiento(almacenamiento)
    , m_reloj(reloj ? std::move(reloj) : relojSistema())
    , m_opciones(opciones)
{
}

OperacionesSatProductivo::~OperacionesSatProductivo()
{
    cerrarSesiones();
}

void OperacionesSatProductivo::invalidarSesion(const PerfilId& perfil)
{
    const QMutexLocker l(&m_mutex);
    m_tokens.remove(perfil.texto());
    ++m_generaciones[perfil.texto()];
}

void OperacionesSatProductivo::cerrarSesiones()
{
    const QMutexLocker l(&m_mutex);
    m_tokens.clear();
    ++m_epoca;
}

bool OperacionesSatProductivo::tieneSesion(const PerfilId& perfil) const
{
    const QMutexLocker l(&m_mutex);
    return m_tokens.contains(perfil.texto());
}

void OperacionesSatProductivo::descartarSiIgual(const PerfilId& perfil, const std::shared_ptr<const TokenSat>& token)
{
    const QMutexLocker l(&m_mutex);
    const auto it = m_tokens.constFind(perfil.texto());
    if (it != m_tokens.cend() && it.value() == token) {
        m_tokens.erase(it);
    }
}

// Estado (Lista) -> material -> token de la sesion o Autentica (D5, D6).
Resultado<OperacionesSatProductivo::Preparada, FallaOperacion>
OperacionesSatProductivo::preparar(const PerfilId& perfil, const SenalCancelacion& cancelacion)
{
    using R = Resultado<Preparada, FallaOperacion>;
    if (cancelacion.solicitada()) {
        return R::fallo(canceladaAntesDeRed());
    }
    auto estado = m_credenciales.estadoEnHiloDeTrabajo(perfil);
    if (!estado) {
        return R::fallo(fallaCredencial(estado.error()));
    }
    if (estado.valor() != EstadoCredencial::Lista) {
        return R::fallo(fallaEstadoCredencial(estado.valor()));
    }
    auto material = m_credenciales.materialEnHiloDeTrabajo(perfil);
    if (!material) {
        return R::fallo(fallaCredencial(material.error()));
    }

    const QString clave = perfil.texto();
    std::uint64_t generacion = 0;
    std::uint64_t epoca = 0;
    {
        const QMutexLocker l(&m_mutex);
        generacion = m_generaciones.value(clave);
        epoca = m_epoca;
        const auto it = m_tokens.constFind(clave);
        if (it != m_tokens.cend()) {
            const QDateTime limite = m_reloj().addSecs(qint64(m_opciones.margenToken.count()));
            if (it.value()->vigenteEn(limite)) {
                return R::exito(Preparada{std::move(material).valor(), it.value()});
            }
            m_tokens.erase(it); // expirado (o por expirar dentro del margen)
        }
    }
    if (cancelacion.solicitada()) {
        return R::fallo(canceladaAntesDeRed());
    }
    auto token = m_gateway.autenticar(material.valor(), cancelacionDe(cancelacion));
    if (!token) {
        invalidarSesion(perfil);
        return R::fallo(fallaAutenticacion(token.error()));
    }
    if (token.valor().vacio()) {
        invalidarSesion(perfil);
        ErrorSatGateway vacio;
        vacio.fase = FaseSatGateway::RespuestaExplicita;
        vacio.diagnosticoSanitizado = QStringLiteral("token vacio");
        return R::fallo(fallaAutenticacion(vacio));
    }
    auto compartido = std::make_shared<const TokenSat>(std::move(token).valor());
    {
        // Si la sesion se invalido mientras se autenticaba (cambio de
        // credencial o cierre), el token se usa solo en esta operacion.
        const QMutexLocker l(&m_mutex);
        if (m_generaciones.value(clave) == generacion && m_epoca == epoca) {
            m_tokens.insert(clave, compartido);
        }
    }
    return R::exito(Preparada{std::move(material).valor(), std::move(compartido)});
}

// --- Operaciones ---------------------------------------------------------------

Resultado<ResultadoEnvio, FallaOperacion> OperacionesSatProductivo::enviar(const ContextoEnvio& contexto)
{
    using R = Resultado<ResultadoEnvio, FallaOperacion>;
    const SolicitudPersistida& s = contexto.solicitud;
    auto preparada = preparar(s.perfilSatId, contexto.cancelacion);
    if (!preparada) {
        return R::fallo(std::move(preparada).error());
    }
    const Preparada& p = preparada.valor();
    auto r = m_gateway.crearSolicitud(*p.token, solicitudSatDe(s), p.material, cancelacionDe(contexto.cancelacion));
    if (!r) {
        if (r.error().tokenRechazado) {
            descartarSiIgual(s.perfilSatId, p.token);
        }
        return R::fallo(fallaEnvio(r.error()));
    }
    const RespuestaCreacion& c = r.valor();
    std::optional<QString> id = c.idSolicitud;
    if (id && id->trimmed().isEmpty()) {
        id.reset();
    }
    if (id && saneamiento::codigoSeguro(QStringView(*id)) != id->trimmed()) {
        id.reset(); // IdSolicitud no persistible: no se acepta (EnvioIncierto)
    }
    return R::exito(ResultadoEnvio{saneamiento::codigoSeguro(QStringView(c.codEstatus)), mensajeCreacion(c), id});
}

Resultado<ResultadoVerificacion, FallaOperacion>
OperacionesSatProductivo::verificar(const ContextoVerificacion& contexto)
{
    using R = Resultado<ResultadoVerificacion, FallaOperacion>;
    auto preparada = preparar(contexto.perfilSatId, contexto.cancelacion);
    if (!preparada) {
        return R::fallo(std::move(preparada).error());
    }
    const Preparada& p = preparada.valor();
    const ConsultaSolicitudSat consulta{contexto.idSolicitudSat, contexto.rfcSolicitante};
    auto r = m_gateway.verificarSolicitud(*p.token, consulta, p.material, cancelacionDe(contexto.cancelacion));
    if (!r) {
        if (r.error().tokenRechazado) {
            descartarSiIgual(contexto.perfilSatId, p.token);
        }
        return R::fallo(fallaVerificacion(r.error()));
    }
    const RespuestaVerificacion& v = r.valor();
    const QString codigo = saneamiento::codigoSeguro(QStringView(v.codEstatus));
    if (codigo != kCodigoExito) {
        return R::fallo(fallaCodigoVerificacion(codigo));
    }
    const std::optional<EstadoSolicitudSat> estado =
        v.estadoSolicitud ? estadoSatDesdeEntero(*v.estadoSolicitud) : std::nullopt;
    if (!estado) {
        return R::fallo(falla(FaseOperacion::RespuestaExplicita, codigo, m::verificacionTransitoria(),
                              QStringLiteral("EstadoSolicitud ausente o fuera de rango")));
    }
    ResultadoVerificacion resultado;
    resultado.codEstatus = codigo;
    resultado.mensaje = m::verificacionAceptada();
    resultado.estadoSolicitudSat = *estado;
    resultado.codigoEstadoSolicitud = saneamiento::codigoSeguro(v.codigoEstadoSolicitud);
    resultado.numeroCfdi = v.numeroCfdi;
    resultado.idsPaquetes = v.idsPaquete;
    return R::exito(resultado);
}

Resultado<ResultadoDescarga, FallaOperacion> OperacionesSatProductivo::descargar(const ContextoDescarga& contexto)
{
    using R = Resultado<ResultadoDescarga, FallaOperacion>;
    // La ubicacion se valida antes de la red (sin fecha o RFC no hay destino).
    const UbicacionPaquete ubicacion{contexto.rfcSolicitante, contexto.fechaInicialSat, contexto.solicitudId.texto(),
                                     contexto.idPaqueteSat};
    if (auto ruta = PackageStorage::derivarRutaRelativa(ubicacion); !ruta) {
        return R::fallo(falla(FaseOperacion::Preparacion, QStringLiteral("ubicacion_invalida"), m::descargaFallida(),
                              claveEstable(ruta.error())));
    }
    auto preparada = preparar(contexto.perfilSatId, contexto.cancelacion);
    if (!preparada) {
        return R::fallo(std::move(preparada).error());
    }
    const Preparada& p = preparada.valor();
    const Cancelacion cancelacion = cancelacionDe(contexto.cancelacion);
    ReceptorAlmacenamiento receptor(m_almacenamiento, ubicacion, cancelacion);
    const ConsultaPaqueteSat consulta{contexto.idPaqueteSat, contexto.rfcSolicitante};
    auto r = m_gateway.descargarPaquete(*p.token, consulta, p.material, receptor, cancelacion);
    if (!r && r.error().tokenRechazado) {
        descartarSiIgual(contexto.perfilSatId, p.token);
    }
    // El receptor solo se invoca con 5000: si el archivo final quedo
    // promovido, la descarga se confirma aunque el gateway reporte despues.
    if (receptor.resultado) {
        const auto& guardado = *receptor.resultado;
        if (!guardado) {
            return R::fallo(fallaAlmacenamiento(guardado.error()));
        }
        ResultadoDescarga d;
        d.rutaFinal = guardado.valor().rutaRelativa;
        d.codEstatus = kCodigoExito;
        d.mensaje = m::paqueteDescargado();
        if (guardado.valor().advertenciaDurabilidad) {
            d.advertenciaDurabilidad = QStringLiteral("fsync del directorio no confirmado");
        }
        return R::exito(d);
    }
    if (!r) {
        return R::fallo(fallaDescarga(r.error()));
    }
    const QString codigo = saneamiento::codigoSeguro(QStringView(r.valor().codEstatus));
    if (codigo == QStringLiteral("5007")) {
        return R::fallo(falla(FaseOperacion::RespuestaExplicita, codigo, m::paqueteVencido()));
    }
    if (codigo == QStringLiteral("5008")) {
        return R::fallo(falla(FaseOperacion::RespuestaExplicita, codigo, m::paqueteMaximoDescargas()));
    }
    if (codigo == kCodigoExito) {
        return R::fallo(falla(FaseOperacion::RespuestaExplicita, codigo, m::descargaFallida(),
                              QStringLiteral("paquete no entregado")));
    }
    return R::fallo(falla(FaseOperacion::RespuestaExplicita, codigo, m::descargaFallida()));
}

Resultado<ResultadoArchivoFinal, FallaOperacion>
OperacionesSatProductivo::existeArchivoFinal(const ContextoArchivoFinal& contexto)
{
    using R = Resultado<ResultadoArchivoFinal, FallaOperacion>;
    const UbicacionPaquete ubicacion{contexto.rfcSolicitante, contexto.fechaInicialSat, contexto.solicitudId.texto(),
                                     contexto.idPaqueteSat};
    auto ruta = PackageStorage::derivarRutaRelativa(ubicacion);
    if (!ruta) {
        return R::fallo(falla(FaseOperacion::Preparacion, QStringLiteral("ubicacion_invalida"), m::descargaFallida(),
                              claveEstable(ruta.error())));
    }
    auto existe = m_almacenamiento.existeArchivoFinal(ruta.valor());
    if (!existe) {
        FallaOperacion f = falla(FaseOperacion::Almacenamiento, std::nullopt, m::sinPermisoEscritura(),
                                 claveEstable(existe.error()));
        f.causaAlmacenamiento = CausaAlmacenamiento::EscrituraFallida;
        return R::fallo(f);
    }
    ResultadoArchivoFinal a;
    a.existe = existe.valor();
    if (a.existe) {
        a.rutaFinal = ruta.valor();
    }
    return R::exito(a);
}

Resultado<EstadoCredencial, FallaOperacion> OperacionesSatProductivo::obtenerEstadoCredencial(const PerfilId& perfil)
{
    using R = Resultado<EstadoCredencial, FallaOperacion>;
    auto estado = m_credenciales.estadoEnHiloDeTrabajo(perfil);
    if (!estado) {
        return R::fallo(fallaCredencial(estado.error()));
    }
    return R::exito(estado.valor());
}

} // namespace satcfdi
