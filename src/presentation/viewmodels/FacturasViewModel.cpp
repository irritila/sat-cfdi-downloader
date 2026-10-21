#include "FacturasViewModel.h"

#include "domain/cfdi/PlanDescargaGuiada.h"
#include "domain/cfdi/TipoComprobante.h"

#include <QHash>

#include <algorithm>
#include <array>
#include <utility>

// T023.4: pantalla Facturas. Textos en lenguaje de usuario: sin "solicitud",
// "paquete" ni "verificacion" (TestFacturasViewModel::vocabulario).

namespace satcfdi {

namespace {

using EstadoFranja = FranjaMesViewModel::Estado;
using AccionFranja = FranjaMesViewModel::Accion;

constexpr int kAniosAtras = 6; // primer anio navegable: anio en curso - 6

const std::array<const char*, 12> kMeses = {"enero", "febrero", "marzo", "abril", "mayo", "junio",
                                            "julio", "agosto", "septiembre", "octubre", "noviembre", "diciembre"};
const std::array<const char*, 12> kMesesCortos = {"ene", "feb", "mar", "abr", "may", "jun",
                                                  "jul", "ago", "sep", "oct", "nov", "dic"};

QString nombreMes(int mes)
{
    return mes >= 1 && mes <= 12 ? QString::fromUtf8(kMeses[std::size_t(mes - 1)]) : QString();
}

QString capital(QString s)
{
    if (!s.isEmpty()) {
        s[0] = s.at(0).toUpper();
    }
    return s;
}

QString nombreMesAnio(MesCalendario m)
{
    return QStringLiteral("%1 %2").arg(nombreMes(m.mes())).arg(m.anio());
}

QString textoDireccion(TipoDescarga d)
{
    return d == TipoDescarga::Emitidos ? QStringLiteral("emitidas") : QStringLiteral("recibidas");
}

// "15 de octubre, 14:05" desde el reloj de pared Centro.
QString textoHasta(const QDateTime& pared)
{
    return QStringLiteral("%1 de %2, %3")
        .arg(pared.date().day())
        .arg(nombreMes(pared.date().month()))
        .arg(pared.time().toString(u"HH:mm"));
}

QString textoFacturas(int n)
{
    return n == 1 ? QStringLiteral("1 factura") : QStringLiteral("%1 facturas").arg(n);
}

struct Presentacion {
    QString clave;
    QString texto;
    QString tono;
    QString icono;
};

Presentacion presentacion(EstadoFranja e)
{
    switch (e) {
    case EstadoFranja::SinDescargar:
        return {QStringLiteral("SinDescargar"), QStringLiteral("Sin descargar"), QStringLiteral("neutro"),
                QStringLiteral("arrow-down-circle")};
    case EstadoFranja::Preparando:
        return {QStringLiteral("Preparando"), QStringLiteral("Preparando"), QStringLiteral("info"),
                QStringLiteral("clock")};
    case EstadoFranja::Disponibles:
        return {QStringLiteral("Disponibles"), QStringLiteral("Disponibles"), QStringLiteral("exito"),
                QStringLiteral("check-circle")};
    case EstadoFranja::DisponiblesParcialmente:
        return {QStringLiteral("DisponiblesParcialmente"), QStringLiteral("Disponibles parcialmente"),
                QStringLiteral("aviso"), QStringLiteral("circle-dashed")};
    case EstadoFranja::RequiereAtencion:
        return {QStringLiteral("RequiereAtencion"), QStringLiteral("Requiere atención"), QStringLiteral("error"),
                QStringLiteral("exclamation-triangle")};
    case EstadoFranja::Futuro:
        return {QStringLiteral("Futuro"), QStringLiteral("Aún no empieza"), QStringLiteral("neutro"),
                QStringLiteral("calendar")};
    case EstadoFranja::Cargando:
        return {QStringLiteral("Cargando"), QStringLiteral("Cargando"), QStringLiteral("neutro"),
                QStringLiteral("clock")};
    case EstadoFranja::Error:
        return {QStringLiteral("Error"), QStringLiteral("No disponible"), QStringLiteral("error"),
                QStringLiteral("exclamation-octagon")};
    case EstadoFranja::SinPerfil:
        break;
    }
    return {QStringLiteral("SinPerfil"), QString(), QStringLiteral("neutro"), QString()};
}

EstadoFranja estadoFranja(const EvaluacionCoberturaMensual& e)
{
    if (e.tipoMes == TipoMes::Futuro) {
        return EstadoFranja::Futuro;
    }
    switch (e.estado) {
    case EstadoMes::RequiereAtencion: return EstadoFranja::RequiereAtencion;
    case EstadoMes::Preparando: return EstadoFranja::Preparando;
    case EstadoMes::Disponibles: return EstadoFranja::Disponibles;
    case EstadoMes::DisponiblesParcialmente: return EstadoFranja::DisponiblesParcialmente;
    case EstadoMes::SinDescargar: return EstadoFranja::SinDescargar;
    }
    return EstadoFranja::SinDescargar;
}

QString textoCausa(const CausaMensual& c)
{
    const QString d = textoDireccion(c.direccion);
    switch (c.causa) {
    case CausaAtencion::Codigo5002: return QStringLiteral("El SAT ya no permite volver a pedir las %1 de este mes.").arg(d);
    case CausaAtencion::Codigo5005: return QStringLiteral("El SAT indica que ya hay una descarga igual de las %1 en curso.").arg(d);
    case CausaAtencion::EnvioIncierto: return QStringLiteral("No se sabe si el SAT recibió la petición de las %1.").arg(d);
    case CausaAtencion::EnvioFallido: return QStringLiteral("El SAT rechazó la petición de las %1.").arg(d);
    case CausaAtencion::ErrorSat: return QStringLiteral("El SAT reportó un error al preparar las %1.").arg(d);
    case CausaAtencion::Rechazada: return QStringLiteral("El SAT no aceptó preparar las %1.").arg(d);
    case CausaAtencion::Vencida: return QStringLiteral("El SAT dejó de ofrecer las %1 antes de descargarlas.").arg(d);
    case CausaAtencion::PaqueteError: return QStringLiteral("No se pudo descargar una parte de las %1.").arg(d);
    case CausaAtencion::PaqueteVencido: return QStringLiteral("Una parte de las %1 ya no está disponible en el SAT.").arg(d);
    case CausaAtencion::ZipNoDisponible: return QStringLiteral("Falta en este equipo un archivo de las %1.").arg(d);
    case CausaAtencion::ZipIlegible: return QStringLiteral("Un archivo de las %1 que entregó el SAT está dañado.").arg(d);
    case CausaAtencion::IndiceIlegible: return QStringLiteral("No se pudieron leer las %1 guardadas en este equipo.").arg(d);
    case CausaAtencion::IndexacionSinProgramar: return QStringLiteral("Las %1 descargadas aún no se han organizado.").arg(d);
    }
    return {};
}

std::optional<QDateTime> hastaMinimo(const EvaluacionCoberturaMensual& e)
{
    std::optional<QDateTime> r;
    for (const CoberturaDireccion* c : {&e.emitidos, &e.recibidos}) {
        if (c->hasta && (!r || *c->hasta < *r)) {
            r = c->hasta;
        }
    }
    return r;
}

AccionFranja accionDe(AccionGuiada a)
{
    switch (a) {
    case AccionGuiada::DescargarFacturas: return AccionFranja::DescargarFacturas;
    case AccionGuiada::ActualizarMes: return AccionFranja::ActualizarMes;
    case AccionGuiada::CompletarMes: return AccionFranja::CompletarMes;
    }
    return AccionFranja::DescargarFacturas;
}

QString textoAccion(AccionFranja a)
{
    switch (a) {
    case AccionFranja::DescargarFacturas: return QStringLiteral("Descargar facturas");
    case AccionFranja::ActualizarMes: return QStringLiteral("Actualizar mes");
    case AccionFranja::CompletarMes: return QStringLiteral("Completar mes");
    case AccionFranja::Reintentar: return QStringLiteral("Reintentar");
    case AccionFranja::RegistrarEFirma: return QStringLiteral("Registrar e.firma…");
    case AccionFranja::VerDetalles: return QStringLiteral("Ver detalles técnicos");
    case AccionFranja::ReintentarLectura: return QStringLiteral("Reintentar");
    case AccionFranja::Ninguna: break;
    }
    return {};
}

const QString kVerDetalles = QStringLiteral("Ver detalles técnicos");
const QString kMotivoEFirma = QStringLiteral("Registra la e.firma del perfil para descargar.");

} // namespace

struct FacturasViewModel::Impl {
    QList<PerfilFacturas> perfiles;
    bool perfilesCargados = false;
    int perfil = -1;
    std::optional<MesCalendario> mes;
    QHash<QString, MesCalendario> ultimoMes; // D16, por perfil en la sesion
    bool activo = false;

