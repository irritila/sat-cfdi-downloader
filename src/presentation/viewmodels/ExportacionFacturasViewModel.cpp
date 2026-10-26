#include "ExportacionFacturasViewModel.h"

#include "CatalogoMensajes.h"
#include "FacturasListModel.h"
#include "FacturasViewModel.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QVariantMap>

#include <utility>
#include <variant>

// T026.3: textos visibles sin "solicitud", "paquete" ni "verificacion"
// (TestExportacionFacturasViewModel::vocabulario). Sin logs (D20).

namespace satcfdi {

namespace {

QString nombreMesAnio(MesCalendario m)
{
    return QStringLiteral("%1 %2").arg(QLocale(QLocale::Spanish, QLocale::Mexico).monthName(m.mes())).arg(m.anio());
}

QString capital(QString s)
{
    if (!s.isEmpty()) {
        s[0] = s.at(0).toUpper();
    }
    return s;
}

} // namespace

ExportacionFacturasViewModel::ExportacionFacturasViewModel(QObject* parent)
    : QObject(parent)
{
}

ExportacionFacturasViewModel::~ExportacionFacturasViewModel() = default;

// ---- Puertos -------------------------------------------------------------------

void ExportacionFacturasViewModel::setServicio(ExportacionFacturasService* servicio, RelojUtc reloj)
{
    if (m_servicio) {
        disconnect(m_servicio, nullptr, this, nullptr);
    }
    m_servicio = servicio;
    m_reloj = reloj ? std::move(reloj) : relojSistema();
    if (servicio) {
        connect(servicio, &ExportacionFacturasService::progreso, this, &ExportacionFacturasViewModel::alProgreso);
        connect(servicio, &ExportacionFacturasService::destinoExistente, this,
                &ExportacionFacturasViewModel::alDestinoExistente);
    }
    emit estadoChanged();
}

void ExportacionFacturasViewModel::setFacturas(FacturasViewModel* facturas)
{
    if (m_facturas) {
        disconnect(m_facturas, nullptr, this, nullptr);
    }
    m_facturas = facturas;
    if (facturas) {
        connect(facturas, &FacturasViewModel::perfilChanged, this, &ExportacionFacturasViewModel::alCambiarPerfil);
        connect(facturas, &FacturasViewModel::mesChanged, this, &ExportacionFacturasViewModel::estadoChanged);
    }
    alCambiarPerfil();
}

// ---- Contexto de Facturas ------------------------------------------------------

std::optional<MesCalendario> ExportacionFacturasViewModel::mesEnCurso() const
{
    return centro::mesEnCurso(m_reloj());
}

std::optional<PerfilId> ExportacionFacturasViewModel::perfilFacturas() const
{
    return m_facturas ? PerfilId::desdeTexto(m_facturas->perfilId()) : std::nullopt;
}

std::optional<MesCalendario> ExportacionFacturasViewModel::mesFacturas() const
{
    return m_facturas ? MesCalendario::crear(m_facturas->anio(), m_facturas->mes()) : std::nullopt;
}

std::optional<TipoDescarga> ExportacionFacturasViewModel::tipoDireccion() const
{
    switch (m_direccion) {
    case Direccion::Emitidas: return TipoDescarga::Emitidos;
    case Direccion::Recibidas: return TipoDescarga::Recibidos;
    case Direccion::Ambas: break;
    }
    return std::nullopt; // Ambas
}

bool ExportacionFacturasViewModel::puedeExportar() const
{
    if (!m_servicio || m_estado != Estado::Inactivo || !perfilFacturas()) {
        return false;
    }
    const auto mes = mesFacturas();
    const auto curso = mesEnCurso();
    return mes && curso && horizonte_facturas::navegable(*mes, *curso);
}

// ---- Estado --------------------------------------------------------------------

void ExportacionFacturasViewModel::fijarEstado(Estado estado)
{
    if (m_estado == estado) {
        return;
    }
    m_estado = estado;
    emit estadoChanged();
    emit destinoChanged(); // puedeIniciar
}

void ExportacionFacturasViewModel::invalidarPlan()
{
    ++m_tokenPlan;
    const bool cambio = m_plan.has_value() || !m_errorPlan.isEmpty();
    m_plan.reset();
    m_errorPlan.clear();
    if (cambio) {
        emit planChanged();
        emit destinoChanged();
    }
}

void ExportacionFacturasViewModel::fijarErrorDestino(const QString& texto)
{
    if (m_errorDestino != texto) {
        m_errorDestino = texto;
        emit destinoChanged();
    }
}

// ---- Configuracion -------------------------------------------------------------

bool ExportacionFacturasViewModel::rangoValido() const
{
    if (m_errorRangoForzado) {
        return false;
    }
    const auto curso = mesEnCurso();
    return curso && horizonte_facturas::validarRango(m_desde, m_hasta, *curso) == horizonte_facturas::ErrorRango::Ninguno;
}

QVariantList ExportacionFacturasViewModel::opcionesMeses() const
{
    QVariantList r;
    const auto curso = mesEnCurso();
    if (!curso) {
        return r;
    }
    for (std::optional<MesCalendario> m = curso; m && horizonte_facturas::navegable(*m, *curso); m = m->anterior()) {
        r.append(QVariantMap{{QStringLiteral("anio"), m->anio()},
                             {QStringLiteral("mes"), m->mes()},
                             {QStringLiteral("texto"), capital(nombreMesAnio(*m))}});
    }
    return r;
}

int ExportacionFacturasViewModel::indiceOpcionMes(int anio, int mes) const
{
    const auto curso = mesEnCurso();
    const auto m = MesCalendario::crear(anio, mes);
    if (!curso || !m || !horizonte_facturas::navegable(*m, *curso)) {
        return -1;
    }
    return (curso->anio() - m->anio()) * 12 + (curso->mes() - m->mes());
}

QString ExportacionFacturasViewModel::errorRango() const
{
    if (m_modo != Modo::Rango || !dialogoAbierto()) {
        return {};
    }
    if (m_errorRangoForzado) {
        return catalogo::textoErrorRango(*m_errorRangoForzado);
    }
    const auto curso = mesEnCurso();
    if (!curso) {
        return catalogo::textoErrorRango(horizonte_facturas::ErrorRango::Futuro);
    }
    return catalogo::textoErrorRango(horizonte_facturas::validarRango(m_desde, m_hasta, *curso));
}

QString ExportacionFacturasViewModel::textoAlcance() const
{
    if (!dialogoAbierto()) {
        return {};
    }
    switch (m_modo) {
    case Modo::Mes: return capital(nombreMesAnio(m_mesAlcance));
    case Modo::Rango:
        if (m_desde == m_hasta) {
            return capital(nombreMesAnio(m_desde));
        }
        return tr("%1 a %2").arg(capital(nombreMesAnio(m_desde)), nombreMesAnio(m_hasta));
    case Modo::Seleccion:
        return conteoSeleccionadas() == 1 ? tr("1 factura elegida")
                                          : tr("%1 facturas elegidas").arg(conteoSeleccionadas());
    }
    return {};
}

void ExportacionFacturasViewModel::setDireccion(Direccion direccion)
{
    if (m_direccion == direccion || (dialogoAbierto() && !puedeConfigurar())) {
        return;
    }
    m_direccion = direccion;
    emit configuracionChanged();
    if (puedeConfigurar() && m_modo != Modo::Seleccion) {
        planear();
    }
}

void ExportacionFacturasViewModel::setConPdf(bool conPdf)
{
    if (m_conPdf == conPdf || m_estado == Estado::Exportando || m_estado == Estado::EsperandoNombre) {
        return;
    }
    m_conPdf = conPdf; // no cambia el plan
    emit configuracionChanged();
}

QString ExportacionFacturasViewModel::aviso() const
{
    return catalogo::avisoExportacion();
}

std::optional<AlcanceExportacion> ExportacionFacturasViewModel::alcanceSinValidar() const
{
    if (m_perfilAlcance.esNulo()) {
        return std::nullopt;
    }
    switch (m_modo) {
    case Modo::Mes: return AlcanceExportacion(AlcanceMesExportacion{m_perfilAlcance, m_mesAlcance, tipoDireccion()});
    case Modo::Rango:
        return AlcanceExportacion(AlcanceRangoExportacion{m_perfilAlcance, m_desde, m_hasta, tipoDireccion()});
    case Modo::Seleccion: {
        AlcanceSeleccionExportacion a{m_perfilAlcance, {}};
        if (m_perfilSeleccion == m_perfilAlcance) {
            for (const ReferenciaFactura& r : m_ordenSeleccion) {
                a.facturas.append(m_seleccion.value(r));
            }
        }
        return AlcanceExportacion(std::move(a));
    }
    }
    return std::nullopt;
}

std::optional<AlcanceExportacion> ExportacionFacturasViewModel::alcance() const
{
    if (m_modo == Modo::Rango && !rangoValido()) {
        return std::nullopt;
    }
    return alcanceSinValidar();
}

void ExportacionFacturasViewModel::actualizarNombreSugerido()
{
    const auto a = alcanceSinValidar();
    if (!a || (m_modo == Modo::Rango && !rangoValido())) {
        return;
    }
    const QString sugerido = nombreSugeridoZip(*a, centro::hoy(m_reloj()));
    if (m_nombreZip.trimmed().isEmpty() || m_nombreZip == m_nombreSugerido) {
        m_nombreZip = sugerido; // el usuario no lo cambio
    }
    m_nombreSugerido = sugerido;
    emit destinoChanged();
}

// ---- Apertura y plan -----------------------------------------------------------

void ExportacionFacturasViewModel::abrirMesActual()
{
    abrir(Modo::Mes);
}

void ExportacionFacturasViewModel::abrirRango()
{
    abrir(Modo::Rango);
}

void ExportacionFacturasViewModel::abrirSeleccion()
{
    abrir(Modo::Seleccion);
}

void ExportacionFacturasViewModel::abrir(Modo modo)
{
    if (!puedeExportar()) {
        return;
    }
    m_perfilAlcance = *perfilFacturas();
    m_modo = modo;
    m_mesAlcance = *mesFacturas();
    m_desde = m_mesAlcance;
    m_hasta = m_mesAlcance;
    m_errorRangoForzado.reset();
    m_direccion = m_facturas->direccion() == FacturasViewModel::Direccion::Recibidas ? Direccion::Recibidas
                                                                                      : Direccion::Emitidas;
    m_resultado.reset();
    m_errorDestino.clear();
    m_nombreZip.clear();
    m_nombreSugerido.clear();
    reiniciarProgreso();
    invalidarPlan();
    m_estado = Estado::Planeando;
    emit estadoChanged();
    emit configuracionChanged();
    emit resultadoChanged();
    actualizarNombreSugerido();
    planear();
}

void ExportacionFacturasViewModel::cambiarModo(Modo modo)
{
    if (!puedeConfigurar() || m_modo == modo) {
        return;
    }
    if (modo == Modo::Rango) {
        m_desde = m_mesAlcance;
        m_hasta = m_mesAlcance;
        m_errorRangoForzado.reset();
    }
    m_modo = modo;
    emit configuracionChanged();
    actualizarNombreSugerido();
    planear();
}

void ExportacionFacturasViewModel::fijarRango(int desdeAnio, int desdeMes, int hastaAnio, int hastaMes)
{
    if (!puedeConfigurar() || m_modo != Modo::Rango) {
        return;
    }
    const auto desde = MesCalendario::crear(desdeAnio, desdeMes);
    const auto hasta = MesCalendario::crear(hastaAnio, hastaMes);
    if (!desde || !hasta) {
        m_errorRangoForzado = horizonte_facturas::ErrorRango::FueraDelHorizonte;
    } else {
        m_errorRangoForzado.reset();
        m_desde = *desde;
        m_hasta = *hasta;
    }
    emit configuracionChanged();
    actualizarNombreSugerido();
    planear();
}

void ExportacionFacturasViewModel::planear()
{
    if (!m_servicio || !puedeConfigurar()) {
        return;
    }
    invalidarPlan();
    fijarErrorDestino({});
    const auto a = alcance();
    if (!a) { // rango invalido: sin plan
        fijarEstado(Estado::Dialogo);
        return;
    }
    fijarEstado(Estado::Planeando);
    const quint64 token = m_tokenPlan;
    const PerfilId perfil = m_perfilAlcance;
    m_servicio->planear(*a)
        .then(this,
              [this, token, perfil](const ResultadoPlanExportacion& r) {
                  if (token != m_tokenPlan || m_estado != Estado::Planeando || perfil != m_perfilAlcance) {
                      return; // plan obsoleto (C1)
                  }
                  if (r) {
                      m_plan = r.valor();
                  } else {
                      m_errorPlan = catalogo::textoErrorPlanExportacion(r.error().codigo);
                  }
                  emit planChanged();
                  fijarEstado(Estado::Dialogo);
              })
        .onCanceled(this, [this, token] {
            if (token != m_tokenPlan || m_estado != Estado::Planeando) {
                return;
            }
            m_errorPlan = catalogo::textoErrorPlanExportacion(ErrorExportacion::Codigo::Interno);
            emit planChanged();
            fijarEstado(Estado::Dialogo);
        });
}

QString ExportacionFacturasViewModel::textoConteo() const
{
    return m_plan ? catalogo::textoConteoPrevisto(m_plan->conteoPrevisto()) : QString();
}

int ExportacionFacturasViewModel::omitidasPrevistas() const
{
    if (!m_plan) {
        return 0;
    }
    int n = 0;
    for (const OmisionExportacion& o : m_plan->omisiones) {
        n += std::holds_alternative<OmisionFacturaExportacion>(o) ? 1 : 0;
    }
    return n;
}

int ExportacionFacturasViewModel::lineasMesConAviso() const
{
    if (!m_plan) {
        return 0;
    }
    int n = 0;
    for (const OmisionExportacion& o : m_plan->omisiones) {
        n += std::holds_alternative<OmisionMesExportacion>(o) ? 1 : 0;
    }
    return n;
}

QStringList ExportacionFacturasViewModel::avisosPlan() const
{
    return m_plan ? catalogo::avisosPlanExportacion(omitidasPrevistas(), lineasMesConAviso()) : QStringList();
}

// ---- Destino -------------------------------------------------------------------

void ExportacionFacturasViewModel::setNombreZip(const QString& nombre)
{
    if (m_nombreZip == nombre || m_estado == Estado::Exportando) {
        return;
    }
    m_nombreZip = nombre;
    emit destinoChanged();
}

QString ExportacionFacturasViewModel::nombreCarpetaDestino() const
{
    if (m_carpetaDestino.isEmpty()) {
        return {};
    }
    const QString nombre = QFileInfo(m_carpetaDestino).fileName();
    return nombre.isEmpty() ? QStringLiteral("/") : nombre;
}

bool ExportacionFacturasViewModel::puedeIniciar() const
{
    return m_servicio && m_estado == Estado::Dialogo && m_plan && m_plan->conteoPrevisto() > 0
           && !m_carpetaDestino.isEmpty() && !m_nombreZip.trimmed().isEmpty();
}

void ExportacionFacturasViewModel::elegirDestino(const QUrl& carpeta)
{
    if (!puedeConfigurar()) {
        return;
    }
    if (!carpeta.isLocalFile() || carpeta.toLocalFile().isEmpty()) {
        fijarErrorDestino(tr("Elige una carpeta de este equipo."));
        return;
    }
    m_carpetaDestino = QDir::cleanPath(carpeta.toLocalFile());
    m_errorDestino.clear();
    emit destinoChanged();
}

QUrl ExportacionFacturasViewModel::carpetaInicial() const
{
    return m_carpetaDestino.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_carpetaDestino);
}

