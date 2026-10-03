#include "NuevaSolicitudViewModel.h"

#include "application/profiles/PerfilesSatService.h"
#include "application/requests/SolicitudesService.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/EstadosSolicitud.h"

#include <QDate>
#include <QFuture>

#include <utility>

namespace satcfdi {

namespace {

QDate fechaDesdeTexto(const QString& texto)
{
    return QDate::fromString(texto.trimmed(), Qt::ISODate);
}

QString textoDeValidacion(ErrorValidacion::Codigo codigo)
{
    using C = ErrorValidacion::Codigo;
    switch (codigo) {
    case C::PerfilRequerido:
        return QObject::tr("Selecciona un perfil SAT.");
    case C::PerfilInexistente:
        return QObject::tr("El perfil SAT seleccionado ya no esta disponible.");
    case C::FechaInicialRequerida:
        return QObject::tr("Indica una fecha inicial valida (AAAA-MM-DD).");
    case C::FechaFinalRequerida:
        return QObject::tr("Indica una fecha final valida (AAAA-MM-DD).");
    case C::RangoFechasInvalido:
        return QObject::tr("La fecha final debe ser igual o posterior a la fecha inicial.");
    }
    return QObject::tr("La solicitud no es valida.");
}

} // namespace

NuevaSolicitudViewModel::NuevaSolicitudViewModel(SolicitudesService* solicitudes,
                                                 PerfilesSatService* perfiles,
                                                 QObject* parent)
    : QObject(parent)
    , m_solicitudes(solicitudes)
    , m_perfiles(perfiles)
    , m_perfilesDisponibles(new PerfilesDisponiblesModel(this))
{
    Q_ASSERT(solicitudes != nullptr);
    Q_ASSERT(perfiles != nullptr);
    reiniciar();
}

QString NuevaSolicitudViewModel::errorMessage() const
{
    if (!m_errorServicio.isEmpty()) {
        return m_errorServicio;
    }
    return m_tocado ? m_errorValidacion : QString();
}

template <typename F>
void NuevaSolicitudViewModel::actualizar(F&& cambio)
{
    const bool podiaEnviar = canSubmit();
    const QString errorPrevio = errorMessage();
    std::forward<F>(cambio)();
    m_errorValidacion = validar();
    if (podiaEnviar != canSubmit()) {
        emit canSubmitChanged();
    }
    if (errorPrevio != errorMessage()) {
        emit errorMessageChanged();
    }
}

QString NuevaSolicitudViewModel::validar() const
{
    using C = ErrorValidacion::Codigo;
    if (m_perfilId.isEmpty()) {
        return textoDeValidacion(C::PerfilRequerido);
    }
    if (!m_perfilesDisponibles->contiene(m_perfilId)) {
        return textoDeValidacion(C::PerfilInexistente);
    }
    const QDate inicial = fechaDesdeTexto(m_fechaInicial);
    if (!inicial.isValid()) {
        return textoDeValidacion(C::FechaInicialRequerida);
    }
    const QDate final_ = fechaDesdeTexto(m_fechaFinal);
    if (!final_.isValid()) {
        return textoDeValidacion(C::FechaFinalRequerida);
    }
    if (final_ < inicial) {
        return textoDeValidacion(C::RangoFechasInvalido);
    }
    return {};
}

void NuevaSolicitudViewModel::setPerfilId(const QString& valor)
{
    if (m_perfilId == valor) {
        return;
    }
    actualizar([&] {
        m_perfilId = valor;
        m_tocado = true;
        m_errorServicio.clear();
    });
    emit perfilIdChanged();
}

void NuevaSolicitudViewModel::setTipoDescarga(const QString& valor)
{
    if (m_tipoDescarga == valor || !tipoDescargaDesdeClave(valor)) {
        return;
    }
    actualizar([&] {
        m_tipoDescarga = valor;
        m_errorServicio.clear();
    });
    emit tipoDescargaChanged();
}

void NuevaSolicitudViewModel::setFechaInicial(const QString& valor)
{
    if (m_fechaInicial == valor) {
        return;
    }
    actualizar([&] {
        m_fechaInicial = valor;
        m_tocado = true;
        m_errorServicio.clear();
    });
    emit fechaInicialChanged();
}

void NuevaSolicitudViewModel::setFechaFinal(const QString& valor)
{
    if (m_fechaFinal == valor) {
        return;
    }
    actualizar([&] {
        m_fechaFinal = valor;
        m_tocado = true;
        m_errorServicio.clear();
    });
    emit fechaFinalChanged();
}

void NuevaSolicitudViewModel::setRfcContraparte(const QString& valor)
{
    if (m_rfcContraparte == valor) {
        return;
    }
    actualizar([&] {
        m_rfcContraparte = valor;
        m_errorServicio.clear();
    });
    emit rfcContraparteChanged();
}

void NuevaSolicitudViewModel::submit()
{
    if (m_ocupado || !m_solicitudes) {
        return;
    }
    actualizar([&] { m_tocado = true; });
    if (!m_errorValidacion.isEmpty()) {
        return; // error visible; no se llama al servicio
    }

    NuevaSolicitudRequest request;
    request.perfilId = PerfilId::desdeTexto(m_perfilId).value_or(PerfilId());
    request.tipoDescarga = tipoDescargaDesdeClave(m_tipoDescarga).value_or(TipoDescarga::Emitidos);
    request.fechaInicial = fechaDesdeTexto(m_fechaInicial);
    request.fechaFinal = fechaDesdeTexto(m_fechaFinal);
    const QString rfc = m_rfcContraparte.trimmed().toUpper();
    if (!rfc.isEmpty()) {
        request.rfcContraparte = rfc;
    }

    setOcupado(true);
    m_solicitudes->crear(request).then(this, [this](SolicitudesService::ResultadoCrear r) {
        setOcupado(false);
        if (r.esExito()) {
            emit submitted(r.valor().texto());
            return;
        }
        const ErrorCrear& error = r.error();
        actualizar([&] {
            if (error.tipo == ErrorCrear::Tipo::Validacion && !error.validaciones.isEmpty()) {
                m_errorServicio = textoDeValidacion(error.validaciones.constFirst().codigo);
            } else {
                m_errorServicio = tr("No se pudo guardar la solicitud. Intenta de nuevo.");
            }
        });
    });
}

void NuevaSolicitudViewModel::reiniciar()
{
    const QDate hoy = QDate::currentDate();
    const QString inicial = QDate(hoy.year(), hoy.month(), 1).toString(Qt::ISODate);
    const QString final_ = hoy.toString(Qt::ISODate);
    const QString tipo = claveEstable(TipoDescarga::Emitidos);

    const bool cambiaPerfil = !m_perfilId.isEmpty();
    const bool cambiaTipo = m_tipoDescarga != tipo;
    const bool cambiaInicial = m_fechaInicial != inicial;
    const bool cambiaFinal = m_fechaFinal != final_;
    const bool cambiaRfc = !m_rfcContraparte.isEmpty();

    actualizar([&] {
        m_perfilId.clear();
        m_tipoDescarga = tipo;
        m_fechaInicial = inicial;
        m_fechaFinal = final_;
        m_rfcContraparte.clear();
        m_tocado = false;
        m_errorServicio.clear();
    });
    if (cambiaPerfil) {
        emit perfilIdChanged();
    }
    if (cambiaTipo) {
        emit tipoDescargaChanged();
    }
    if (cambiaInicial) {
        emit fechaInicialChanged();
    }
    if (cambiaFinal) {
        emit fechaFinalChanged();
    }
    if (cambiaRfc) {
        emit rfcContraparteChanged();
    }
    cargarPerfiles();
}

void NuevaSolicitudViewModel::cargarPerfiles()
{
    if (!m_perfiles) {
        return;
    }
    m_perfiles->listarActivos().then(this, [this](PerfilesSatService::ResultadoLista r) {
        actualizar([&] {
            if (r.esExito()) {
                m_perfilesDisponibles->reemplazar(std::move(r).valor());
            } else {
                m_perfilesDisponibles->reemplazar({});
                m_errorServicio = tr("No se pudieron cargar los perfiles SAT.");
            }
        });
    });
}

void NuevaSolicitudViewModel::setOcupado(bool valor)
{
    if (m_ocupado == valor) {
        return;
    }
    actualizar([&] { m_ocupado = valor; });
    emit ocupadoChanged();
}

} // namespace satcfdi