    // Resultados del mes elegido.
    quint64 gen = 0;           // cambia con perfil/mes: descarta respuestas viejas
    quint64 genLista = 0;      // cambia tambien con filtros y pestana
    quint64 genPerfiles = 0;
    // Token por consulta: dos recargas del mismo mes pueden responder en
    // desorden; solo la ultima de cada tipo se aplica.
    quint64 genDisponibilidad = 0;
    quint64 genConteosAnio = 0;
    QHash<int, quint64> genOtrosMeses; // mes -> token de su ultima consulta
    bool listaCargada = false;
    bool errorLista = false;
    bool listaConFiltros = false;
    int pendientesLista = 0;
    int conteoEmitidas = 0;
    int conteoRecibidas = 0;
    std::optional<EvaluacionCoberturaMensual> evaluacion;
    bool errorEvaluacion = false;
    QHash<int, EvaluacionCoberturaMensual> evaluacionesAnio; // mes -> evaluacion
    std::array<int, 12> conteosAnio{};
    bool conteosAnioConocidos = false;

    // Accion de la franja.
    bool ejecutando = false;
    QString aviso;
    std::optional<PreplanGuiado> preplan;
    QList<TipoDescarga> porConfirmar;
    DecisionesGuiadas decisiones;
    int replanes = 0;

    // T023.5: destino pedido por Historial o una notificacion antes de que
    // los perfiles carguen, y Reintentar pendiente de la disponibilidad.
    struct Destino {
        QString perfil;
        int anio = 0;
        int mes = 0;
        AlLlegar alLlegar = AlLlegar::Nada;
    };
    std::optional<Destino> destinoPendiente;
    std::optional<std::pair<QString, MesCalendario>> reintentarAlCargar;

    // Anuncio: estado del mes ya anunciado para (perfil, mes).
    QString claveAnunciada;
    std::optional<EstadoFranja> estadoAnunciado;