void ExportacionFacturasViewModel::iniciar()
{
    if (!puedeIniciar()) {
        return;
    }
    m_nombreZip = sanearNombreZip(m_nombreZip); // D15: el campo muestra el nombre efectivo
    m_errorDestino.clear();
    emit destinoChanged();
    reiniciarProgreso();
    const quint64 token = ++m_tokenTrabajo;
    fijarEstado(Estado::Exportando);
    m_servicio->iniciar(*m_plan, m_carpetaDestino, m_nombreZip, m_conPdf)
        .then(this, [this, token](const ResultadoExportacion& r) {
            if (token == m_tokenTrabajo) {
                alTerminar(r);
            }
        });
}

void ExportacionFacturasViewModel::publicarConNombre()
{
    if (!m_servicio || m_estado != Estado::EsperandoNombre || m_nombreZip.trimmed().isEmpty()) {
        return;
    }
    m_nombreZip = sanearNombreZip(m_nombreZip);
    emit destinoChanged();
    const auto r = m_servicio->publicarComo(m_nombreZip);
    if (!r) {
        fijarErrorDestino(catalogo::textoErrorPublicarExportacion(r.error().codigo));
        return;
    }
    m_errorDestino.clear();
    fijarEstado(Estado::Exportando); // emite destinoChanged
}

