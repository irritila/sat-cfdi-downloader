#include "NuevaSolicitudViewModel.h"

#include "AccionesSolicitud.h"

#include "application/profiles/ConsultaPreparacionPerfiles.h"
#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/PerfilesSatService.h"
#include "application/requests/SolicitudesService.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/Duplicados.h"
#include "domain/solicitudes/EstadosSolicitud.h"

#include <QDate>
#include <QFuture>
#include <QStringList>

#include <utility>

namespace satcfdi {

namespace {

QDate fechaDesdeTexto(const QString& texto)
{
    return QDate::fromString(texto.trimmed(), Qt::ISODate);
}

bool tipoComprobanteValido(const QString& valor)
{
    static const QStringList validos = {QString(), QStringLiteral("I"), QStringLiteral("E"),
                                        QStringLiteral("T"), QStringLiteral("N"),
                                        QStringLiteral("P")};
    return validos.contains(valor);
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

QString textoDeMotivo(MotivoDuplicado motivo)
{
    using M = MotivoDuplicado;
    switch (motivo) {
    case M::SinCoincidencias:
        return {};
    case M::SolicitudEnCurso:
        return QObject::tr("Hay una solicitud equivalente en curso.");
    case M::TerminadaConPaquetesPendientes:
        return QObject::tr("Hay una solicitud equivalente terminada con paquetes pendientes de descargar.");
    case M::TerminadaDescargada:
        return QObject::tr("Ya existe una solicitud equivalente terminada y descargada.");
    case M::TerminadaConPaquetesVencidos:
        return QObject::tr("Existe una solicitud equivalente terminada cuyos paquetes vencieron.");
    case M::TerminadaSinPaquetes:
        return QObject::tr("Existe una solicitud equivalente terminada sin paquetes.");
    case M::EnvioIncierto:
        return QObject::tr("Existe una solicitud equivalente con envio incierto: el SAT pudo haberla recibido.");
    case M::SolicitudSinExito:
        return QObject::tr("Existe una solicitud equivalente que no tuvo exito.");
    case M::SolicitudEliminada:
        return QObject::tr("Existe una solicitud equivalente que eliminaste localmente.");
    }
    return {};
}

} // namespace

NuevaSolicitudViewModel::NuevaSolicitudViewModel(SolicitudesService* solicitudes,
                                                 PerfilesSatService* perfiles,
                                                 CredencialesSatService* credenciales,
                                                 QObject* parent)
    : QObject(parent)
    , m_solicitudes(solicitudes)
    , m_consulta(new ConsultaPreparacionPerfiles(*perfiles, *credenciales, this))
    , m_perfilesDisponibles(new PerfilesDisponiblesModel(this))
{
    Q_ASSERT(solicitudes != nullptr);
    Q_ASSERT(perfiles != nullptr);
    Q_ASSERT(credenciales != nullptr);
    // Solo se recarga si el selector ya se uso (no en el arranque).
    const auto recargar = [this] {
        if (m_perfilesCargados || m_cargandoPerfiles) {
            cargarPerfiles();
        }
    };
    connect(perfiles, &PerfilesSatService::perfilesCambiaron, this, recargar);
    connect(credenciales, &CredencialesSatService::credencialCambio, this, recargar);
    restablecerCampos();
}

bool NuevaSolicitudViewModel::sinPerfiles() const
{
    return m_perfilesCargados && m_perfilesDisponibles->count() == 0;
}

bool NuevaSolicitudViewModel::canSubmit() const
{
    return m_errorValidacion.isEmpty() && !m_ocupado && !confirmacionPendiente();
}

QString NuevaSolicitudViewModel::errorMessage() const
{
    if (!m_errorServicio.isEmpty()) {
        return m_errorServicio;
    }
    return m_tocado ? m_errorValidacion : QString();
}

NuevaSolicitudViewModel::Observables NuevaSolicitudViewModel::observables() const
{
    return {canSubmit(),        ocupado(),     errorMessage(),
            m_cargandoPerfiles, sinPerfiles(), confirmacionPendiente(),
            m_motivoDuplicado,  m_solicitudExistenteId};
}

template <typename F>
void NuevaSolicitudViewModel::actualizar(F&& cambio)
{
    const Observables antes = observables();
    std::forward<F>(cambio)();
    m_errorValidacion = validar();
    const Observables despues = observables();
    if (antes.canSubmit != despues.canSubmit) {
        emit canSubmitChanged();
    }
    if (antes.ocupado != despues.ocupado) {
        emit ocupadoChanged();
    }
    if (antes.error != despues.error) {
        emit errorMessageChanged();
    }
    if (antes.cargandoPerfiles != despues.cargandoPerfiles || antes.sinPerfiles != despues.sinPerfiles
        || antes.pendiente != despues.pendiente || antes.motivo != despues.motivo
        || antes.existente != despues.existente) {
        emit estadoChanged();
    }
}

template <typename F>
void NuevaSolicitudViewModel::editar(F&& cambio)
{
    actualizar([&] {
        std::forward<F>(cambio)();
        m_errorServicio.clear();
        m_solicitudExistenteId.clear();
        if (m_snapshot) {
            // Editar invalida una confirmacion pendiente (el snapshot ya no
            // corresponde al formulario).
            ++m_genEnvio;
            m_snapshot.reset();
            m_motivoDuplicado.clear();
        }
    });
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
    editar([&] {
        m_perfilId = valor;
        m_tocado = true;
    });
    emit perfilIdChanged();
}

void NuevaSolicitudViewModel::setTipoDescarga(const QString& valor)
{
    if (m_tipoDescarga == valor || !tipoDescargaDesdeClave(valor)) {
        return;
    }
    editar([&] { m_tipoDescarga = valor; });
    emit tipoDescargaChanged();
}

void NuevaSolicitudViewModel::setFechaInicial(const QString& valor)
{
    if (m_fechaInicial == valor) {
        return;
    }
    editar([&] {
        m_fechaInicial = valor;
        m_tocado = true;
    });
    emit fechaInicialChanged();
}

void NuevaSolicitudViewModel::setFechaFinal(const QString& valor)
{
    if (m_fechaFinal == valor) {
        return;
    }
    editar([&] {
        m_fechaFinal = valor;
        m_tocado = true;
    });
    emit fechaFinalChanged();
}

void NuevaSolicitudViewModel::setRfcContraparte(const QString& valor)
{
    if (m_rfcContraparte == valor) {
        return;
    }
    editar([&] { m_rfcContraparte = valor; });
    emit rfcContraparteChanged();
}

void NuevaSolicitudViewModel::setTipoComprobante(const QString& valor)
{
    if (m_tipoComprobante == valor || !tipoComprobanteValido(valor)) {
        return;
    }
    editar([&] { m_tipoComprobante = valor; });
    emit tipoComprobanteChanged();
}

void NuevaSolicitudViewModel::setComplemento(const QString& valor)
{
    if (m_complemento == valor) {
        return;
    }
    editar([&] { m_complemento = valor; });
    emit complementoChanged();
}

NuevaSolicitudRequest NuevaSolicitudViewModel::construirRequest() const
{
    NuevaSolicitudRequest request;
    request.perfilId = PerfilId::desdeTexto(m_perfilId).value_or(PerfilId());
    request.tipoDescarga = tipoDescargaDesdeClave(m_tipoDescarga).value_or(TipoDescarga::Emitidos);
    request.fechaInicial = fechaDesdeTexto(m_fechaInicial);
    request.fechaFinal = fechaDesdeTexto(m_fechaFinal);
    const QString rfc = m_rfcContraparte.trimmed().toUpper();
    if (!rfc.isEmpty()) {
        request.rfcContraparte = rfc;
    }
    if (!m_tipoComprobante.isEmpty()) {
        request.tipoComprobante = m_tipoComprobante;
    }
    const QString complemento = m_complemento.trimmed();
    if (!complemento.isEmpty()) {
        request.complemento = complemento;
    }
    return request;
}

void NuevaSolicitudViewModel::submit()
{
    if (m_ocupado || confirmacionPendiente() || !m_solicitudes) {
        return;
    }
    actualizar([&] {
        m_tocado = true;
        m_errorServicio.clear();
        m_solicitudExistenteId.clear();
    });
    if (!m_errorValidacion.isEmpty()) {
        return; // error visible; no se llama al servicio
    }

    const NuevaSolicitudRequest request = construirRequest(); // snapshot inmutable
    const quint64 generacion = ++m_genEnvio;
    actualizar([&] { m_ocupado = true; });

    m_solicitudes->evaluarDuplicado(request).then(
        this, [this, generacion, request](SolicitudesService::ResultadoEvaluarDuplicado r) {
            if (generacion != m_genEnvio) {
                return; // operacion invalidada
            }
            if (!r.esExito()) {
                actualizar([&] { m_ocupado = false; });
                aplicarErrorCrear(r.error(), request);
                return;
            }
            const EvaluacionDuplicado& e = r.valor();
            switch (e.clasificacion) {
            case ClasificacionDuplicado::Libre:
                crearConfirmacion(request, ConfirmacionDuplicado::SinConfirmar);
                return;
            case ClasificacionDuplicado::Bloqueado:
                actualizar([&] { m_ocupado = false; });
                aplicarErrorCrear(ErrorCrear::dedupBloqueado(e), request);
                return;
            case ClasificacionDuplicado::RequiereConfirmacion:
                actualizar([&] { m_ocupado = false; });
                aplicarErrorCrear(ErrorCrear::requiereConfirmacion(e), request);
                return;
            }
        });
}

void NuevaSolicitudViewModel::crearConfirmacion(const NuevaSolicitudRequest& request,
                                                ConfirmacionDuplicado confirmacion)
{
    if (!m_solicitudes) {
        return;
    }
    const quint64 generacion = ++m_genEnvio;
    actualizar([&] { m_ocupado = true; });
    QFuture<SolicitudesService::ResultadoCrear> futuro =
        confirmacion == ConfirmacionDuplicado::SinConfirmar
            ? m_solicitudes->crear(request)
            : m_solicitudes->crearLocal(request, confirmacion);
    futuro.then(this, [this, generacion, request](SolicitudesService::ResultadoCrear r) {
        if (generacion != m_genEnvio) {
            return;
        }
        actualizar([&] { m_ocupado = false; });
        if (r.esExito()) {
            // D4: el envio lo dispara el usuario al crear; se encola en el
            // ejecutor (no bloquea). D17: tambien con el monitoreo pausado.
            if (m_acciones) {
                m_acciones->enviar(r.valor());
            }
            emit submitted(r.valor().texto());
            return;
        }
        aplicarErrorCrear(r.error(), request);
    });
}

void NuevaSolicitudViewModel::aplicarErrorCrear(const ErrorCrear& error,
                                                const NuevaSolicitudRequest& request)
{
    actualizar([&] {
        m_errorServicio.clear();
        m_solicitudExistenteId.clear();
        switch (error.tipo) {
        case ErrorCrear::Tipo::Validacion:
            m_errorServicio = error.validaciones.isEmpty()
                                  ? tr("La solicitud no es valida.")
                                  : textoDeValidacion(error.validaciones.constFirst().codigo);
            break;
        case ErrorCrear::Tipo::FiltroInvalido: {
            QStringList mensajes;
            for (const ErrorSolicitudCanonica& f : error.filtros) {
                mensajes.append(f.mensaje);
            }
            m_errorServicio = mensajes.isEmpty() ? tr("Algun filtro no es valido.")
                                                 : mensajes.join(QLatin1Char(' '));
            break;
        }
        case ErrorCrear::Tipo::DedupBloqueado:
            m_errorServicio = tr("No se puede crear: ya existe una solicitud equivalente.");
            if (error.duplicado) {
                const QString motivo = textoDeMotivo(error.duplicado->motivo);
                if (!motivo.isEmpty()) {
                    m_errorServicio += QLatin1Char(' ') + motivo;
                }
                if (error.duplicado->solicitudReferencia) {
                    m_solicitudExistenteId = error.duplicado->solicitudReferencia->texto();
                }
            }
            break;
        case ErrorCrear::Tipo::RequiereConfirmacion:
            ++m_genEnvio;
            m_snapshot = request;
            m_motivoDuplicado = error.duplicado ? textoDeMotivo(error.duplicado->motivo)
                                                : tr("Existe una solicitud equivalente.");
            break;
        case ErrorCrear::Tipo::Persistencia:
        case ErrorCrear::Tipo::Integridad:
            m_errorServicio = tr("No se pudo guardar la solicitud. Intenta de nuevo.");
            break;
        }
    });
}

void NuevaSolicitudViewModel::confirmarDuplicado()
{
    if (!m_snapshot || m_ocupado) {
        return;
    }
    const NuevaSolicitudRequest request = *m_snapshot;
    actualizar([&] {
        m_snapshot.reset();
        m_motivoDuplicado.clear();
    });
    crearConfirmacion(request, ConfirmacionDuplicado::Confirmada);
}

void NuevaSolicitudViewModel::cancelarDuplicado()
{
    actualizar([&] {
        ++m_genEnvio;
        m_snapshot.reset();
        m_motivoDuplicado.clear();
        m_ocupado = false;
    });
}

void NuevaSolicitudViewModel::restablecerCampos()
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
    const bool cambiaComprobante = !m_tipoComprobante.isEmpty();
    const bool cambiaComplemento = !m_complemento.isEmpty();

