#include "application/cfdi/PlanificadorExportacion.h"

#include "domain/cfdi/CoberturaMensual.h"
#include "domain/cfdi/UuidCfdi.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <utility>

namespace satcfdi {

namespace {

template <class... F>
struct Visitante : F... {
    using F::operator()...;
};

// Factura planeada u omitida (con su motivo) antes de asignar nombres.
struct Entrada {
    FacturaPlaneada factura;
    std::optional<MotivoOmisionFactura> omision;
};

struct Candidata {
    const CfdiMensual* cfdi = nullptr;
    MesCalendario mes = *MesCalendario::crear(2000, 1);
};

// Indice de un mes de la instantanea.
struct MesIndexado {
    const MesInstantaneaExportacion* datos = nullptr;
    QHash<ReferenciaFactura, const CfdiMensual*> porReferencia;
    QHash<QString, EstadoDescarga> estadoPaquete; // de la evidencia
};

ReferenciaFactura referenciaDe(const CfdiMensual& c)
{
    return ReferenciaFactura{c.cfdi.paqueteId, c.cfdi.posicion};
}

bool aceptaDireccion(const std::optional<TipoDescarga>& pedida, TipoDescarga direccion)
{
    return !pedida || *pedida == direccion;
}

// Linea de mes de una direccion (ver PlanificadorExportacion.h).
std::optional<MotivoOmisionMes> lineaDeMes(const EvaluacionCoberturaMensual& e, TipoDescarga direccion)
{
    if (e.tipoMes == TipoMes::Futuro) {
        return std::nullopt;
    }
    for (const CausaMensual& c : e.causas) {
        if (c.direccion == direccion) {
            return MotivoOmisionMes::CoberturaIncompleta;
        }
    }
    const CoberturaDireccion& cobertura = e.cobertura(direccion);
    if (!cobertura.solicitudesEnCurso.isEmpty()) {
        return MotivoOmisionMes::DescargasPendientes;
    }
    if (!cobertura.alcanzaFrontera) {
        return MotivoOmisionMes::CoberturaIncompleta;
    }
    return std::nullopt;
}

} // namespace

PlanificadorExportacion::PlanificadorExportacion(CatalogoMensualRepository& catalogo, RelojUtc reloj,
                                                 int versionParser, IndexacionProgramada indexacion)
    : m_catalogo(catalogo)
    , m_reloj(std::move(reloj))
    , m_versionParser(versionParser)
    , m_indexacion(std::move(indexacion))
{
}

Resultado<PlanExportacion, ErrorPersistencia> PlanificadorExportacion::planear(const AlcanceExportacion& alcance) const
{
    using R = Resultado<PlanExportacion, ErrorPersistencia>;
    if (const auto* rango = std::get_if<AlcanceRangoExportacion>(&alcance); rango && rango->desde > rango->hasta) {
        return R::fallo(ErrorPersistencia::de(ErrorPersistencia::Tipo::Interno,
                                              QStringLiteral("exportacion.plan: rango invertido")));
    }
    const QList<MesCalendario> meses = mesesDe(alcance);
    if (perfilDe(alcance).esNulo() || meses.isEmpty()) {
        return R::exito(PlanExportacion{});
    }
    const QDateTime instante = m_reloj();
    auto instantanea = m_catalogo.capturarParaExportar(perfilDe(alcance), meses, m_versionParser);
    if (!instantanea) {
        return R::fallo(std::move(instantanea).error());
    }
    return R::exito(planificar(alcance, instantanea.valor(), instante, m_indexacion));
}

PlanExportacion PlanificadorExportacion::planificar(const AlcanceExportacion& alcance,
                                                    const InstantaneaExportacion& instantanea,
                                                    const QDateTime& instante, const IndexacionProgramada& indexacion)
{
    PlanExportacion plan;
    const PerfilId& perfil = perfilDe(alcance);
    if (perfil.esNulo()) {
        return plan;
    }

    QList<MesIndexado> indice;
    indice.reserve(instantanea.meses.size());
    for (const MesInstantaneaExportacion& m : instantanea.meses) {
        MesIndexado mi;
        mi.datos = &m;
        for (const CfdiMensual& c : m.cfdis) {
            mi.porReferencia.insert(referenciaDe(c), &c);
        }
        for (const EvidenciaSolicitudMensual& e : m.evidencia) {
            for (const EvidenciaPaqueteMensual& p : e.paquetes) {
                mi.estadoPaquete.insert(p.paqueteId, p.estadoDescarga);
            }
        }
        indice.append(std::move(mi));
    }
    const auto buscarMes = [&indice](MesCalendario mes) -> const MesIndexado* {
        for (const MesIndexado& m : indice) {
            if (m.datos->mes == mes) {
                return &m;
            }
        }
        return nullptr;
    };

    // 1. Candidatas segun el alcance.
    QList<Candidata> candidatas;
    QList<Entrada> entradas; // planeadas y omitidas, antes de ordenar
    const std::optional<TipoDescarga> direccion = std::visit(
        Visitante{
            [](const AlcanceMesExportacion& a) { return a.direccion; },
            [](const AlcanceRangoExportacion& a) { return a.direccion; },
            [](const AlcanceSeleccionExportacion&) { return std::optional<TipoDescarga>(); },
        },
        alcance);
    const bool esSeleccion = std::holds_alternative<AlcanceSeleccionExportacion>(alcance);
    if (const auto* seleccion = std::get_if<AlcanceSeleccionExportacion>(&alcance)) {
        QSet<ReferenciaFactura> vistas;
        for (const FacturaSeleccionada& f : seleccion->facturas) {
            if (vistas.contains(f.referencia)) {
                continue;
            }
            vistas.insert(f.referencia);
            const MesIndexado* mes = buscarMes(f.mes);
            const CfdiMensual* encontrado = mes ? mes->porReferencia.value(f.referencia, nullptr) : nullptr;
            const std::optional<QString> uuid = cfdi::normalizarUuid(f.uuid);
            // La identidad con otro UUID canonico ya no es la factura elegida
            // (p. ej. el paquete se reindexo con otro contenido): se trata
            // como si no existiera. Sin UUID canonico elegido basta la identidad.
            if (encontrado && uuid && cfdi::normalizarUuid(encontrado->cfdi.resumen.uuid) != uuid) {
                encontrado = nullptr;
            }
            if (!encontrado && mes && uuid) {
                for (const CfdiMensual& c : mes->datos->cfdis) {
                    if (c.direccion == f.direccion && cfdi::normalizarUuid(c.cfdi.resumen.uuid) == uuid) {
                        encontrado = &c;
                        break;
                    }
                }
            }
            if (encontrado) {
                candidatas.append(Candidata{encontrado, f.mes});
            } else {
                FacturaPlaneada omitida;
                omitida.origen = f.referencia;
                omitida.mes = f.mes;
                omitida.direccion = f.direccion;
                omitida.uuidCanonico = uuid;
                entradas.append(Entrada{std::move(omitida), MotivoOmisionFactura::YaNoDisponible});
            }
        }
    } else {
        for (const MesCalendario mes : mesesDe(alcance)) {
            const MesIndexado* m = buscarMes(mes);
            if (!m) {
                continue;
            }
            for (const CfdiMensual& c : m->datos->cfdis) {
                if (aceptaDireccion(direccion, c.direccion)) {
                    candidatas.append(Candidata{&c, mes});
                }
            }
        }
    }

    // 2. Facturas planeadas y omitidas (D4) en orden determinista; nombres
    //    D18 con un solo asignador en ese orden, de modo que una omision sin
    //    UUID tiene su propio `sin-uuid-<n>` y nunca repite el de otra factura.
    for (const Candidata& c : std::as_const(candidatas)) {
        const MesIndexado* m = buscarMes(c.mes);
        FacturaPlaneada f;
        f.origen = referenciaDe(*c.cfdi);
        f.mes = c.mes;
        f.direccion = c.cfdi->direccion;
        f.uuidCanonico = cfdi::normalizarUuid(c.cfdi->cfdi.resumen.uuid);
        f.solicitud = c.cfdi->solicitud;
        // Un paquete ausente de la evidencia no se descarta (su solicitud
        // puede no intersectar el mes); solo se omite un estado conocido.
        if (m) {
            const auto estado = m->estadoPaquete.constFind(c.cfdi->cfdi.paqueteId);
            if (estado != m->estadoPaquete.cend() && *estado != EstadoDescarga::Descargado) {
                entradas.append(Entrada{std::move(f), MotivoOmisionFactura::NoDisponibleEnEsteEquipo});
                continue;
            }
        }
        // Sin ruta o sin huella del ZIP no se puede revalidar la relectura.
        const auto fuente = instantanea.paquetes.constFind(c.cfdi->cfdi.paqueteId);
        if (fuente == instantanea.paquetes.cend() || fuente->rutaLocal.isEmpty() || !fuente->huella) {
            entradas.append(Entrada{std::move(f), MotivoOmisionFactura::NoDisponibleEnEsteEquipo});
            continue;
        }
        f.rutaPaquete = fuente->rutaLocal;
        f.huellaPaquete = *fuente->huella;
        entradas.append(Entrada{std::move(f), std::nullopt});
    }
    std::stable_sort(entradas.begin(), entradas.end(), [](const Entrada& x, const Entrada& y) {
        const FacturaPlaneada& a = x.factura;
        const FacturaPlaneada& b = y.factura;
        if (a.direccion != b.direccion) {
            return a.direccion < b.direccion;
        }
        if (a.mes != b.mes) {
            return a.mes < b.mes;
        }
        if (a.uuidCanonico.has_value() != b.uuidCanonico.has_value()) {
            return a.uuidCanonico.has_value(); // sin UUID al final
        }
        if (a.uuidCanonico && *a.uuidCanonico != *b.uuidCanonico) {
            return *a.uuidCanonico < *b.uuidCanonico;
        }
        if (a.origen.paqueteId != b.origen.paqueteId) {
            return a.origen.paqueteId < b.origen.paqueteId;
        }
        return a.origen.posicion < b.origen.posicion;
    });
    AsignadorNombresExportacion asignador;
    QSet<QString> uuids;
    QSet<ReferenciaFactura> referencias;
    for (Entrada& e : entradas) {
        FacturaPlaneada& f = e.factura;
        if (!e.omision) {
            if (referencias.contains(f.origen) || (f.uuidCanonico && uuids.contains(*f.uuidCanonico))) {
                continue;
            }
            referencias.insert(f.origen);
            if (f.uuidCanonico) {
                uuids.insert(*f.uuidCanonico);
            }
        }
        f.nombre = asignador.siguiente(f.direccion, f.mes, f.uuidCanonico.value_or(QString()));
        if (e.omision) {
            plan.omisiones.append(OmisionFacturaExportacion{f.direccion, f.mes, f.nombre.base, *e.omision});
        } else {
            plan.facturas.append(std::move(f));
        }
    }

    // 3. Lineas de mes (solo mes o rango).
    if (!esSeleccion) {
        for (const MesCalendario mes : mesesDe(alcance)) {
            const MesIndexado* m = buscarMes(mes);
            QList<EvidenciaSolicitudMensual> evidencia = m ? m->datos->evidencia : QList<EvidenciaSolicitudMensual>();
            if (indexacion) {
                for (EvidenciaSolicitudMensual& e : evidencia) {
                    e.indexacionProgramada = indexacion(e.solicitud.id);
                }
            }
            const EvaluacionCoberturaMensual evaluacion =
                evaluarCoberturaMensual(perfil, mes, instante, evidencia);
            for (const TipoDescarga d : {TipoDescarga::Emitidos, TipoDescarga::Recibidos}) {
                if (!aceptaDireccion(direccion, d)) {
                    continue;
                }
                if (const auto motivo = lineaDeMes(evaluacion, d)) {
                    plan.omisiones.append(OmisionMesExportacion{d, mes, *motivo});
                }
            }
        }
    }
    return plan;
}

} // namespace satcfdi