void ExportacionFacturasViewModel::alDestinoExistente()
{
    if (m_estado != Estado::Exportando) {
        return;
    }
    m_errorDestino = catalogo::textoDestinoExistente();
    fijarEstado(Estado::EsperandoNombre); // emite destinoChanged
}

// ---- Progreso y resultado ------------------------------------------------------

void ExportacionFacturasViewModel::reiniciarProgreso()
{
    m_textoFase.clear();
    m_actual = 0;
    m_total = 0;
    m_exportadasProgreso = 0;
    m_sinPdfProgreso = 0;
    m_omitidasProgreso = 0;
    m_preparando = false;
    emit progresoChanged();
}

void ExportacionFacturasViewModel::alProgreso(FaseExportacion fase, int actual, int total, int exportadas, int sinPdf,
                                              int omitidas)
{
    if (m_estado != Estado::Exportando) {
        return;
    }
    m_textoFase = catalogo::textoFase(fase);
    m_actual = actual;
    m_total = total;
    m_exportadasProgreso = exportadas;
    m_sinPdfProgreso = sinPdf;
    m_omitidasProgreso = omitidas;
    m_preparando = fase == FaseExportacion::Preparando;
    emit progresoChanged();
}

double ExportacionFacturasViewModel::fraccion() const
{
    if (m_total <= 0 || m_preparando) {
        return -1;
    }
    const double f = double(m_actual) / double(m_total);
    return f < 0 ? 0 : (f > 1 ? 1 : f);
}