    actualizar([&] {
        ++m_genEnvio;
        m_perfilId.clear();
        m_tipoDescarga = tipo;
        m_fechaInicial = inicial;
        m_fechaFinal = final_;
        m_rfcContraparte.clear();
        m_tipoComprobante.clear();
        m_complemento.clear();
        m_tocado = false;
        m_ocupado = false;
        m_errorServicio.clear();
        m_snapshot.reset();
        m_motivoDuplicado.clear();
        m_solicitudExistenteId.clear();
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
    if (cambiaComprobante) {
        emit tipoComprobanteChanged();
    }
    if (cambiaComplemento) {
        emit complementoChanged();
    }
}

void NuevaSolicitudViewModel::reiniciar()
{
    restablecerCampos();
    cargarPerfiles();
}

void NuevaSolicitudViewModel::cargarPerfiles()
{
    const quint64 generacion = ++m_genPerfiles;
    actualizar([&] { m_cargandoPerfiles = true; });
    m_consulta->listarListosParaSolicitudes().then(
        this, [this, generacion](ConsultaPreparacionPerfiles::ResultadoLista r) {
            if (generacion != m_genPerfiles) {
                return; // respuesta de una carga anterior
            }
            actualizar([&] {
                m_cargandoPerfiles = false;
                if (r.esExito()) {
                    QList<PerfilResumen> listos;
                    for (const PerfilConPreparacion& p : r.valor()) {
                        if (p.listoParaSolicitudes) { // la consulta ya filtra; defensa
                            listos.append(p.perfil);
                        }
                    }
                    m_perfilesDisponibles->reemplazar(std::move(listos));
                    m_perfilesCargados = true;
                } else {
                    m_errorServicio = tr("No se pudieron cargar los perfiles SAT.");
                }
            });
        });
}

} // namespace satcfdi
