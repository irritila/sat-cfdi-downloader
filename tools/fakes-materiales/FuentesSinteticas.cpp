#include "FuentesSinteticas.h"

#include "domain/cfdi/PlanDescargaGuiada.h"

#include <array>

namespace materiales {

namespace {

bool coincide(const CfdiListado& cfdi, const ConsultaCatalogoMensual& c)
{
    if (c.tipo && cfdi.resumen.tipo != *c.tipo) {
        return false;
    }
    const QString t = c.texto.trimmed();
    if (t.isEmpty()) {
        return true;
    }
    const ResumenCfdi& x = cfdi.resumen;
    for (const QString& campo : {x.emisor.rfc, x.emisor.nombre, x.receptor.rfc, x.receptor.nombre, x.uuid, x.serie,
                                 x.folio}) {
        if (campo.contains(t, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

// Cobertura completa de una direccion hasta la frontera.
void completar(CoberturaDireccion& d, const QDateTime& frontera)
{
    for (const HuecoCobertura& h : std::as_const(d.huecos)) {
        d.tramos.append(h.tramo);
    }
    d.huecos.clear();
    d.hasta = frontera;
    d.alcanzaFrontera = true;
}

} // namespace

FuenteFacturasSintetica::FuenteFacturasSintetica(DatosFacturas datos)
    : m_datos(std::move(datos))
{
}

FuenteFacturasSintetica::~FuenteFacturasSintetica() = default;

const PerfilDeclarado* FuenteFacturasSintetica::perfil(const PerfilId& id) const
{
    for (const PerfilDeclarado& p : m_datos.perfiles) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

void FuenteFacturasSintetica::fijarMes(const PerfilId& id, int anio, int mes, const MesDeclarado& declarado)
{
    for (PerfilDeclarado& p : m_datos.perfiles) {
        if (p.id == id) {
            p.meses.insert(claveMes(anio, mes), declarado);
        }
    }
    emit datosCambiados(id);
}

MesDeclarado FuenteFacturasSintetica::mes(const PerfilId& id, MesCalendario mes) const
{
    const PerfilDeclarado* p = perfil(id);
    return p ? p->meses.value(claveMes(mes.anio(), mes.mes())) : MesDeclarado{};
}

QList<CfdiMensual> FuenteFacturasSintetica::facturas(const PerfilId& id, MesCalendario mes,
                                                     std::optional<TipoDescarga> direccion) const
{
    const PerfilDeclarado* p = perfil(id);
    if (!p) {
        return {};
    }
    const MesDeclarado m = this->mes(id, mes);
    QList<CfdiMensual> r;
    if (!direccion || *direccion == TipoDescarga::Emitidos) {
        r += cfdisDelMes(*p, mes.anio(), mes.mes(), TipoDescarga::Emitidos, m.emitidas);
    }
    if (!direccion || *direccion == TipoDescarga::Recibidos) {
        r += cfdisDelMes(*p, mes.anio(), mes.mes(), TipoDescarga::Recibidos, m.recibidas);
    }
    if (!direccion) {
        std::stable_sort(r.begin(), r.end(), [](const CfdiMensual& a, const CfdiMensual& b) {
            return a.cfdi.resumen.fecha > b.cfdi.resumen.fecha;
        });
    }
    return r;
}

QFuture<FuenteFacturasSintetica::ResultadoPerfiles> FuenteFacturasSintetica::perfiles()
{
    if (proveedorPerfiles) {
        return QtFuture::makeReadyValueFuture(ResultadoPerfiles::exito(proveedorPerfiles()));
    }
    QList<PerfilFacturas> r;
    for (const PerfilDeclarado& p : m_datos.perfiles) {
        r.append(PerfilFacturas{p.id, p.rfc, p.nombre, p.credencial});
    }
    return QtFuture::makeReadyValueFuture(ResultadoPerfiles::exito(r));
}

QFuture<FuenteFacturasSintetica::ResultadoCatalogo> FuenteFacturasSintetica::consultar(const ConsultaCatalogoMensual& c)
{
    CatalogoMensual cat;
    std::array<int, 12> porMes{};
    for (int m = 1; m <= 12; ++m) {
        const MesCalendario mes = *MesCalendario::crear(c.mes.anio(), m);
        for (const CfdiMensual& f : facturas(c.perfil, mes, std::nullopt)) {
            if (!coincide(f.cfdi, c)) {
                continue;
            }
            const bool enDireccion = !c.direccion || *c.direccion == f.direccion;
            if (enDireccion) {
                ++porMes[std::size_t(m - 1)];
            }
            if (m == c.mes.mes()) {
                (f.direccion == TipoDescarga::Emitidos ? cat.conteos.emitidos : cat.conteos.recibidos) += 1;
                if (enDireccion) {
                    cat.cfdis.append(f);
                }
            }
        }
    }
    cat.total = int(cat.cfdis.size());
    if (c.desplazamiento > 0) {
        cat.cfdis = cat.cfdis.mid(c.desplazamiento);
    }
    if (c.limite > 0) {
        cat.cfdis = cat.cfdis.mid(0, c.limite);
    }
    if (c.conConteosAnuales) {
        cat.conteos.porMes = porMes;
    }
    return QtFuture::makeReadyValueFuture(ResultadoCatalogo::exito(cat));
}

EvaluacionCoberturaMensual FuenteFacturasSintetica::evaluacion(const PerfilId& id, MesCalendario mes) const
{
    EvaluacionCoberturaMensual e = evaluarCoberturaMensual(id, mes, m_datos.ahoraUtc, {});
    if (e.tipoMes == TipoMes::Futuro) {
        return e;
    }
    const MesDeclarado m = this->mes(id, mes);
    switch (m.estado) {
    case EstadoMesSintetico::Disponibles:
        completar(e.emitidos, e.frontera);
        completar(e.recibidos, e.frontera);
        e.estado = EstadoMes::Disponibles;
        break;
    case EstadoMesSintetico::DisponiblesParcialmente:
        completar(e.emitidos, e.frontera);
        e.estado = EstadoMes::DisponiblesParcialmente;
        break;
    case EstadoMesSintetico::RequiereAtencion: {
        CoberturaDireccion& sana = m.direccionCausa == TipoDescarga::Emitidos ? e.recibidos : e.emitidos;
        completar(sana, e.frontera);
        const CausaMensual causa{m.causa, m.direccionCausa, solicitudIdFija(claveMes(mes.anio(), mes.mes()) * 10 + 7)};
        e.causa = causa;
        e.causas = {causa};
        e.estado = EstadoMes::RequiereAtencion;
        break;
    }
    case EstadoMesSintetico::Preparando: {
        int n = 0;
        for (CoberturaDireccion* d : {&e.emitidos, &e.recibidos}) {
            const SolicitudId reserva = solicitudIdFija(claveMes(mes.anio(), mes.mes()) * 10 + 3 + n++);
            for (HuecoCobertura& h : d->huecos) {
                h.reservadoPor = {reserva};
                h.libres.clear();
            }
        }
        e.estado = EstadoMes::Preparando;
        break;
    }
    case EstadoMesSintetico::SinDescargar:
        break;
    }
    return e;
}

QFuture<FuenteFacturasSintetica::ResultadoDisponibilidad>
FuenteFacturasSintetica::disponibilidad(const PerfilId& id, MesCalendario mes, bool)
{
    return QtFuture::makeReadyValueFuture(ResultadoDisponibilidad::exito(evaluacion(id, mes)));
}

QFuture<FuenteFacturasSintetica::ResultadoPreplan> FuenteFacturasSintetica::planificar(const PerfilId& id,
                                                                                      MesCalendario mes,
                                                                                      AccionGuiada accion)
{
    PreplanGuiado p;
    p.perfil = id;
    p.mes = mes;
    p.accion = accion;
    p.instante = m_datos.ahoraUtc;
    p.evaluacion = evaluacion(id, mes);
    p.oferta = ofertaDescargaGuiada(p.evaluacion);
    if (auto plan = planificarDescargaGuiada(p.evaluacion, accion)) {
        p.plan = plan.valor();
    }
    // Un candidato por direccion del mes; las declaradas piden confirmacion.
    const QString inicio = QStringLiteral("%1-01T00:00:00").arg(mes.texto());
    const QString fin = QDate(mes.anio(), mes.mes(), 1).addMonths(1).addDays(-1).toString(Qt::ISODate)
                        + QStringLiteral("T23:59:59");
    int n = 0;
    for (TipoDescarga d : {TipoDescarga::Emitidos, TipoDescarga::Recibidos}) {
        CandidatoGuiado c;
        c.direccion = d;
        c.fechaInicialSat = inicio;
        c.fechaFinalSat = fin;
        if (m_datos.preplan.porConfirmar.contains(d)) {
            c.duplicado.clasificacion = ClasificacionDuplicado::RequiereConfirmacion;
            c.duplicado.solicitudReferencia = solicitudIdFija(claveMes(mes.anio(), mes.mes()) * 10 + 5 + n);
        }
        ++n;
        p.candidatos.append(c);
    }
    return QtFuture::makeReadyValueFuture(ResultadoPreplan::exito(p));
}

QFuture<FuenteFacturasSintetica::ResultadoEjecucion> FuenteFacturasSintetica::ejecutar(const PreplanGuiado& preplan,
                                                                                      const DecisionesGuiadas&)
{
    if (m_datos.solicitudesCreadasAlEjecutar) {
        EjecucionGuiada e;
        if (*m_datos.solicitudesCreadasAlEjecutar > 0) {
            e.loteId = QStringLiteral("lote-%1").arg(preplan.mes.texto());
        }
        for (int i = 0; i < *m_datos.solicitudesCreadasAlEjecutar; ++i) {
            e.creadas.append(solicitudIdFija(claveMes(preplan.mes.anio(), preplan.mes.mes()) * 10 + 8 + i));
        }
        e.encoladas = e.creadas;
        return QtFuture::makeReadyValueFuture(ResultadoEjecucion::exito(e));
    }
    m_pendientes.push_back(std::make_unique<QPromise<ResultadoEjecucion>>());
    m_pendientes.back()->start();
    return m_pendientes.back()->future();
}

void HistorialSintetico::fijarHistorial(const PerfilId& perfil, QList<EntradaHistorial> lista)
{
    entradas.insert(perfil.texto(), std::move(lista));
    emit datosCambiados(perfil);
}

QFuture<HistorialSintetico::ResultadoEntradas> HistorialSintetico::historial(const PerfilId& perfil)
{
    return QtFuture::makeReadyValueFuture(
        ResultadoEntradas::exito(proveedor ? proveedor(perfil) : entradas.value(perfil.texto())));
}

} // namespace materiales
