#include "app_core/RespaldoPresentacionRoot.h"

#include "app_core/AppCompositionRoot.h"
#include "app_core/AreaRestauracionLocal.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

namespace satcfdi {

using respaldo::ResultadoRespaldo;

namespace {

// Recorre los enumeradores [0, ultimo] y compara su clave estable.
template <typename E>
std::optional<E> desdeClave(const QString& clave, E ultimo)
{
    if (clave.isEmpty()) {
        return std::nullopt;
    }
    for (int i = 0; i <= static_cast<int>(ultimo); ++i) {
        const E e = static_cast<E>(i);
        if (respaldo::claveEstable(e) == clave) {
            return e;
        }
    }
    return std::nullopt;
}

} // namespace

std::optional<ResultadoRespaldo::Estado> estadoRespaldoDesdeClave(const QString& clave)
{
    return desdeClave(clave, ResultadoRespaldo::Estado::ErrorPublicacion);
}

std::optional<respaldo::ErrorDestino::Categoria> errorDestinoDesdeClave(const QString& clave)
{
    return desdeClave(clave, respaldo::ErrorDestino::Categoria::Interno);
}

std::optional<respaldo::ErrorCaptura::Categoria> errorCapturaDesdeClave(const QString& clave)
{
    return desdeClave(clave, respaldo::ErrorCaptura::Categoria::Interno);
}

std::optional<respaldo::ErrorRespaldo::Categoria> errorContenedorDesdeClave(const QString& clave)
{
    return desdeClave(clave, respaldo::ErrorRespaldo::Categoria::Interno);
}

std::optional<ResultadoRespaldoPresentacion> resultadoDesdeResumen(const respaldo::ResumenUltimoRespaldo& r)
{
    const auto estado = estadoRespaldoDesdeClave(r.estado);
    if (!estado) {
        return std::nullopt;
    }
    ResultadoRespaldoPresentacion p;
    p.estado = *estado;
    p.errorDestino = errorDestinoDesdeClave(r.errorDestino);
    p.errorCaptura = errorCapturaDesdeClave(r.errorCaptura);
    p.errorContenedor = errorContenedorDesdeClave(r.errorContenedor);
    p.fechaUtc = r.fechaUtc;
    p.bytes = r.bytesDatos;
    p.faltantes = r.faltantes;
    p.omitidos = r.omitidos;
    p.depurados = r.depurados;
    p.copiasSinRegistro = r.copiasSinRegistro;
    p.avisoDepuracion = r.avisoDepuracion;
    p.avisoRegistro = r.avisoRegistro;
    p.avisoPersistencia = r.avisoPersistencia; // Q1: solo en memoria
    return p;
}

// --- Respaldo ------------------------------------------------------------------

ControlRespaldoGestor::ControlRespaldoGestor(respaldo::GestorRespaldo& gestor, bool disponible, QObject* parent)
    : ControlRespaldoPresentacion(parent), m_gestor(gestor), m_disponible(disponible)
{
    qRegisterMetaType<satcfdi::ResultadoRespaldoPresentacion>();
    QObject::connect(&m_gestor, &respaldo::GestorRespaldo::estadoCambiado, this,
                     &ControlRespaldoPresentacion::estadoCambiado);
    QObject::connect(&m_gestor, &respaldo::GestorRespaldo::progreso, this, &ControlRespaldoPresentacion::progreso);
    // C6: cada respaldo terminado (manual o diario); el gestor no emite con
    // YaEnCurso ni Detenido.
    QObject::connect(&m_gestor, &respaldo::GestorRespaldo::respaldoTerminado, this,
                     [this](const respaldo::ResumenUltimoRespaldo& r) {
                         if (const auto p = resultadoDesdeResumen(r)) {
                             emit respaldoTerminado(*p);
                         }
                     });
}

EstadoRespaldoPresentacion ControlRespaldoGestor::estado() const
{
    using P = EstadoRespaldoPresentacion::PendienteDiario;
    const respaldo::EstadoRespaldoGestor g = m_gestor.estado();
    EstadoRespaldoPresentacion e;
    e.disponible = m_disponible;
    e.cargado = g.cargado;
    e.carpetaConfigurada = g.carpetaConfigurada;
    // Solo el ultimo componente: nunca la ruta absoluta.
    e.nombreCarpeta = g.rutaCarpeta.isEmpty() ? QString() : QFileInfo(QDir::cleanPath(g.rutaCarpeta)).fileName();
    e.contrasenaConfigurada = g.contrasenaConfigurada;
    e.diarioActivado = g.diarioActivado;
    switch (g.pendienteDiario) {
    case respaldo::EstadoRespaldoGestor::PendienteDiario::Ninguno: e.pendienteDiario = P::Ninguno; break;
    case respaldo::EstadoRespaldoGestor::PendienteDiario::FaltaCarpeta: e.pendienteDiario = P::FaltaCarpeta; break;
    case respaldo::EstadoRespaldoGestor::PendienteDiario::FaltaContrasena: e.pendienteDiario = P::FaltaContrasena; break;
    }
    e.enCurso = g.enCurso;
    e.ultimoExitoUtc = g.ultimoExitoUtc;
    e.ultimoIntentoUtc = g.ultimoIntentoUtc;
    return e;
}

std::optional<ResultadoRespaldoPresentacion> ControlRespaldoGestor::ultimoResultado() const
{
    const auto resumen = m_gestor.estado().ultimoResumen;
    return resumen ? resultadoDesdeResumen(*resumen) : std::nullopt;
}

QUrl ControlRespaldoGestor::urlCarpeta() const
{
    const respaldo::EstadoRespaldoGestor g = m_gestor.estado();
    return g.carpetaConfigurada && !g.rutaCarpeta.isEmpty() ? QUrl::fromLocalFile(g.rutaCarpeta) : QUrl();
}

QFuture<ControlRespaldoPresentacion::ResultadoOperacion> ControlRespaldoGestor::configurarCarpeta(const QString& rutaLocal)
{
    return m_gestor.configurarCarpeta(rutaLocal);
}

QFuture<ControlRespaldoPresentacion::ResultadoClave>
ControlRespaldoGestor::configurarContrasena(std::shared_ptr<BufferSecreto> contrasena)
{
    return m_gestor.configurarContrasena(std::move(contrasena));
}

QFuture<ControlRespaldoPresentacion::ResultadoOperacion>
ControlRespaldoGestor::cambiarContrasena(std::shared_ptr<BufferSecreto> nueva, respaldo::VerificacionCambio verificacion)
{
    return m_gestor.cambiarContrasena(std::move(nueva), std::move(verificacion));
}

QFuture<ControlRespaldoPresentacion::ResultadoOperacion> ControlRespaldoGestor::activarDiario(bool activado)
{
    return m_gestor.activarDiario(activado);
}

QFuture<ResultadoRespaldoPresentacion> ControlRespaldoGestor::respaldarAhora()
{
    respaldo::GestorRespaldo* gestor = &m_gestor;
    return m_gestor.respaldarAhora().then(this, [gestor](ResultadoRespaldo r) {
        // La fecha es la del resumen persistido si ya corresponde a este
        // respaldo (el espejo se publica antes de resolver); si no (YaEnCurso,
        // Detenido), la hora actual.
        QDateTime fecha = QDateTime::currentDateTimeUtc();
        const auto resumen = gestor->estado().ultimoResumen;
        if (resumen && resumen->estado == respaldo::claveEstable(r.estado) && r.estado != ResultadoRespaldo::Estado::YaEnCurso
            && r.estado != ResultadoRespaldo::Estado::Detenido) {
            fecha = resumen->fechaUtc;
        }
        return ResultadoRespaldoPresentacion::desde(r, fecha);
    });
}

// --- Restauracion ----------------------------------------------------------------

ControlRestauracionServicio::ControlRestauracionServicio(respaldo::RestauracionService& servicio, QObject* parent)
    : ControlRestauracionPresentacion(parent), m_servicio(servicio)
{
    QObject::connect(&m_servicio, &respaldo::RestauracionService::progreso, this,
                     &ControlRestauracionPresentacion::progreso);
}

QFuture<ControlRestauracionPresentacion::ResultadoPreparacion>
ControlRestauracionServicio::preparar(const QString& rutaArchivo, respaldo::PeticionRestauracion::Credencial credencial,
                                      std::shared_ptr<BufferSecreto> secreto)
{
    respaldo::PeticionRestauracion p;
    p.origen = std::make_shared<OrigenRestauracionArchivo>(rutaArchivo);
    p.credencial = credencial;
    p.secreto = std::move(secreto);
    return m_servicio.preparar(std::move(p));
}

void ControlRestauracionServicio::cancelar()
{
    m_servicio.cancelar();
}

QFuture<void> ControlRestauracionServicio::descartar()
{
    return m_servicio.descartar();
}

QFuture<respaldo::ResultadoSolicitudRestauracion> ControlRestauracionServicio::solicitar()
{
    return m_servicio.solicitar();
}

bool ControlRestauracionServicio::enCurso() const
{
    return m_servicio.enCurso();
}

bool ControlRestauracionServicio::preparada() const
{
    return m_servicio.preparada();
}

// --- Datos previos -------------------------------------------------------------------

QFuture<bool> DatosPreviosRestauracionRoot::hayDatosPrevios()
{
    return m_root.hayDatosPrevios();
}

QFuture<ResultadoDescartePresentacion> DatosPreviosRestauracionRoot::descartarDatosPrevios()
{
    return m_root.descartarDatosPrevios().then([](ResultadoDescarteDatosPrevios r) {
        using E = ResultadoDescarteDatosPrevios::Estado;
        using P = ResultadoDescartePresentacion::Estado;
        ResultadoDescartePresentacion p;
        switch (r.estado) {
        case E::Descartados: p.estado = P::Descartados; break;
        case E::SinDatosPrevios: p.estado = P::SinDatosPrevios; break;
        case E::Incompleto: p.estado = P::Incompleto; break;
        case E::ErrorReconciliacion: p.estado = P::ErrorReconciliacion; break;
        }
        p.carpetasBorradas = r.carpetasBorradas;
        p.carpetasAjenas = r.carpetasAjenas;
        return p;
    });
}

ResultadoArranquePresentacion DatosPreviosRestauracionRoot::restauracionAlArrancar() const
{
    using E = ResultadoRestauracionArranque::Estado;
    using P = ResultadoArranquePresentacion::Estado;
    ResultadoArranquePresentacion p;
    const auto& r = m_root.restauracionAlArrancar();
    if (!r) {
        return p;
    }
    switch (r->estado) {
    case E::Aplicada:
        p.estado = P::Aplicada;
        p.idRespaldo = r->idRespaldo;
        p.creadoRespaldoUtc = r->creadoRespaldoUtc;
        break;
    case E::Descartada: p.estado = P::Descartada; break;
    case E::Revertida: p.estado = P::Revertida; break;
    case E::SinMarcador:
    case E::Error:        // nunca llega: la app no arranca
    case E::Interrumpida: // solo pruebas
        p.estado = P::Ninguno;
        break;
    }
    return p;
}

} // namespace satcfdi
