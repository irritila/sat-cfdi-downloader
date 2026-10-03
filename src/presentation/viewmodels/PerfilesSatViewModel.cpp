#include "PerfilesSatViewModel.h"

#include "application/common/Errores.h"
#include "application/profiles/ConsultaPreparacionPerfiles.h"
#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/PerfilesSatService.h"
#include "domain/perfiles/PerfilId.h"

#include <QFuture>

#include <utility>

namespace satcfdi {

namespace {

const QString kCampoRfc = QStringLiteral("rfc");
const QString kCampoNombre = QStringLiteral("nombre");

QString textoValidacion(CodigoValidacionPerfil codigo)
{
    return codigo == CodigoValidacionPerfil::RfcInvalido
               ? QObject::tr("El RFC no es valido.")
               : QObject::tr("Indica un nombre para el perfil.");
}

QString claveValidacion(CodigoValidacionPerfil codigo)
{
    return codigo == CodigoValidacionPerfil::RfcInvalido ? QStringLiteral("RfcInvalido")
                                                         : QStringLiteral("NombreRequerido");
}

bool conCredencial(PreparacionPerfil p)
{
    switch (p) {
    case PreparacionPerfil::Lista:
    case PreparacionPerfil::Vencida:
    case PreparacionPerfil::NoVigenteAun:
    case PreparacionPerfil::MaterialFaltante:
    case PreparacionPerfil::MaterialDanado:
        return true;
    case PreparacionPerfil::Verificando:
    case PreparacionPerfil::SinCredencial:
    case PreparacionPerfil::EstadoNoDisponible:
        return false;
    }
    return false;
}

} // namespace

PerfilesSatViewModel::PerfilesSatViewModel(PerfilesSatService* perfiles,
                                           CredencialesSatService* credenciales,
                                           QObject* parent)
    : QObject(parent)
    , m_perfilesServicio(perfiles)
    , m_consulta(new ConsultaPreparacionPerfiles(*perfiles, *credenciales, this))
    , m_modelo(new PerfilesSatListModel(this))
{
    Q_ASSERT(perfiles != nullptr);
    Q_ASSERT(credenciales != nullptr);
    connect(perfiles, &PerfilesSatService::perfilesCambiaron, this, [this] {
        if (m_cargado || m_cargando) {
            cargar();
        }
    });
    connect(credenciales, &CredencialesSatService::credencialCambio, this,
            &PerfilesSatViewModel::reintentarEstado);
}

PerfilesSatViewModel::EstadoLista PerfilesSatViewModel::estadoLista() const
{
    if (!m_errorLista.isEmpty()) {
        return m_cargando ? EstadoLista::Cargando : EstadoLista::Error;
    }
    if (!m_cargado) {
        return EstadoLista::Cargando;
    }
    return m_modelo->count() == 0 ? EstadoLista::Vacia : EstadoLista::ConDatos;
}

bool PerfilesSatViewModel::sucio() const
{
    switch (m_modo) {
    case Modo::Ninguno:
        return false;
    case Modo::Nuevo:
        return !m_rfc.trimmed().isEmpty() || !m_nombre.trimmed().isEmpty();
    case Modo::Edicion:
        return m_nombre != m_nombreGuardado;
    }
    return false;
}

bool PerfilesSatViewModel::puedeGuardar() const
{
    if (m_guardando || !sucio()) {
        return false;
    }
    // Validacion superficial; las reglas viven en el servicio.
    if (m_modo == Modo::Nuevo) {
        return !m_rfc.trimmed().isEmpty() && !m_nombre.trimmed().isEmpty();
    }
    return m_modo == Modo::Edicion && !m_nombre.trimmed().isEmpty();
}

std::optional<PerfilConPreparacion> PerfilesSatViewModel::seleccion() const
{
    if (m_modo != Modo::Edicion || m_perfilId.isEmpty()) {
        return std::nullopt;
    }
    return m_modelo->perfil(m_perfilId);
}

bool PerfilesSatViewModel::seleccionActiva() const
{
    const auto s = seleccion();
    return s && s->perfil.activo;
}

QString PerfilesSatViewModel::seleccionPreparacion() const
{
    const auto s = seleccion();
    return s ? claveEstable(s->preparacion) : QString();
}

QString PerfilesSatViewModel::seleccionEstadoTexto() const
{
    const auto s = seleccion();
    return s ? PerfilesSatListModel::textoDe(s->preparacion) : QString();
}

bool PerfilesSatViewModel::seleccionListo() const
{
    const auto s = seleccion();
    return s && s->listoParaSolicitudes;
}

bool PerfilesSatViewModel::tieneCredencial() const
{
    const auto s = seleccion();
    return s && conCredencial(s->preparacion);
}

bool PerfilesSatViewModel::puedeGestionarEFirma() const
{
    const auto s = seleccion();
    if (!s || sucio() || m_guardando || !s->perfil.activo) {
        return false;
    }
    return s->preparacion != PreparacionPerfil::Verificando
           && s->preparacion != PreparacionPerfil::EstadoNoDisponible;
}

void PerfilesSatViewModel::setRfc(const QString& valor)
{
    if (m_modo != Modo::Nuevo || m_rfc == valor) {
        return; // el RFC es inmutable en edicion
    }
    m_rfc = valor;
    if (m_campoConError == kCampoRfc || !m_errorRfc.isEmpty()) {
        m_errorRfc.clear();
        if (m_campoConError == kCampoRfc) {
            m_campoConError.clear();
            m_errorKey.clear();
        }
    }
    emit formularioChanged();
    emit seleccionChanged();
}

void PerfilesSatViewModel::setNombre(const QString& valor)
{
    if (m_modo == Modo::Ninguno || m_nombre == valor) {
        return;
    }
    m_nombre = valor;
    if (!m_errorNombre.isEmpty() || m_campoConError == kCampoNombre) {
        m_errorNombre.clear();
        if (m_campoConError == kCampoNombre) {
            m_campoConError.clear();
            m_errorKey.clear();
        }
    }
    emit formularioChanged();
    emit seleccionChanged();
}

void PerfilesSatViewModel::cargar()
{
    const quint64 generacion = ++m_genCarga;
    m_cargando = true;
    emit listaChanged();
    m_consulta->listarPersistidos().then(
        this, [this, generacion](ConsultaPreparacionPerfiles::ResultadoLista r) {
            if (generacion != m_genCarga) {
                return; // llego (o llegara) una carga mas reciente
            }
            m_cargando = false;
            if (!r.esExito()) {
                m_errorLista = tr("No se pudieron cargar los perfiles SAT.");
                emit listaChanged();
                return;
            }
            const QList<PerfilConPreparacion> lista = std::move(r).valor();
            m_errorLista.clear();
            m_cargado = true;
            m_modelo->reemplazar(lista);
            emit listaChanged();

            // El perfil en edicion pudo cambiar o desaparecer.
            if (m_modo == Modo::Edicion) {
                if (const auto s = m_modelo->perfil(m_perfilId)) {
                    if (!sucio() && !m_guardando) {
                        m_nombre = s->perfil.nombre;
                    }
                    m_nombreGuardado = s->perfil.nombre;
                    emit formularioChanged();
                } else if (!m_guardando) {
                    cerrarFormulario();
                }
            }
            emit seleccionChanged();

            for (const PerfilConPreparacion& p : lista) {
                verificarPerfil(p.perfil);
            }
        });
}

void PerfilesSatViewModel::verificarPerfil(const PerfilResumen& perfil)
{
    const QString id = perfil.id.texto();
    const quint64 generacion = ++m_genPerfil[id];
    const quint64 carga = m_genCarga;
    m_consulta->verificar(perfil).then(this, [this, id, generacion, carga](PerfilConPreparacion r) {
        if (carga != m_genCarga || m_genPerfil.value(id) != generacion) {
            return; // lista reemplazada o verificacion mas reciente del perfil
        }
        if (m_modelo->actualizar(r) && id == m_perfilId) {
            emit seleccionChanged();
        }
    });
}

void PerfilesSatViewModel::reintentarEstado(const QString& id)
{
    const auto actual = m_modelo->perfil(id);
    if (!actual) {
        return;
    }
    m_modelo->actualizar(PerfilConPreparacion::componer(actual->perfil, PreparacionPerfil::Verificando));
    if (id == m_perfilId) {
        emit seleccionChanged();
    }
    verificarPerfil(actual->perfil);
}

void PerfilesSatViewModel::limpiarErrores()
{
    m_errorRfc.clear();
    m_errorNombre.clear();
    m_errorKey.clear();
    m_errorMessage.clear();
    m_campoConError.clear();
}

void PerfilesSatViewModel::abrirEdicion(const PerfilResumen& perfil)
{
    m_modo = Modo::Edicion;
    m_perfilId = perfil.id.texto();
    m_rfc = perfil.rfc;
    m_nombre = perfil.nombre;
    m_nombreGuardado = perfil.nombre;
}

bool PerfilesSatViewModel::seleccionar(const QString& id)
{
    const auto p = m_modelo->perfil(id);
    if (!p) {
        return false;
    }
    ++m_genFormulario;
    m_guardando = false;
    limpiarErrores();
    abrirEdicion(p->perfil);
    emit formularioChanged();
    emit seleccionChanged();
    return true;
}

void PerfilesSatViewModel::nuevo()
{
    ++m_genFormulario;
    m_guardando = false;
    limpiarErrores();
    m_modo = Modo::Nuevo;
    m_perfilId.clear();
    m_rfc.clear();
    m_nombre.clear();
    m_nombreGuardado.clear();
    emit formularioChanged();
    emit seleccionChanged();
}

void PerfilesSatViewModel::descartar()
{
    if (m_modo == Modo::Nuevo) {
        cerrarFormulario();
        return;
    }
    if (m_modo == Modo::Edicion) {
        ++m_genFormulario;
        m_guardando = false;
        limpiarErrores();
        m_nombre = m_nombreGuardado;
        emit formularioChanged();
        emit seleccionChanged();
    }
}

void PerfilesSatViewModel::cerrarFormulario()
{
    ++m_genFormulario;
    m_guardando = false;
    limpiarErrores();
    m_modo = Modo::Ninguno;
    m_perfilId.clear();
    m_rfc.clear();
    m_nombre.clear();
    m_nombreGuardado.clear();
    emit formularioChanged();
    emit seleccionChanged();
}

void PerfilesSatViewModel::aplicarError(const QString& clave, const QString& mensaje, const QString& campo)
{
    m_errorKey = clave;
    m_errorMessage = mensaje;
    m_campoConError = campo;
}

void PerfilesSatViewModel::guardar()
{
    if (!puedeGuardar() || !m_perfilesServicio) {
        return; // incluye doble envio (guardando)
    }
    const quint64 formulario = m_genFormulario;
    const quint64 operacion = ++m_genGuardar;
    limpiarErrores();
    m_guardando = true;
    emit formularioChanged();
    emit seleccionChanged();

    const auto vigente = [this, formulario, operacion] {
        return formulario == m_genFormulario && operacion == m_genGuardar;
    };
    const auto terminar = [this] {
        m_guardando = false;
        emit formularioChanged();
        emit seleccionChanged();
        if (!m_campoConError.isEmpty()) {
            emit enfocarCampo(m_campoConError);
        }
    };

    if (m_modo == Modo::Nuevo) {
        m_perfilesServicio->crear(m_rfc, m_nombre)
            .then(this, [this, vigente, terminar](PerfilesSatService::ResultadoCrear r) {
                if (!vigente()) {
                    return;
                }
                if (r.esExito()) {
                    abrirEdicion(r.valor());
                    terminar();
                    emit guardado(m_perfilId);
                    return;
                }
                const ErrorCrearPerfil& e = r.error();
                switch (e.tipo) {
                case ErrorCrearPerfil::Tipo::Validacion:
                    for (CodigoValidacionPerfil c : e.validaciones) {
                        (campoDe(c) == CampoPerfil::Rfc ? m_errorRfc : m_errorNombre) = textoValidacion(c);
                    }
                    if (!e.validaciones.isEmpty()) {
                        const CodigoValidacionPerfil primero =
                            e.tieneErrorEn(CampoPerfil::Rfc) ? CodigoValidacionPerfil::RfcInvalido
                                                             : e.validaciones.constFirst();
                        aplicarError(claveValidacion(primero), textoValidacion(primero),
                                     campoDe(primero) == CampoPerfil::Rfc ? kCampoRfc : kCampoNombre);
                    }
                    break;
                case ErrorCrearPerfil::Tipo::RfcDuplicado:
                    m_errorRfc = tr("Ya existe un perfil con ese RFC.");
                    aplicarError(QStringLiteral("RfcDuplicado"), m_errorRfc, kCampoRfc);
                    break;
                case ErrorCrearPerfil::Tipo::Persistencia:
                    aplicarError(QStringLiteral("Persistencia"),
                                 tr("No se pudo guardar el perfil. Intenta de nuevo."), QString());
                    break;
                }
                terminar(); // el formulario se conserva
            });
        return;
    }

    const std::optional<PerfilId> id = PerfilId::desdeTexto(m_perfilId);
    if (!id) {
        m_guardando = false;
        emit formularioChanged();
        return;
    }
    m_perfilesServicio->actualizarNombre(*id, m_nombre)
        .then(this, [this, vigente, terminar](PerfilesSatService::ResultadoActualizar r) {
            if (!vigente()) {
                return;
            }
            if (r.esExito()) {
                m_nombreGuardado = r.valor().nombre;
                m_nombre = r.valor().nombre;
                terminar();
                emit guardado(m_perfilId);
                return;
            }
            const ErrorActualizarPerfil& e = r.error();
            switch (e.tipo) {
            case ErrorActualizarPerfil::Tipo::Validacion:
                m_errorNombre = textoValidacion(CodigoValidacionPerfil::NombreRequerido);
                aplicarError(QStringLiteral("NombreRequerido"), m_errorNombre, kCampoNombre);
                break;
            case ErrorActualizarPerfil::Tipo::PerfilInexistente:
                aplicarError(QStringLiteral("PerfilInexistente"),
                             tr("El perfil ya no existe."), QString());
                break;
            case ErrorActualizarPerfil::Tipo::Persistencia:
                aplicarError(QStringLiteral("Persistencia"),
                             tr("No se pudo guardar el perfil. Intenta de nuevo."), QString());
                break;
            }
            terminar();
        });
}

} // namespace satcfdi