    const PerfilFacturas* perfilActual() const
    {
        return perfil >= 0 && perfil < perfiles.size() ? &perfiles.at(perfil) : nullptr;
    }
};

FacturasViewModel::FacturasViewModel(QObject* parent)
    : QObject(parent)
    , m_perfiles(new PerfilesDisponiblesModel(this))
    , m_meses(new MesesFacturasModel(this))
    , m_franja(new FranjaMesViewModel(this))
    , m_modelo(new FacturasListModel(this))
    , m_visor(new VisorPdfCfdiViewModel(this))
    , m_impl(std::make_unique<Impl>())
{
    m_retardo.setSingleShot(true);
    m_retardo.setInterval(kIntervaloBusquedaMs);
    connect(&m_retardo, &QTimer::timeout, this, [this] { consultarLista(true); });
    actualizarFranja();
}

FacturasViewModel::~FacturasViewModel() = default;

void FacturasViewModel::setFuente(FuenteFacturasMes* fuente)
{
    if (m_fuente) {
        disconnect(m_fuente, nullptr, this, nullptr);
    }
    m_fuente = fuente;
    if (!fuente) {
        return;
    }
    connect(fuente, &FuenteFacturasMes::datosCambiados, this, [this](const PerfilId& perfil) {
        const PerfilFacturas* p = m_impl->perfilActual();
        if (m_impl->activo && p && (perfil.esNulo() || p->id == perfil)) {
            recargarMes(false);
        }
    });
    connect(fuente, &FuenteFacturasMes::perfilesCambiados, this, [this] {
        if (m_impl->activo) {
            cargarPerfiles();
        }
    });
}

void FacturasViewModel::setPreferencias(PreferenciasFacturas* preferencias)
{
    m_preferencias = preferencias;
}

void FacturasViewModel::setPrevisualizacion(PrevisualizacionCfdi* previsualizacion)
{
    m_visor->setServicio(previsualizacion);
}

// ---- Perfil -------------------------------------------------------------------

int FacturasViewModel::perfilIndice() const
{
    return m_impl->perfil;
}

QString FacturasViewModel::perfilId() const
{
    const PerfilFacturas* p = m_impl->perfilActual();
    return p ? p->id.texto() : QString();
}

QString FacturasViewModel::perfilNombre() const
{
    const PerfilFacturas* p = m_impl->perfilActual();
    return p ? p->nombre : QString();
}

QString FacturasViewModel::perfilRfc() const
{
    const PerfilFacturas* p = m_impl->perfilActual();
    return p ? p->rfc : QString();
}

bool FacturasViewModel::hayPerfiles() const
{
    return !m_impl->perfiles.isEmpty();
}

bool FacturasViewModel::eFirmaLista() const
{
    const PerfilFacturas* p = m_impl->perfilActual();
    return p && p->credencial == EstadoCredencial::Lista;
}

QString FacturasViewModel::motivoEFirma() const
{
    return m_impl->perfilActual() && !eFirmaLista() ? kMotivoEFirma : QString();
}

// ---- Mes ----------------------------------------------------------------------

std::optional<MesCalendario> FacturasViewModel::mesEnCurso() const
{
    return m_fuente ? centro::mesEnCurso(m_fuente->ahoraUtc()) : std::nullopt;
}

bool FacturasViewModel::esFuturo(MesCalendario m) const
{
    const auto curso = mesEnCurso();
    return curso && m > *curso;
}

bool FacturasViewModel::navegable(MesCalendario m) const
{
    const auto curso = mesEnCurso();
    return curso && m <= *curso && m.anio() >= curso->anio() - kAniosAtras;
}

int FacturasViewModel::anio() const
{
    return m_impl->mes ? m_impl->mes->anio() : 0;
}

int FacturasViewModel::mes() const
{
    return m_impl->mes ? m_impl->mes->mes() : 0;
}

QString FacturasViewModel::tituloMes() const
{
    return m_impl->mes ? capital(nombreMesAnio(*m_impl->mes)) : QString();
}

bool FacturasViewModel::puedeMesAnterior() const
{
    const auto a = m_impl->mes ? m_impl->mes->anterior() : std::nullopt;
    return a && navegable(*a);
}

bool FacturasViewModel::puedeMesSiguiente() const
{
    const auto s = m_impl->mes ? m_impl->mes->siguiente() : std::nullopt;
    return s && navegable(*s);
}

bool FacturasViewModel::puedeAnioAnterior() const
{
    if (!m_impl->mes) {
        return false;
    }
    const auto m = MesCalendario::crear(m_impl->mes->anio() - 1, 12);
    return m && navegable(*m);
}

bool FacturasViewModel::puedeAnioSiguiente() const
{
    if (!m_impl->mes) {
        return false;
    }
    const auto m = MesCalendario::crear(m_impl->mes->anio() + 1, 1);
    return m && navegable(*m);
}

// ---- Pestanas y lista -----------------------------------------------------------

void FacturasViewModel::setDireccion(Direccion direccion)
{
    if (m_direccion == direccion) {
        return;
    }
    m_direccion = direccion;
    m_visor->cerrar();
    emit direccionChanged();
    consultarLista(false); // los conteos por mes ya incluyen ambas pestanas
}

int FacturasViewModel::conteoEmitidas() const
{
    return m_impl->conteoEmitidas;
}

int FacturasViewModel::conteoRecibidas() const
{
    return m_impl->conteoRecibidas;
}

QString FacturasViewModel::textoPestanaEmitidas() const
{
    return QStringLiteral("Emitidas (%1)").arg(m_impl->conteoEmitidas);
}

QString FacturasViewModel::textoPestanaRecibidas() const
{
    return QStringLiteral("Recibidas (%1)").arg(m_impl->conteoRecibidas);
}

QString FacturasViewModel::encabezadoContraparte() const
{
    return m_direccion == Direccion::Emitidas ? QStringLiteral("Receptor") : QStringLiteral("Emisor");
}

QString FacturasViewModel::placeholderBusqueda() const
{
    return m_direccion == Direccion::Emitidas ? QStringLiteral("Buscar por receptor, RFC, UUID, serie o folio")
                                              : QStringLiteral("Buscar por emisor, RFC, UUID, serie o folio");
}

void FacturasViewModel::setTexto(const QString& texto)
{
    if (m_texto == texto) {
        return;
    }
    m_texto = texto;
    emit filtrosChanged();
    if (m_retardo.interval() <= 0) {
        consultarLista(true);
    } else {
        m_retardo.start();
    }
}

void FacturasViewModel::setTipo(const QString& tipo)
{
    if (m_tipo == tipo) {
        return;
    }
    m_tipo = tipo;
    emit filtrosChanged();
    m_retardo.stop();
    consultarLista(true);
}

bool FacturasViewModel::hayFiltros() const
{
    return !m_texto.trimmed().isEmpty() || !m_tipo.isEmpty();
}

QString FacturasViewModel::vista() const
{
    if (!hayPerfiles()) {
        return QStringLiteral("sinPerfil");
    }
    if (!m_impl->mes || !m_impl->perfilActual()) {
        return QStringLiteral("cargando");
    }
    if (esFuturo(*m_impl->mes)) {
        return QStringLiteral("futuro");
    }
    if (m_impl->errorLista) {
        return QStringLiteral("error");
    }
    if (!m_impl->listaCargada) {
        return QStringLiteral("cargando");
    }
    if (m_modelo->count() > 0) {
        return QStringLiteral("lista");
    }
    if (m_impl->listaConFiltros) {
        return QStringLiteral("sinCoincidencias");
    }
    const EvaluacionCoberturaMensual* e = m_impl->evaluacion ? &*m_impl->evaluacion : nullptr;
    if (e && (e->estado == EstadoMes::Preparando
              || std::any_of(e->causas.cbegin(), e->causas.cend(),
                             [](const CausaMensual& c) { return c.causa == CausaAtencion::IndexacionSinProgramar; }))) {
        return QStringLiteral("llenandose");
    }
    return QStringLiteral("sinFacturas");
}

QString FacturasViewModel::textoVista() const
{
    const QString v = vista();
    if (v == u"sinPerfil") {
        return QStringLiteral("Agrega un perfil SAT para ver sus facturas.");
    }
    if (v == u"cargando") {
        return QStringLiteral("Cargando facturas…");
    }
    if (v == u"futuro") {
        return QStringLiteral("Este mes aún no empieza.");
    }
    if (v == u"error") {
        return QStringLiteral("No se pudieron leer las facturas de este mes.");
    }
    if (v == u"sinCoincidencias") {
        return QStringLiteral("Ninguna factura coincide con la búsqueda.");
    }
    if (v == u"llenandose") {
        return QStringLiteral("Las facturas aparecerán aquí conforme se descarguen.");
    }
    if (v == u"sinFacturas") {
        if (m_impl->evaluacion && m_impl->evaluacion->estado == EstadoMes::SinDescargar) {
            return QStringLiteral("Aún no hay facturas de este mes en el equipo.");
        }
        return m_direccion == Direccion::Emitidas ? QStringLiteral("No hay facturas emitidas en este mes.")
                                                  : QStringLiteral("No hay facturas recibidas en este mes.");
    }
    return {};
}

bool FacturasViewModel::actualizando() const
{
    return m_impl->pendientesLista > 0 && m_impl->listaCargada;
}

// ---- Confirmacion -------------------------------------------------------------

bool FacturasViewModel::confirmacionPendiente() const
{
    return !m_impl->porConfirmar.isEmpty();
}

QString FacturasViewModel::confirmacionTitulo() const
{
    if (m_impl->porConfirmar.isEmpty() || !m_impl->mes) {
        return {};
    }
    return QStringLiteral("¿Descargar de nuevo las %1 de %2?")
        .arg(textoDireccion(m_impl->porConfirmar.first()), nombreMesAnio(*m_impl->mes));
}

QString FacturasViewModel::confirmacionTexto() const
{
    if (m_impl->porConfirmar.isEmpty()) {
        return {};
    }
    return QStringLiteral("Ya se intentó descargar este periodo y no terminó bien. Puedes pedirlo de nuevo; "
                          "el SAT podría rechazar una petición igual. Las %1 no cambian.")
        .arg(textoDireccion(m_impl->porConfirmar.first() == TipoDescarga::Emitidos ? TipoDescarga::Recibidos
                                                                                   : TipoDescarga::Emitidos));
}

QString FacturasViewModel::textoConfirmar() const
{
    return m_impl->porConfirmar.isEmpty() ? QString() : QStringLiteral("Descargar de nuevo");
}

QString FacturasViewModel::textoCancelar() const
{
    return m_impl->porConfirmar.isEmpty() ? QString()
                                          : QStringLiteral("Omitir %1").arg(textoDireccion(m_impl->porConfirmar.first()));
}

// ---- Ciclo y seleccion --------------------------------------------------------

void FacturasViewModel::activar()
{
    m_impl->activo = true;
    cargarPerfiles();
}

void FacturasViewModel::desactivar()
{
    m_impl->activo = false;
    m_retardo.stop();
    m_visor->cerrar();
}

void FacturasViewModel::cargarPerfiles()
{
    if (!m_fuente) {
        return;
    }
    const quint64 gen = ++m_impl->genPerfiles;
    m_fuente->perfiles().then(this, [this, gen](FuenteFacturasMes::ResultadoPerfiles r) {
        if (gen != m_impl->genPerfiles) {
            return;
        }
        if (!r) {
            if (!m_impl->perfilesCargados) {
                m_impl->perfilesCargados = true;
                emit perfilChanged();
                emit vistaChanged();
                actualizarFranja();
            }
            return;
        }
        aplicarPerfiles(std::move(r).valor());
        aplicarDestinoPendiente();
    });
}

void FacturasViewModel::aplicarPerfiles(QList<PerfilFacturas> perfiles)
{
    const QString previo = perfilId();
    m_impl->perfiles = std::move(perfiles);
    m_impl->perfilesCargados = true;
    QList<PerfilResumen> resumen;
    for (const PerfilFacturas& p : std::as_const(m_impl->perfiles)) {
        PerfilResumen r;
        r.id = p.id;
        r.rfc = p.rfc;
        r.nombre = p.nombre;
        resumen.append(r);
    }
    m_perfiles->reemplazar(resumen);

    // Seleccion: el actual si sigue; si no, el ultimo usado; si no, el primero.
    int indice = -1;
    auto buscar = [this](const QString& id) {
        for (qsizetype i = 0; i < m_impl->perfiles.size(); ++i) {
            if (m_impl->perfiles.at(i).id.texto() == id) {
                return int(i);
            }
        }
        return -1;
    };
    if (!previo.isEmpty()) {
        indice = buscar(previo);
    }
    if (indice < 0 && m_preferencias) {
        if (const auto ultimo = m_preferencias->ultimoPerfil()) {
            indice = buscar(*ultimo);
        }
    }
    if (indice < 0 && !m_impl->perfiles.isEmpty()) {
        indice = 0;
    }
    if (indice >= 0 && perfilId() == m_impl->perfiles.at(indice).id.texto() && m_impl->mes) {
        // Mismo perfil: solo pudo cambiar su e.firma.
        m_impl->perfil = indice;
        emit perfilChanged();
        actualizarFranja();
        return;
    }
    aplicarPerfil(indice, false);
}

void FacturasViewModel::aplicarPerfil(int indice, bool guardar)
{
    if (const PerfilFacturas* p = m_impl->perfilActual(); p && m_impl->mes) {
        m_impl->ultimoMes.insert(p->id.texto(), *m_impl->mes);
    }
    m_impl->perfil = indice;
    const PerfilFacturas* p = m_impl->perfilActual();
    if (p && guardar && m_preferencias) {
        m_preferencias->setUltimoPerfil(p->id.texto());
    }
    std::optional<MesCalendario> mes;
    if (p) {
        const auto it = m_impl->ultimoMes.constFind(p->id.texto());
        if (it != m_impl->ultimoMes.cend()) {
            mes = *it;
        } else if (m_fuente) {
            mes = centro::mesInicialPorOmision(m_fuente->ahoraUtc());
        }
    }
    emit perfilChanged();
    aplicarMes(mes, true);
}

void FacturasViewModel::seleccionarPerfil(int indice)
{
    if (indice < 0 || indice >= m_impl->perfiles.size() || indice == m_impl->perfil) {
        return;
    }
    aplicarPerfil(indice, true);
}

bool FacturasViewModel::seleccionarPerfilId(const QString& perfilId)
{
    for (qsizetype i = 0; i < m_impl->perfiles.size(); ++i) {
        if (m_impl->perfiles.at(i).id.texto() == perfilId) {
            if (int(i) != m_impl->perfil) {
                aplicarPerfil(int(i), true);
            }
            return true;
        }
    }
    return false;
}

bool FacturasViewModel::seleccionarMes(int anio, int mes)
{
    const auto m = MesCalendario::crear(anio, mes);
    if (!m || !navegable(*m) || !m_impl->perfilActual()) {
        return false;
    }
    if (m_impl->mes && *m_impl->mes == *m) {
        return true;
    }
    const bool otroAnio = !m_impl->mes || m_impl->mes->anio() != m->anio();
    aplicarMes(*m, otroAnio);
    return true;
}

bool FacturasViewModel::moverMes(int delta)
{
    if (!m_impl->mes || (delta != 1 && delta != -1)) {
        return false;
    }
    const auto m = delta > 0 ? m_impl->mes->siguiente() : m_impl->mes->anterior();
    return m && seleccionarMes(m->anio(), m->mes());
}

bool FacturasViewModel::cambiarAnio(int delta)
{
    if (!m_impl->mes || (delta != 1 && delta != -1)) {
        return false;
    }
    auto m = MesCalendario::crear(m_impl->mes->anio() + delta, m_impl->mes->mes());
    const auto curso = mesEnCurso();
    if (m && curso && *m > *curso) {
        m = curso; // al anio en curso: su mes mas reciente
    }
    return m && seleccionarMes(m->anio(), m->mes());
}

void FacturasViewModel::aplicarMes(std::optional<MesCalendario> mes, bool recargarAnio)
{
    if (m_impl->reintentarAlCargar
        && (!mes || m_impl->reintentarAlCargar->second != *mes || m_impl->reintentarAlCargar->first != perfilId())) {
        m_impl->reintentarAlCargar.reset(); // el usuario (o el destino) cambio de mes
    }
    m_impl->mes = mes;
    ++m_impl->gen;
    m_impl->evaluacion.reset();
    m_impl->errorEvaluacion = false;
    m_impl->listaCargada = false;
    m_impl->errorLista = false;
    m_impl->conteoEmitidas = 0;
    m_impl->conteoRecibidas = 0;
    m_impl->aviso.clear();
    m_impl->porConfirmar.clear();
    m_impl->preplan.reset();
    m_impl->ejecutando = false;
    if (recargarAnio) {
        m_impl->evaluacionesAnio.clear();
        m_impl->conteosAnioConocidos = false;
    }
    m_texto.clear();
    m_tipo.clear();
    m_retardo.stop();
    m_visor->cerrar();
    m_modelo->limpiar();
    emit filtrosChanged();
    emit mesChanged();
    emit conteosChanged();
    emit confirmacionChanged();
    emit vistaChanged();
    actualizarMeses();
    actualizarFranja();
    recargarMes(true);
    if (recargarAnio) {
        consultarAnio();
    }
}

// ---- Consultas ----------------------------------------------------------------

void FacturasViewModel::recargar()
{
    m_impl->errorLista = false;
    m_impl->errorEvaluacion = false;
    emit vistaChanged();
    recargarMes(false);
    consultarAnio();
}

void FacturasViewModel::recargarMes(bool revalidar)
{
    consultarLista(false);
    consultarDisponibilidad(revalidar);
    consultarConteosAnio();
}

void FacturasViewModel::consultarLista(bool cambioDeFiltro)
{
    const PerfilFacturas* p = m_impl->perfilActual();
    if (!m_fuente || !p || !m_impl->mes || esFuturo(*m_impl->mes)) {
        return;
    }
    if (cambioDeFiltro) {
        m_visor->cerrar();
    }
    ConsultaCatalogoMensual c;
    c.perfil = p->id;
    c.mes = *m_impl->mes;
    c.direccion = m_direccion == Direccion::Emitidas ? TipoDescarga::Emitidos : TipoDescarga::Recibidos;
    c.texto = m_texto;
    if (!m_tipo.isEmpty()) {
        c.tipo = tipoComprobanteDesdeTexto(m_tipo);
    }
    c.conConteosAnuales = false;
    const bool conFiltros = hayFiltros();
    const quint64 gen = m_impl->gen;
    const quint64 genLista = ++m_impl->genLista;
    ++m_impl->pendientesLista;
    emit vistaChanged();
    m_fuente->consultar(c).then(this, [this, gen, genLista, conFiltros, dir = c.direccion](
                                          FuenteFacturasMes::ResultadoCatalogo r) {
        --m_impl->pendientesLista;
        if (gen != m_impl->gen || genLista != m_impl->genLista) {
            emit vistaChanged();
            return;
        }
        if (!r) {
            m_impl->errorLista = true;
            emit vistaChanged();
            return;
        }
        const CatalogoMensual& cat = r.valor();
        QList<FilaFactura> filas;
        filas.reserve(cat.cfdis.size());
        for (const CfdiMensual& m : cat.cfdis) {
            FilaFactura f;
            f.cfdi = FilaCfdi::desde(m.cfdi);
            f.solicitudId = m.solicitud.texto();
            const bool emitida = *dir == TipoDescarga::Emitidos;
            f.contraparteRfc = emitida ? m.cfdi.resumen.receptor.rfc : m.cfdi.resumen.emisor.rfc;
            f.contraparteNombre = emitida ? m.cfdi.resumen.receptor.nombre : m.cfdi.resumen.emisor.nombre;
            f.contraparte = f.contraparteNombre.trimmed().isEmpty() ? f.contraparteRfc : f.contraparteNombre;
            f.totalTexto = formatearTotalFactura(f.cfdi.total);
            QStringList detalle;
            for (const QString& p : {f.cfdi.moneda, f.cfdi.metodoPago}) {
                if (!p.trimmed().isEmpty()) {
                    detalle.append(p.trimmed());
                }
            }
            f.detalleTotal = detalle.join(QStringLiteral(" · "));
            const QString total = f.totalTexto.isEmpty() ? QStringLiteral("sin total")
                                                         : (f.totalTexto + (f.cfdi.moneda.isEmpty()
                                                                                ? QString()
                                                                                : QStringLiteral(" ") + f.cfdi.moneda));
            f.nombreAccesible = QStringLiteral("%1, %2, %3, %4")
                                    .arg(f.cfdi.tipoTexto,
                                         f.cfdi.fecha.isEmpty() ? QStringLiteral("sin fecha") : f.cfdi.fecha,
                                         f.contraparte.isEmpty() ? QStringLiteral("sin contraparte") : f.contraparte,
                                         total);
            filas.append(std::move(f));
        }
        m_modelo->reemplazar(std::move(filas));
        m_impl->listaCargada = true;
        m_impl->errorLista = false;
        m_impl->listaConFiltros = conFiltros;
        if (m_impl->conteoEmitidas != cat.conteos.emitidos || m_impl->conteoRecibidas != cat.conteos.recibidos) {
            m_impl->conteoEmitidas = cat.conteos.emitidos;
            m_impl->conteoRecibidas = cat.conteos.recibidos;
            emit conteosChanged();
        }
        emit vistaChanged();
    });
    if (cambioDeFiltro) {
        consultarConteosAnio(); // los conteos por mes siguen a la busqueda y el filtro
    }
}

void FacturasViewModel::consultarConteosAnio()
{
    const PerfilFacturas* p = m_impl->perfilActual();
    if (!m_fuente || !p || !m_impl->mes) {
        return;
    }
    // Conteo por mes de ambas pestanas con la busqueda y el filtro vigentes.
    ConsultaCatalogoMensual c;
    c.perfil = p->id;
    c.mes = *m_impl->mes;
    c.texto = m_texto;
    if (!m_tipo.isEmpty()) {
        c.tipo = tipoComprobanteDesdeTexto(m_tipo);
    }
    c.limite = 1;
    c.conConteosAnuales = true;
    const quint64 gen = m_impl->gen;
    const quint64 token = ++m_impl->genConteosAnio;
    const int anio = m_impl->mes->anio();
    m_fuente->consultar(c).then(this, [this, gen, token, anio](FuenteFacturasMes::ResultadoCatalogo r) {
        if (gen != m_impl->gen || token != m_impl->genConteosAnio || !m_impl->mes || m_impl->mes->anio() != anio
            || !r || !r.valor().conteos.porMes) {
            return;
        }
        m_impl->conteosAnio = *r.valor().conteos.porMes;
        m_impl->conteosAnioConocidos = true;
        actualizarMeses();
    });
}

void FacturasViewModel::consultarDisponibilidad(bool revalidar)
{
    const PerfilFacturas* p = m_impl->perfilActual();
    if (!m_fuente || !p || !m_impl->mes) {
        return;
    }
    const quint64 gen = m_impl->gen;
    const quint64 token = ++m_impl->genDisponibilidad;
    const MesCalendario mes = *m_impl->mes;
    ++m_impl->genOtrosMeses[mes.mes()]; // invalida consultas de "otro mes" pendientes para este
    m_fuente->disponibilidad(p->id, mes, revalidar)
        .then(this, [this, gen, token, mes](FuenteFacturasMes::ResultadoDisponibilidad r) {
            if (gen != m_impl->gen || token != m_impl->genDisponibilidad) {
                return;
            }
            if (!r) {
                m_impl->errorEvaluacion = true;
            } else {
                m_impl->errorEvaluacion = false;
                m_impl->evaluacion = r.valor();
                m_impl->evaluacionesAnio.insert(mes.mes(), r.valor());
            }
            emit vistaChanged();
            actualizarMeses();
            actualizarFranja();
            if (m_impl->reintentarAlCargar && r && m_impl->reintentarAlCargar->second == mes
                && m_impl->reintentarAlCargar->first == perfilId()) {
                m_impl->reintentarAlCargar.reset();
                const FranjaMesViewModel::Datos d = m_franja->datos();
                if (d.accion == AccionFranja::Reintentar && d.accionHabilitada) {
                    ejecutarAccion();
                }
            }
        });
}

void FacturasViewModel::consultarAnio()
{
    const PerfilFacturas* p = m_impl->perfilActual();
    if (!m_fuente || !p || !m_impl->mes) {
        return;
    }
    const quint64 gen = m_impl->gen;
    const int anio = m_impl->mes->anio();
    for (int m = 1; m <= 12; ++m) {
        const MesCalendario mes = *MesCalendario::crear(anio, m);
        if (mes == *m_impl->mes || esFuturo(mes)) {
            continue;
        }
        const quint64 token = ++m_impl->genOtrosMeses[m];
        m_fuente->disponibilidad(p->id, mes, false)
            .then(this, [this, gen, token, mes](FuenteFacturasMes::ResultadoDisponibilidad r) {
                if (gen != m_impl->gen || token != m_impl->genOtrosMeses.value(mes.mes()) || !r
                    || (m_impl->mes && *m_impl->mes == mes)) {
                    return;
                }
                m_impl->evaluacionesAnio.insert(mes.mes(), r.valor());
                actualizarMeses();
            });
    }
}

// ---- Derivados ------------------------------------------------------------------

void FacturasViewModel::actualizarMeses()
{
    QList<FilaMesFacturas> filas;
    if (!m_impl->mes || !m_impl->perfilActual()) {
        m_meses->reemplazar(filas);
        return;
    }
    const int anio = m_impl->mes->anio();
    for (int m = 1; m <= 12; ++m) {
        const MesCalendario mes = *MesCalendario::crear(anio, m);
        FilaMesFacturas f;
        f.anio = anio;
        f.mes = m;
        f.nombre = nombreMes(m);
        f.nombreCorto = QString::fromUtf8(kMesesCortos[std::size_t(m - 1)]);
        f.futuro = esFuturo(mes);
        f.seleccionado = mes == *m_impl->mes;
        EstadoFranja estado = EstadoFranja::Cargando;
        if (f.futuro) {
            estado = EstadoFranja::Futuro;
        } else if (const auto it = m_impl->evaluacionesAnio.constFind(m); it != m_impl->evaluacionesAnio.cend()) {
            estado = estadoFranja(*it);
        }
        const Presentacion pr = presentacion(estado);
        f.estado = pr.clave;
        f.estadoTexto = pr.texto;
        f.tono = pr.tono;
        f.icono = pr.icono;
        if (!f.futuro && m_impl->conteosAnioConocidos) {
            f.conteo = m_impl->conteosAnio[std::size_t(m - 1)];
            f.textoConteo = textoFacturas(f.conteo);
        }
        f.nombreAccesible = QStringLiteral("%1 %2, %3").arg(f.nombre).arg(anio).arg(f.estadoTexto);
        if (!f.textoConteo.isEmpty()) {
            f.nombreAccesible += QStringLiteral(", ") + f.textoConteo;
        }
        filas.append(std::move(f));
    }
    m_meses->reemplazar(std::move(filas));
}

void FacturasViewModel::actualizarFranja()
{
    FranjaMesViewModel::Datos d;
    const PerfilFacturas* p = m_impl->perfilActual();
    auto estado = [&d](EstadoFranja e) {
        const Presentacion pr = presentacion(e);
        d.estado = e;
        d.estadoTexto = pr.texto;
        d.tono = pr.tono;
        d.icono = pr.icono;
    };
    if (!p) {
        estado(m_impl->perfilesCargados ? EstadoFranja::SinPerfil : EstadoFranja::Cargando);
        d.titular = m_impl->perfilesCargados ? QStringLiteral("Agrega un perfil SAT para ver sus facturas")
                                             : QStringLiteral("Cargando perfiles");
        m_franja->fijar(d);
        return;
    }
    if (!m_impl->mes) {
        estado(EstadoFranja::Cargando);
        m_franja->fijar(d);
        return;
    }
    const MesCalendario mes = *m_impl->mes;
    const QString nombre = nombreMesAnio(mes);
    d.aviso = m_impl->aviso;
    d.ejecutando = m_impl->ejecutando;
    if (esFuturo(mes)) {
        estado(EstadoFranja::Futuro);
        d.titular = QStringLiteral("%1 aún no empieza").arg(capital(nombre));
        d.descripcion = QStringLiteral("Podrás descargar sus facturas cuando inicie el mes.");
        m_franja->fijar(d);
        anunciar(d);
        return;
    }
    if (m_impl->errorEvaluacion && !m_impl->evaluacion) {
        estado(EstadoFranja::Error);
        d.titular = QStringLiteral("No se pudo leer el estado de %1").arg(nombre);
        d.descripcion = QStringLiteral("Vuelve a intentarlo en un momento.");
        d.accion = AccionFranja::ReintentarLectura;
        d.textoAccion = textoAccion(d.accion);
        d.accionHabilitada = true;
        m_franja->fijar(d);
        return;
    }
    if (!m_impl->evaluacion) {
        estado(EstadoFranja::Cargando);
        d.titular = QStringLiteral("Cargando las facturas de %1").arg(nombre);
        m_franja->fijar(d);
        return;
    }
    const EvaluacionCoberturaMensual& e = *m_impl->evaluacion;
    const OfertaDescargaGuiada oferta = ofertaDescargaGuiada(e);
    const bool lista = eFirmaLista();
    const std::optional<QDateTime> hasta = hastaMinimo(e);
    if (hasta && e.tipoMes == TipoMes::EnCurso) {
        d.hasta = textoHasta(*hasta);
    }
    // Accion ofrecida por la regla del mes (o ninguna).
    auto ofrecer = [&](AccionFranja siNoHayOferta) {
        if (oferta.accion) {
            d.accion = accionDe(*oferta.accion);
        } else {
            d.accion = siNoHayOferta;
        }
        d.textoAccion = textoAccion(d.accion);
        d.accionHabilitada = oferta.accion.has_value() && !m_impl->ejecutando;
    };

    switch (estadoFranja(e)) {
    case EstadoFranja::SinDescargar:
        estado(EstadoFranja::SinDescargar);
        d.titular = QStringLiteral("Las facturas de %1 no están en este equipo").arg(nombre);
        d.descripcion = QStringLiteral(
            "Descarga del SAT las emitidas y recibidas del mes. Puede tardar de unos minutos a varias horas.");
        ofrecer(AccionFranja::DescargarFacturas);
        break;
    case EstadoFranja::Preparando:
        estado(EstadoFranja::Preparando);
        d.titular = QStringLiteral("Preparando las facturas de %1").arg(nombre);
        d.ocupado = true;
        d.etapa = m_impl->conteoEmitidas + m_impl->conteoRecibidas > 0
                      ? QStringLiteral("Descargando y organizando las facturas")
                      : QStringLiteral("Esperando a que el SAT prepare las facturas");
        d.descripcion = d.etapa;
        d.nota = QStringLiteral("Puedes cerrar la ventana; te avisaremos.");
        ofrecer(AccionFranja::DescargarFacturas);
        if (!oferta.accion) {
            d.motivoDeshabilitada = QStringLiteral("Ya se están preparando las facturas de este mes.");
        }
        break;
    case EstadoFranja::Disponibles:
        estado(EstadoFranja::Disponibles);
        if (e.tipoMes == TipoMes::EnCurso) {
            d.titular = QStringLiteral("Facturas disponibles hasta el %1").arg(d.hasta);
            d.descripcion = QStringLiteral("El mes sigue en curso. Mañana podrás actualizarlo para traer las facturas nuevas.");
        } else {
            d.titular = QStringLiteral("Facturas de %1 completas").arg(nombre);
            d.descripcion = QStringLiteral("Las emitidas y recibidas del mes están en este equipo.");
        }
        break;
    case EstadoFranja::DisponiblesParcialmente:
        estado(EstadoFranja::DisponiblesParcialmente);
        if (e.tipoMes == TipoMes::EnCurso) {
            d.titular = d.hasta.isEmpty() ? QStringLiteral("Faltan facturas de %1").arg(nombre)
                                          : QStringLiteral("Facturas disponibles hasta el %1").arg(d.hasta);
            d.descripcion = oferta.accion
                                ? QStringLiteral("El mes sigue en curso. Actualiza para traer las facturas desde esa hora hasta ahora.")
                                : QStringLiteral("El mes sigue en curso. Mañana podrás actualizarlo para traer las facturas nuevas.");
        } else {
            d.titular = QStringLiteral("Faltan facturas de %1").arg(nombre);
            const bool emitidasCompletas = e.emitidos.alcanzaFrontera && !e.recibidos.alcanzaFrontera;
            const bool recibidasCompletas = e.recibidos.alcanzaFrontera && !e.emitidos.alcanzaFrontera;
            if (emitidasCompletas || recibidasCompletas) {
                d.descripcion = QStringLiteral("Las %1 ya están en el equipo. Completa el mes para traer las %2.")
                                    .arg(emitidasCompletas ? QStringLiteral("emitidas") : QStringLiteral("recibidas"),
                                         emitidasCompletas ? QStringLiteral("recibidas") : QStringLiteral("emitidas"));
            } else if (hasta) {
                d.descripcion = QStringLiteral("Hay facturas hasta el %1. Completa el mes para traer las del resto de %2.")
                                    .arg(textoHasta(*hasta), nombreMes(mes.mes()));
            } else {
                d.descripcion = QStringLiteral("Completa el mes para traer las facturas que faltan.");
            }
        }
        if (oferta.accion) {
            ofrecer(AccionFranja::Ninguna);
        }
        break;
    case EstadoFranja::RequiereAtencion: {
        estado(EstadoFranja::RequiereAtencion);
        const CausaMensual c = *e.causa;
        d.causa = textoCausa(c);
        d.titular = QStringLiteral("No se pudieron obtener todas las facturas de %1").arg(nombre);
        d.textoEnlace = kVerDetalles;
        if (oferta.accion && c.causa != CausaAtencion::Codigo5005) {
            d.descripcion = d.causa + QStringLiteral(" Reintenta; si el problema sigue, revisa los detalles técnicos.");
            d.accion = AccionFranja::Reintentar;
            d.textoAccion = textoAccion(d.accion);
            d.accionHabilitada = !m_impl->ejecutando;
        } else {
            d.descripcion = d.causa;
            d.accion = AccionFranja::VerDetalles;
            d.textoAccion = textoAccion(d.accion);
            d.accionHabilitada = true;
            d.textoEnlace.clear(); // el boton ya lleva al diagnostico
        }
        break;
    }
    case EstadoFranja::Futuro:
    case EstadoFranja::Cargando:
    case EstadoFranja::Error:
    case EstadoFranja::SinPerfil:
        break;
    }

    // e.firma no lista: facturas visibles; la descarga se sustituye por
    // "Registrar e.firma..." con el motivo.
    const bool esDescarga = d.accion == AccionFranja::DescargarFacturas || d.accion == AccionFranja::ActualizarMes
                            || d.accion == AccionFranja::CompletarMes || d.accion == AccionFranja::Reintentar;
    if (esDescarga && d.accionHabilitada && !lista) {
        d.accion = AccionFranja::RegistrarEFirma;
        d.textoAccion = textoAccion(d.accion);
        d.accionHabilitada = true;
        d.motivoDeshabilitada = kMotivoEFirma;
    }
    if (esDescarga && !d.accionHabilitada && d.motivoDeshabilitada.isEmpty() && m_impl->ejecutando) {
        d.motivoDeshabilitada = QStringLiteral("La descarga se está iniciando.");
    }
    m_franja->fijar(d);
    anunciar(d);
}

void FacturasViewModel::anunciar(const FranjaMesViewModel::Datos& d)
{
    const QString clave = perfilId() + QLatin1Char('|') + (m_impl->mes ? m_impl->mes->texto() : QString());
    if (clave != m_impl->claveAnunciada) {
        // Cambio de seleccion: no se anuncia (la vista ya lo muestra).
        m_impl->claveAnunciada = clave;
        m_impl->estadoAnunciado = d.estado;
        return;
    }
    if (m_impl->estadoAnunciado == d.estado) {
        return;
    }
    m_impl->estadoAnunciado = d.estado;
    m_anuncio = d.estadoTexto + QStringLiteral(". ") + d.titular;
    emit anuncioChanged();
}

// ---- Accion de la franja ---------------------------------------------------------

void FacturasViewModel::ejecutarAccion()
{
    const FranjaMesViewModel::Datos d = m_franja->datos();
    if (!d.accionHabilitada) {
        return;
    }
    switch (d.accion) {
    case AccionFranja::Ninguna:
        return;
    case AccionFranja::RegistrarEFirma:
        emit registrarEFirmaSolicitado(perfilId());
        return;
    case AccionFranja::VerDetalles:
        verDetallesTecnicos();
        return;
    case AccionFranja::ReintentarLectura:
        recargar();
        return;
    case AccionFranja::DescargarFacturas:
    case AccionFranja::ActualizarMes:
    case AccionFranja::CompletarMes:
    case AccionFranja::Reintentar:
        break;
    }
    if (!m_impl->evaluacion || m_impl->ejecutando) {
        return;
    }
    const auto ofrecida = ofertaDescargaGuiada(*m_impl->evaluacion).accion;
    if (!ofrecida) {
        return;
    }
    m_impl->replanes = 0;
    planificar(*ofrecida);
}

void FacturasViewModel::planificar(AccionGuiada accion)
{
    const PerfilFacturas* p = m_impl->perfilActual();
    if (!m_fuente || !p || !m_impl->mes) {
        return;
    }
    m_impl->ejecutando = true;
    m_impl->aviso.clear();
    actualizarFranja();
    const quint64 gen = m_impl->gen;
    m_fuente->planificar(p->id, *m_impl->mes, accion)
        .then(this, [this, gen](FuenteFacturasMes::ResultadoPreplan r) {
            if (gen != m_impl->gen) {
                return;
            }
            if (!r) {
                terminarConError(r.error());
                return;
            }
            m_impl->preplan = std::move(r).valor();
            m_impl->decisiones = {};
            m_impl->porConfirmar = m_impl->preplan->direccionesPorConfirmar();
            emit confirmacionChanged();
            if (m_impl->porConfirmar.isEmpty()) {
                ejecutarPreplan();
            }
        });
}

void FacturasViewModel::confirmar()
{
    decidir(DecisionConfirmacion::Confirmar);
}

void FacturasViewModel::cancelarConfirmacion()
{
    decidir(DecisionConfirmacion::Cancelar);
}

void FacturasViewModel::decidir(DecisionConfirmacion decision)
{
    if (m_impl->porConfirmar.isEmpty() || !m_impl->preplan) {
        return;
    }
    const TipoDescarga d = m_impl->porConfirmar.takeFirst();
    (d == TipoDescarga::Emitidos ? m_impl->decisiones.emitidos : m_impl->decisiones.recibidos) = decision;
    emit confirmacionChanged();
    if (m_impl->porConfirmar.isEmpty()) {
        ejecutarPreplan();
    }
}

void FacturasViewModel::ejecutarPreplan()
{
    if (!m_fuente || !m_impl->preplan) {
        return;
    }
    // Todo cancelado y nada mas que crear o reenviar: no hay nada que hacer.
    const quint64 gen = m_impl->gen;
    m_fuente->ejecutar(*m_impl->preplan, m_impl->decisiones)
        .then(this, [this, gen](FuenteFacturasMes::ResultadoEjecucion r) {
            if (gen != m_impl->gen) {
                return;
            }
            if (!r) {
                const ErrorDescargaGuiada e = r.error();
                if (e.tipo == ErrorDescargaGuiada::Tipo::PlanDesactualizado && m_impl->replanes < 1 && m_impl->evaluacion
                    && e.preplanActual) {
                    // La evidencia cambio entre planificar y ejecutar: se
                    // vuelve a planificar una vez con la oferta vigente.
                    ++m_impl->replanes;
                    m_impl->evaluacion = e.preplanActual->evaluacion;
                    const auto ofrecida = ofertaDescargaGuiada(*m_impl->evaluacion).accion;
                    if (ofrecida) {
                        planificar(*ofrecida);
                        return;
                    }
                }
                terminarConError(e);
                return;
            }
            const EjecucionGuiada& ej = r.valor();
            m_impl->ejecutando = false;
            m_impl->preplan.reset();
            if (!ej.canceladas.isEmpty()) {
                QStringList omitidas;
                for (TipoDescarga d : ej.canceladas) {
                    omitidas.append(textoDireccion(d));
                }
                m_impl->aviso = QStringLiteral("Se omitieron las %1.").arg(omitidas.join(QStringLiteral(" y las ")));
            }
            actualizarFranja();
            recargarMes(false);
        });
}

void FacturasViewModel::terminarConError(const ErrorDescargaGuiada& e)
{
    m_impl->ejecutando = false;
    m_impl->preplan.reset();
    m_impl->porConfirmar.clear();
    emit confirmacionChanged();
    switch (e.tipo) {
    case ErrorDescargaGuiada::Tipo::EFirmaNoLista:
        m_impl->aviso = kMotivoEFirma;
        cargarPerfiles(); // refresca el estado de la e.firma
        break;
    case ErrorDescargaGuiada::Tipo::AccionNoDisponible:
    case ErrorDescargaGuiada::Tipo::PlanDesactualizado:
        m_impl->aviso = QStringLiteral("El estado del mes cambió; revisa la franja antes de continuar.");
        if (e.preplanActual) {
            m_impl->evaluacion = e.preplanActual->evaluacion;
        }
        break;
    case ErrorDescargaGuiada::Tipo::PerfilNoDisponible:
        m_impl->aviso = QStringLiteral("El perfil ya no está disponible.");
        cargarPerfiles();
        break;
    case ErrorDescargaGuiada::Tipo::MesFuturo:
        m_impl->aviso = QStringLiteral("Este mes aún no empieza.");
        break;
    case ErrorDescargaGuiada::Tipo::ConfirmacionPendiente:
    case ErrorDescargaGuiada::Tipo::Creacion:
    case ErrorDescargaGuiada::Tipo::Persistencia:
        m_impl->aviso = QStringLiteral("No se pudo iniciar la descarga. Inténtalo de nuevo.");
        break;
    }
    actualizarFranja();
    recargarMes(false);
}

void FacturasViewModel::verDetallesTecnicos()
{
    if (m_impl->perfilActual() && m_impl->mes) {
        emit detallesTecnicosSolicitados(perfilId(), m_impl->mes->anio(), m_impl->mes->mes(),
                                         solicitudDiagnostico());
    }
}

QString FacturasViewModel::solicitudDiagnostico() const
{
    if (!m_impl->evaluacion) {
        return {};
    }
    const EvaluacionCoberturaMensual& e = *m_impl->evaluacion;
    if (e.causa) {
        return e.causa->solicitud.texto();
    }
    const TipoDescarga activa = m_direccion == Direccion::Emitidas ? TipoDescarga::Emitidos : TipoDescarga::Recibidos;
    const TipoDescarga otra = activa == TipoDescarga::Emitidos ? TipoDescarga::Recibidos : TipoDescarga::Emitidos;
    for (TipoDescarga d : {activa, otra}) {
        const CoberturaDireccion& c = e.cobertura(d);
        if (!c.solicitudesEnCurso.isEmpty()) {
            return c.solicitudesEnCurso.constLast().texto();
        }
        if (!c.solicitudesAcreditadas.isEmpty()) {
            return c.solicitudesAcreditadas.constLast().texto();
        }
        for (const CausaMensual& s : e.causasSuperadas) {
            if (s.direccion == d) {
                return s.solicitud.texto();
            }
        }
    }
    return {};
}

void FacturasViewModel::irAMes(const QString& perfilId, int anio, int mes, AlLlegar alLlegar)
{
    m_impl->destinoPendiente = Impl::Destino{perfilId, anio, mes, alLlegar};
    if (m_impl->perfilesCargados) {
        aplicarDestinoPendiente();
    }
}

void FacturasViewModel::aplicarDestinoPendiente()
{
    if (!m_impl->destinoPendiente) {
        return;
    }
    const Impl::Destino d = *std::exchange(m_impl->destinoPendiente, std::nullopt);
    if (!seleccionarPerfilId(d.perfil)) {
        return; // perfil inexistente: se conserva la seleccion vigente, sin error
    }
    if (!seleccionarMes(d.anio, d.mes)) {
        return; // mes futuro o invalido: el perfil con su mes vigente
    }
    if (d.alLlegar == AlLlegar::Reintentar && m_impl->mes) {
        m_impl->reintentarAlCargar = std::pair{perfilId(), *m_impl->mes};
        // Si la disponibilidad del mes ya estaba cargada (mismo mes), se
        // vuelve a pedir para ejecutar la accion con datos frescos.
        consultarDisponibilidad(false);
    }
}

bool FacturasViewModel::previsualizar(int fila)
{
    const FilaFactura* f = m_modelo->filaEn(fila);
    if (!f) {
        return false;
    }
    const auto solicitud = SolicitudId::desdeTexto(f->solicitudId);
    if (!solicitud) {
        return false;
    }
    const FilaCfdi copia = f->cfdi;
    m_visor->abrir(*solicitud, copia);
    return true;
}

void FacturasViewModel::limpiarFiltros()
{
    const bool cambio = hayFiltros();
    m_texto.clear();
    m_tipo.clear();
    m_retardo.stop();
    if (cambio) {
        emit filtrosChanged();
        consultarLista(true);
    }
}

} // namespace satcfdi
