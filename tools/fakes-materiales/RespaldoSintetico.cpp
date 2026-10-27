#include "RespaldoSintetico.h"

#include <QTimeZone>

namespace materiales {

namespace {

template <typename T>
QFuture<T> pendiente(std::vector<std::unique_ptr<QPromise<T>>>& lista)
{
    lista.push_back(std::make_unique<QPromise<T>>());
    lista.back()->start();
    return lista.back()->future();
}

template <typename T>
void resolverUltima(std::vector<std::unique_ptr<QPromise<T>>>& lista, const T& valor)
{
    if (lista.empty() || lista.back()->future().isFinished()) {
        return;
    }
    lista.back()->addResult(valor);
    lista.back()->finish();
}

} // namespace

// ---- Respaldo -----------------------------------------------------------------

ControlRespaldoSintetico::~ControlRespaldoSintetico() = default;

QUrl ControlRespaldoSintetico::urlCarpeta() const
{
    return estadoActual.carpetaConfigurada ? QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Respaldos")) : QUrl();
}

EstadoRespaldoPresentacion ControlRespaldoSintetico::configurado()
{
    EstadoRespaldoPresentacion e;
    e.cargado = true;
    e.carpetaConfigurada = true;
    e.nombreCarpeta = QStringLiteral("Respaldos");
    e.contrasenaConfigurada = true;
    e.diarioActivado = true;
    e.ultimoExitoUtc = QDateTime(QDate(2026, 10, 14), QTime(9, 0), QTimeZone::UTC);
    e.ultimoIntentoUtc = e.ultimoExitoUtc;
    return e;
}

ResultadoRespaldoPresentacion ControlRespaldoSintetico::exitoDeEjemplo()
{
    ResultadoRespaldoPresentacion r;
    r.estado = respaldo::ResultadoRespaldo::Estado::Exito;
    r.fechaUtc = QDateTime(QDate(2026, 10, 14), QTime(9, 0), QTimeZone::UTC);
    r.bytes = 48'318'976;
    return r;
}

QString ControlRespaldoSintetico::claveRecuperacionSintetica()
{
    return QStringLiteral("ABCD-EFGH-JKMN-PQRS-TUVW-XYZ2-3456-789A");
}

QFuture<ControlRespaldoSintetico::ResultadoOperacion> ControlRespaldoSintetico::configurarCarpeta(const QString& rutaLocal)
{
    estadoActual.carpetaConfigurada = true;
    // T024.4: el nombre visible es el de la carpeta elegida (como la app).
    const QString nombre = rutaLocal.section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
    estadoActual.nombreCarpeta = nombre.isEmpty() ? QStringLiteral("Respaldos") : nombre;
    emit estadoCambiado();
    return QtFuture::makeReadyValueFuture(ResultadoOperacion::exito({}));
}

QFuture<ControlRespaldoSintetico::ResultadoClave>
ControlRespaldoSintetico::configurarContrasena(std::shared_ptr<BufferSecreto>)
{
    estadoActual.contrasenaConfigurada = true;
    emit estadoCambiado();
    return QtFuture::makeReadyValueFuture(ResultadoClave::exito(
        std::make_shared<BufferSecreto>(BufferSecreto::desdeTexto(claveRecuperacionSintetica()))));
}

QFuture<ControlRespaldoSintetico::ResultadoOperacion>
ControlRespaldoSintetico::cambiarContrasena(std::shared_ptr<BufferSecreto>, respaldo::VerificacionCambio)
{
    return QtFuture::makeReadyValueFuture(ResultadoOperacion::exito({}));
}

QFuture<ControlRespaldoSintetico::ResultadoOperacion> ControlRespaldoSintetico::activarDiario(bool activado)
{
    estadoActual.diarioActivado = activado;
    emit estadoCambiado();
    return QtFuture::makeReadyValueFuture(ResultadoOperacion::exito({}));
}

QFuture<ResultadoRespaldoPresentacion> ControlRespaldoSintetico::respaldarAhora()
{
    if (resultadoRespaldo) {
        const ResultadoRespaldoPresentacion r = *resultadoRespaldo;
        ultimo = r;
        emit respaldoTerminado(r);
        return QtFuture::makeReadyValueFuture(r);
    }
    estadoActual.enCurso = true;
    emit estadoCambiado();
    return pendiente(m_respaldos);
}

void ControlRespaldoSintetico::fijarEstado(const EstadoRespaldoPresentacion& e)
{
    estadoActual = e;
    emit estadoCambiado();
}

void ControlRespaldoSintetico::emitirProgreso(respaldo::RespaldoService::Fase fase, quint64 bytes, quint64 total)
{
    emit progreso(fase, bytes, total);
}

void ControlRespaldoSintetico::terminarRespaldo(const ResultadoRespaldoPresentacion& r)
{
    ultimo = r;
    estadoActual.enCurso = false;
    if (r.exito()) {
        estadoActual.ultimoExitoUtc = r.fechaUtc;
    }
    estadoActual.ultimoIntentoUtc = r.fechaUtc;
    resolverUltima(m_respaldos, r);
    emit estadoCambiado();
    emit respaldoTerminado(r);
}

// ---- Restauracion ---------------------------------------------------------------

ControlRestauracionSintetico::~ControlRestauracionSintetico() = default;

respaldo::ResumenRestauracion ControlRestauracionSintetico::resumenDeEjemplo()
{
    respaldo::ResumenRestauracion r;
    r.idRespaldo = QByteArray::fromHex("00112233445566778899aabbccddeeff");
    r.creadoEnUtc = QDateTime(QDate(2026, 10, 14), QTime(9, 0), QTimeZone::UTC);
    r.versionAppRespaldo = QStringLiteral("1.0.0");
    r.perfiles = 2;
    r.solicitudes = 18;
    r.paquetes = 18;
    return r;
}

QFuture<ControlRestauracionSintetico::ResultadoPreparacion>
ControlRestauracionSintetico::preparar(const QString&, respaldo::PeticionRestauracion::Credencial,
                                       std::shared_ptr<BufferSecreto>)
{
    if (resultadoPreparacion) {
        m_preparada = resultadoPreparacion->esExito();
        return QtFuture::makeReadyValueFuture(*resultadoPreparacion);
    }
    return pendiente(m_preparaciones);
}

void ControlRestauracionSintetico::cancelar()
{
    resolverUltima(m_preparaciones, ResultadoPreparacion::fallo(
                                        respaldo::ErrorRestauracion::de(respaldo::ErrorRestauracion::Categoria::Cancelado)));
}

QFuture<void> ControlRestauracionSintetico::descartar()
{
    m_preparada = false;
    return QtFuture::makeReadyVoidFuture();
}

QFuture<respaldo::ResultadoSolicitudRestauracion> ControlRestauracionSintetico::solicitar()
{
    return pendiente(m_solicitudes);
}

void ControlRestauracionSintetico::emitirProgreso(respaldo::RestauracionService::Fase fase, quint64 bytes,
                                                  quint64 total)
{
    emit progreso(fase, bytes, total);
}

void ControlRestauracionSintetico::terminarPreparacion(const ResultadoPreparacion& r)
{
    m_preparada = r.esExito();
    resolverUltima(m_preparaciones, r);
}

QFuture<ResultadoDescartePresentacion> DatosPreviosSinteticos::descartarDatosPrevios()
{
    ResultadoDescartePresentacion r;
    r.estado = hay ? ResultadoDescartePresentacion::Estado::Descartados : ResultadoDescartePresentacion::Estado::SinDatosPrevios;
    r.carpetasBorradas = hay ? 1 : 0;
    hay = false;
    return QtFuture::makeReadyValueFuture(r);
}

} // namespace materiales