QString ExportacionFacturasViewModel::textoProgreso() const
{
    return m_total > 0 ? tr("%1 de %2").arg(m_actual).arg(m_total) : QString();
}

void ExportacionFacturasViewModel::alTerminar(const ResultadoExportacion& resultado)
{
    reiniciarProgreso();
    // Carpeta o nombre que el usuario corrige en el dialogo (incluido
    // DestinoExistente al reservar): mismo plan y carpeta; iniciar() de nuevo.
    if (resultado.estado == ResultadoExportacion::Estado::Error && resultado.error
        && resultado.error->codigo == ErrorExportacion::Codigo::Destino && resultado.error->destino) {
        const QString corregible = catalogo::textoErrorDestinoCorregible(resultado.error->destino->codigo);
        if (!corregible.isEmpty()) {
            m_errorDestino = corregible;
            fijarEstado(Estado::Dialogo); // emite destinoChanged
            return;
        }
    }
    m_errorDestino.clear();
    m_resultado = resultado;
    emit resultadoChanged();
    fijarEstado(Estado::Resultado);
}

QString ExportacionFacturasViewModel::varianteResultado() const
{
    return m_resultado ? catalogo::mensajeExportacion(catalogo::resumenExportacion(*m_resultado)).variante : QString();
}

QString ExportacionFacturasViewModel::tituloResultado() const
{
    return m_resultado ? catalogo::mensajeExportacion(catalogo::resumenExportacion(*m_resultado)).titulo : QString();
}

