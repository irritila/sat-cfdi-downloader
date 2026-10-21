#include "app_core/FuenteFacturasRoot.h"

#include "application/cfdi/CatalogoMensualCfdiService.h"
#include "application/cfdi/DescargaGuiadaService.h"
#include "application/profiles/ConsultaPreparacionPerfiles.h"

#include <utility>

namespace satcfdi {

namespace {

std::optional<EstadoCredencial> credencialDe(PreparacionPerfil p)
{
    switch (p) {
    case PreparacionPerfil::Verificando: return EstadoCredencial::Validando;
    case PreparacionPerfil::SinCredencial: return EstadoCredencial::SinCredencial;
    case PreparacionPerfil::Lista: return EstadoCredencial::Lista;
    case PreparacionPerfil::Vencida: return EstadoCredencial::Vencida;
    case PreparacionPerfil::NoVigenteAun: return EstadoCredencial::NoVigenteAun;
    case PreparacionPerfil::MaterialFaltante: return EstadoCredencial::MaterialFaltante;
    case PreparacionPerfil::MaterialDanado: return EstadoCredencial::MaterialDanado;
    case PreparacionPerfil::EstadoNoDisponible: break;
    }
    return std::nullopt;
}

const QString kClaveUltimoPerfil = QStringLiteral("facturas/ultimoPerfil");

} // namespace

FuenteFacturasRoot::FuenteFacturasRoot(CatalogoMensualCfdiService& catalogo, DescargaGuiadaService& descarga,
                                       ConsultaPreparacionPerfiles& preparacion, RelojUtc reloj, QObject* parent)
    : FuenteFacturasMes(parent)
    , m_catalogo(catalogo)
    , m_descarga(descarga)
    , m_preparacion(preparacion)
    , m_reloj(std::move(reloj))
{
    m_agrupar.setSingleShot(true);
    m_agrupar.setInterval(150);
    connect(&m_agrupar, &QTimer::timeout, this, [this] { emit datosCambiados(PerfilId()); });
    connect(&m_descarga, &DescargaGuiadaService::mesActualizado, this, [this] { avisarCambio(); });
}

void FuenteFacturasRoot::avisarCambio()
{
    if (!m_agrupar.isActive()) {
        m_agrupar.start();
    }
}

QFuture<FuenteFacturasMes::ResultadoPerfiles> FuenteFacturasRoot::perfiles()
{
    return m_preparacion.listarVerificados().then(this, [](ConsultaPreparacionPerfiles::ResultadoLista r) {
        if (!r) {
            return ResultadoPerfiles::fallo(std::move(r).error());
        }
        QList<PerfilFacturas> lista;
        for (const PerfilConPreparacion& p : r.valor()) {
            if (!p.perfil.activo) {
                continue;
            }
            lista.append(PerfilFacturas{p.perfil.id, p.perfil.rfc, p.perfil.nombre, credencialDe(p.preparacion)});
        }
        return ResultadoPerfiles::exito(std::move(lista));
    });
}

QFuture<FuenteFacturasMes::ResultadoCatalogo> FuenteFacturasRoot::consultar(const ConsultaCatalogoMensual& consulta)
{
    return m_catalogo.consultar(consulta);
}

QFuture<FuenteFacturasMes::ResultadoDisponibilidad>
FuenteFacturasRoot::disponibilidad(const PerfilId& perfil, MesCalendario mes, bool revalidar)
{
    using R = CatalogoMensualCfdiService::Revalidacion;
    return m_catalogo.disponibilidad(perfil, mes, revalidar ? R::Si : R::No);
}

QFuture<FuenteFacturasMes::ResultadoPreplan> FuenteFacturasRoot::planificar(const PerfilId& perfil, MesCalendario mes,
                                                                            AccionGuiada accion)
{
    return m_descarga.planificar(perfil, mes, accion);
}

QFuture<FuenteFacturasMes::ResultadoEjecucion> FuenteFacturasRoot::ejecutar(const PreplanGuiado& preplan,
                                                                            const DecisionesGuiadas& decisiones)
{
    return m_descarga.ejecutar(preplan, decisiones);
}

FuenteHistorialRoot::FuenteHistorialRoot(CatalogoMensualCfdiService& catalogo, FuenteFacturasMes& cambios,
                                         QObject* parent)
    : FuenteHistorial(parent)
    , m_catalogo(catalogo)
{
    connect(&cambios, &FuenteFacturasMes::datosCambiados, this, &FuenteHistorial::datosCambiados);
    connect(&cambios, &FuenteFacturasMes::perfilesCambiados, this, [this] { emit datosCambiados(PerfilId()); });
}

QFuture<FuenteHistorial::ResultadoEntradas> FuenteHistorialRoot::historial(const PerfilId& perfil)
{
    return m_catalogo.historial(perfil);
}

DestinoNavegacion destinoDeUbicacion(const std::optional<UbicacionSolicitud>& ubicacion)
{
    if (!ubicacion) {
        return DestinoNavegacion::facturas();
    }
    if (ubicacion->mesLote) {
        return DestinoNavegacion::facturasMes(ubicacion->perfil, *ubicacion->mesLote);
    }
    return DestinoNavegacion::diagnostico(ubicacion->solicitud);
}

QFuture<DestinoNavegacion> FuenteHistorialRoot::resolver(const SolicitudId& solicitud)
{
    return m_catalogo.ubicar(solicitud).then(this, [](const CatalogoMensualCfdiService::ResultadoUbicacion& r) {
        return r ? destinoDeUbicacion(r.valor()) : DestinoNavegacion::facturas();
    });
}

PreferenciasFacturasArchivo::PreferenciasFacturasArchivo(const QString& rutaIni)
    : m_settings(rutaIni, QSettings::IniFormat)
{
}

std::optional<QString> PreferenciasFacturasArchivo::ultimoPerfil() const
{
    const QString v = m_settings.value(kClaveUltimoPerfil).toString();
    return v.isEmpty() ? std::nullopt : std::optional(v);
}

void PreferenciasFacturasArchivo::setUltimoPerfil(const QString& perfilId)
{
    m_settings.setValue(kClaveUltimoPerfil, perfilId);
    m_settings.sync();
}

} // namespace satcfdi