QString ExportacionFacturasViewModel::descripcionResultado() const
{
    return m_resultado ? catalogo::mensajeExportacion(catalogo::resumenExportacion(*m_resultado)).descripcion
                       : QString();
}

QStringList ExportacionFacturasViewModel::detallesResultado() const
{
    return m_resultado ? catalogo::detallesExportacion(catalogo::resumenExportacion(*m_resultado)) : QStringList();
}

QString ExportacionFacturasViewModel::advertenciaResultado() const
{
    return m_resultado ? catalogo::advertenciaExportacion(catalogo::resumenExportacion(*m_resultado)) : QString();
}

// ---- Cancelar y cerrar ---------------------------------------------------------

void ExportacionFacturasViewModel::cancelar()
{
    switch (m_estado) {
    case Estado::Exportando:
    case Estado::EsperandoNombre:
        if (m_servicio) {
            m_servicio->cancelar(); // el resultado llega por el future de iniciar()
        }
        return;
    case Estado::Inactivo:
        return;
    case Estado::Planeando:
    case Estado::Dialogo:
    case Estado::Resultado:
        cerrarDialogo();
        return;
    }
}

void ExportacionFacturasViewModel::cerrarDialogo()
{
    if (m_estado == Estado::Inactivo || m_estado == Estado::Exportando || m_estado == Estado::EsperandoNombre) {
        return;
    }
    invalidarPlan();
    m_resultado.reset();
    m_errorDestino.clear();
    m_perfilAlcance = PerfilId();
    reiniciarProgreso();
    m_estado = Estado::Inactivo;
    emit estadoChanged();
    emit configuracionChanged();
    emit destinoChanged();
    emit resultadoChanged();
}

// ---- Seleccion -----------------------------------------------------------------

void ExportacionFacturasViewModel::iniciarSeleccion()
{
    if (!m_seleccionando) {
        m_seleccionando = true;
        emit seleccionChanged();
    }
}

void ExportacionFacturasViewModel::terminarSeleccion()
{
    if (dialogoAbierto()) {
        return;
    }
    const bool cambio = m_seleccionando || !m_seleccion.isEmpty();
    m_seleccionando = false;
    m_seleccion.clear();
    m_ordenSeleccion.clear();
    m_perfilSeleccion = PerfilId();
    if (cambio) {
        ++m_revisionSeleccion;
        emit seleccionChanged();
    }
}

bool ExportacionFacturasViewModel::alternarSeleccion(const QString& paqueteId, int posicion)
{
    // Con el dialogo abierto la seleccion queda fija (el plan mostrado es el que se exporta).
    if (dialogoAbierto() || !m_facturas) {
        return false;
    }
    const auto perfil = perfilFacturas();
    const auto mes = mesFacturas();
    if (!perfil || !mes) {
        return false;
    }
    const ReferenciaFactura ref{paqueteId, posicion};
    if (m_seleccion.contains(ref)) {
        m_seleccion.remove(ref);
        m_ordenSeleccion.removeOne(ref);
        if (m_seleccion.isEmpty()) {
            m_perfilSeleccion = PerfilId();
        }
        ++m_revisionSeleccion;
        emit seleccionChanged();
        return true;
    }
    const FilaFactura* fila = nullptr;
    for (const FilaFactura& f : m_facturas->modelo()->filas()) {
        if (f.cfdi.paqueteId == paqueteId && f.cfdi.posicion == posicion) {
            fila = &f;
            break;
        }
    }
    if (!fila) {
        return false;
    }
    if (m_perfilSeleccion != *perfil) { // salvaguarda: nunca mezcla perfiles
        m_seleccion.clear();
        m_ordenSeleccion.clear();
        m_perfilSeleccion = *perfil;
    }
    FacturaSeleccionada s;
    s.referencia = ref;
    s.mes = *mes;
    s.direccion = m_facturas->direccion() == FacturasViewModel::Direccion::Recibidas ? TipoDescarga::Recibidos
                                                                                      : TipoDescarga::Emitidos;
    s.uuid = fila->cfdi.uuid;
    m_seleccion.insert(ref, s);
    m_ordenSeleccion.append(ref);
    ++m_revisionSeleccion;
    emit seleccionChanged();
    return true;
}

bool ExportacionFacturasViewModel::estaSeleccionada(const QString& paqueteId, int posicion) const
{
    return m_seleccion.contains(ReferenciaFactura{paqueteId, posicion});
}

void ExportacionFacturasViewModel::limpiarSeleccion()
{
    if (dialogoAbierto() || m_seleccion.isEmpty()) {
        return;
    }
    m_seleccion.clear();
    m_ordenSeleccion.clear();
    m_perfilSeleccion = PerfilId();
    ++m_revisionSeleccion;
    emit seleccionChanged();
}

void ExportacionFacturasViewModel::alCambiarPerfil()
{
    const PerfilId perfil = perfilFacturas().value_or(PerfilId());
    if (!m_seleccion.isEmpty() && perfil != m_perfilSeleccion) {
        m_seleccion.clear(); // D14: cambiar de perfil vacia la seleccion
        m_ordenSeleccion.clear();
        m_perfilSeleccion = PerfilId();
        ++m_revisionSeleccion;
        emit seleccionChanged();
    }
    if (puedeConfigurar() && perfil != m_perfilAlcance) {
        cerrarDialogo(); // invalida el plan pendiente del perfil anterior
    }
    emit estadoChanged(); // puedeExportar
}

} // namespace satcfdi
