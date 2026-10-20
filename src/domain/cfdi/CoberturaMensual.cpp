#include "domain/cfdi/CoberturaMensual.h"

#include "domain/solicitudes/OperacionSat.h"

#include <QTime>

#include <algorithm>

namespace satcfdi {

namespace {

// Clasificacion de una solicitud canonica: puede a la vez tener causas de
// atencion y trabajo en curso (p. ej. un paquete Error y otro Descargando).
struct Clasificacion {
    QList<CausaAtencion> causas;
    bool preparando = false;
    bool acredita = false;
};

void clasificarPaquetes(const EvidenciaSolicitudMensual& e, Clasificacion& c)
{
    if (e.paquetes.isEmpty()) {
        // Terminada sin paquetes: solo cuenta si la verificacion SAT persistio
        // el conjunto vacio; si no, los paquetes aun no se registraron.
        c.acredita = e.solicitud.conjuntoPaquetesVacioConfirmadoEn.has_value();
        return;
    }
    bool completos = true;
    for (const EvidenciaPaqueteMensual& p : e.paquetes) {
        switch (p.estadoDescarga) {
        case EstadoDescarga::Disponible:
        case EstadoDescarga::Descargando:
            c.preparando = true;
            completos = false;
            continue;
        case EstadoDescarga::Error:
            c.causas.append(CausaAtencion::PaqueteError);
            completos = false;
            continue;
        case EstadoDescarga::Vencido:
            c.causas.append(CausaAtencion::PaqueteVencido);
            completos = false;
            continue;
        case EstadoDescarga::Descargado:
            break;
        }
        if (!p.indiceLegible) {
            c.causas.append(CausaAtencion::IndiceIlegible);
            completos = false;
            continue;
        }
        if (p.indiceVigente && p.indice == EstadoIndicePaquete::Indexado) {
            continue;
        }
        completos = false;
        if (p.indiceVigente && p.indice == EstadoIndicePaquete::OrigenNoDisponible) {
            c.causas.append(CausaAtencion::ZipNoDisponible);
        } else if (p.indiceVigente && p.indice == EstadoIndicePaquete::Ilegible) {
            c.causas.append(CausaAtencion::ZipIlegible);
        } else if (e.indexacionProgramada) {
            // Sin fila, Pendiente o version de parser distinta.
            c.preparando = true;
        } else {
            c.causas.append(CausaAtencion::IndexacionSinProgramar);
        }
    }
    c.acredita = completos;
}

Clasificacion clasificar(const EvidenciaSolicitudMensual& e)
{
    Clasificacion c;
    const SolicitudPersistida& s = e.solicitud;
    for (const std::optional<QString>& codigo : {s.codEstatusSolicitud, s.codigoEstadoSolicitud}) {
        if (codigo && codigo->trimmed() == u"5002") {
            c.causas.append(CausaAtencion::Codigo5002);
        } else if (codigo && codigo->trimmed() == u"5005") {
            c.causas.append(CausaAtencion::Codigo5005);
        }
    }
    switch (s.estadoLocal) {
    case EstadoLocal::Creada:
    case EstadoLocal::Enviando:
        c.preparando = true;
        return c;
    case EstadoLocal::EnvioFallido:
        c.causas.append(CausaAtencion::EnvioFallido);
        return c;
    case EstadoLocal::EnvioIncierto:
        c.causas.append(CausaAtencion::EnvioIncierto);
        return c;
    case EstadoLocal::Enviada:
        break;
    }
    if (!s.estadoSolicitudSat) {
        c.preparando = true;
        return c;
    }
    switch (*s.estadoSolicitudSat) {
    case EstadoSolicitudSat::Aceptada:
    case EstadoSolicitudSat::EnProceso:
        c.preparando = true;
        return c;
    case EstadoSolicitudSat::Error:
        c.causas.append(CausaAtencion::ErrorSat);
        return c;
    case EstadoSolicitudSat::Rechazada:
        c.causas.append(CausaAtencion::Rechazada);
        return c;
    case EstadoSolicitudSat::Vencida:
        c.causas.append(CausaAtencion::Vencida);
        return c;
    case EstadoSolicitudSat::Terminada:
        break;
    }
    clasificarPaquetes(e, c);
    // Una solicitud con causas o trabajo no acredita aunque sus paquetes lo
    // permitan (p. ej. 5005 registrado en una Terminada).
    c.acredita = c.acredita && c.causas.isEmpty() && !c.preparando;
    return c;
}

using Tramo = TramoMensual;

bool menorTramo(const Tramo& a, const Tramo& b)
{
    return a.inicio != b.inicio ? a.inicio < b.inicio : a.fin < b.fin;
}

// Union en componentes maximos (contiguos por segundo o traslapados).
QList<Tramo> unir(QList<Tramo> tramos)
{
    std::sort(tramos.begin(), tramos.end(), menorTramo);
    QList<Tramo> unidos;
    for (const Tramo& t : std::as_const(tramos)) {
        if (!unidos.isEmpty() && t.inicio <= unidos.last().fin.addSecs(1)) {
            if (t.fin > unidos.last().fin) {
                unidos.last().fin = t.fin;
            }
        } else {
            unidos.append(t);
        }
    }
    return unidos;
}

// `unidos` (resultado de unir) cubre integramente `t`.
bool cubreIntegro(const QList<Tramo>& unidos, const Tramo& t)
{
    for (const Tramo& u : unidos) {
        if (u.inicio <= t.inicio && u.fin >= t.fin) {
            return true;
        }
    }
    return false;
}

bool intersecta(const Tramo& a, const Tramo& b)
{
    return a.inicio <= b.fin && b.inicio <= a.fin;
}

void calcularCobertura(const QList<Tramo>& acreditados, const QDateTime& inicioMes, CoberturaDireccion& cobertura)
{
    cobertura.tramosAcreditados = int(acreditados.size());
    cobertura.tramos = unir(acreditados);
    if (!cobertura.tramos.isEmpty() && cobertura.tramos.first().inicio <= inicioMes) {
        cobertura.hasta = cobertura.tramos.first().fin;
    }
    int contiguos = 0;
    for (const Tramo& t : acreditados) {
        if (cobertura.hasta && t.inicio <= *cobertura.hasta) {
            ++contiguos;
        }
    }
    cobertura.tramosTrasHueco = cobertura.tramosAcreditados - contiguos;
}

// Complemento de `cobertura.tramos` dentro de [desde, hasta].
void calcularHuecos(const QDateTime& desde, const QDateTime& hasta, CoberturaDireccion& cobertura)
{
    QDateTime siguiente = desde; // primer segundo aun no examinado
    auto agregar = [&](const QDateTime& fin) {
        if (siguiente <= fin) {
            HuecoCobertura h;
            h.tramo = Tramo{siguiente, fin};
            for (const TramoEnCurso& e : std::as_const(cobertura.enCurso)) {
                if (intersecta(e.tramo, h.tramo)) {
                    h.reservadoPor.append(e.solicitud);
                }
            }
            cobertura.huecos.append(h);
        }
    };
    for (const Tramo& t : std::as_const(cobertura.tramos)) {
        if (t.inicio > hasta) {
            break;
        }
        agregar(t.inicio.addSecs(-1));
        if (t.fin.addSecs(1) > siguiente) {
            siguiente = t.fin.addSecs(1);
        }
    }
    agregar(hasta);
}

bool menorSolicitud(const EvidenciaSolicitudMensual* a, const EvidenciaSolicitudMensual* b)
{
    if (a->solicitud.creadaEn != b->solicitud.creadaEn) {
        return a->solicitud.creadaEn < b->solicitud.creadaEn;
    }
    return a->solicitud.id.texto() < b->solicitud.id.texto();
}

} // namespace

QLatin1StringView claveEstable(EstadoMes estado)
{
    switch (estado) {
    case EstadoMes::RequiereAtencion:
        return QLatin1StringView("RequiereAtencion");
    case EstadoMes::Preparando:
        return QLatin1StringView("Preparando");
    case EstadoMes::Disponibles:
        return QLatin1StringView("Disponibles");
    case EstadoMes::DisponiblesParcialmente:
        return QLatin1StringView("DisponiblesParcialmente");
    case EstadoMes::SinDescargar:
        return QLatin1StringView("SinDescargar");
    }
    return QLatin1StringView("SinDescargar");
}

QLatin1StringView claveEstable(CausaAtencion causa)
{
    switch (causa) {
    case CausaAtencion::Codigo5002:
        return QLatin1StringView("Codigo5002");
    case CausaAtencion::Codigo5005:
        return QLatin1StringView("Codigo5005");
    case CausaAtencion::EnvioIncierto:
        return QLatin1StringView("EnvioIncierto");
    case CausaAtencion::EnvioFallido:
        return QLatin1StringView("EnvioFallido");
    case CausaAtencion::ErrorSat:
        return QLatin1StringView("ErrorSat");
    case CausaAtencion::Rechazada:
        return QLatin1StringView("Rechazada");
    case CausaAtencion::Vencida:
        return QLatin1StringView("Vencida");
    case CausaAtencion::PaqueteError:
        return QLatin1StringView("PaqueteError");
    case CausaAtencion::PaqueteVencido:
        return QLatin1StringView("PaqueteVencido");
    case CausaAtencion::ZipNoDisponible:
        return QLatin1StringView("ZipNoDisponible");
    case CausaAtencion::ZipIlegible:
        return QLatin1StringView("ZipIlegible");
    case CausaAtencion::IndiceIlegible:
        return QLatin1StringView("IndiceIlegible");
    case CausaAtencion::IndexacionSinProgramar:
        return QLatin1StringView("IndexacionSinProgramar");
    }
    return QLatin1StringView("IndexacionSinProgramar");
}

QLatin1StringView claveEstable(TipoMes tipo)
{
    switch (tipo) {
    case TipoMes::Cerrado:
        return QLatin1StringView("Cerrado");
    case TipoMes::EnCurso:
        return QLatin1StringView("EnCurso");
    case TipoMes::Futuro:
        return QLatin1StringView("Futuro");
    }
    return QLatin1StringView("Cerrado");
}

bool esSolicitudCanonicaDelMes(const SolicitudPersistida& s, const PerfilId& perfil, MesCalendario mes)
{
    if (s.eliminadoEn || perfil.esNulo() || s.perfilSatId != perfil) {
        return false;
    }
    if (s.tipoSolicitudSat != u"CFDI" || s.estadoComprobanteSat != u"Vigente"
        || s.operacionSat != operacionPara(s.tipoCfdi)) {
        return false;
    }
    if (s.tipoComprobante || s.complemento || !s.rfcReceptores.isEmpty()) {
        return false;
    }
    const bool sinContraparte = s.tipoCfdi == TipoDescarga::Emitidos
                                    ? (s.rfcEmisor == s.rfcSolicitante && !s.rfcReceptor)
                                    : (s.rfcReceptor == s.rfcSolicitante && !s.rfcEmisor);
    if (!sinContraparte) {
        return false;
    }
    const auto inicio = centro::desdeFechaSat(s.fechaInicialSat);
    const auto fin = centro::desdeFechaSat(s.fechaFinalSat);
    const auto inicioMes = centro::desdeFechaSat(mes.inicioSat());
    const auto finMes = centro::desdeFechaSat(mes.finSat());
    return inicio && fin && *inicio <= *fin && *inicio >= *inicioMes && *fin <= *finMes;
}

EvaluacionCoberturaMensual evaluarCoberturaMensual(const PerfilId& perfil, MesCalendario mes,
                                                   const QDateTime& instante,
                                                   const QList<EvidenciaSolicitudMensual>& evidencia)
{
    EvaluacionCoberturaMensual r;
    r.mes = mes;
    r.instanteCentro = centro::aParedCentro(instante);
    r.emitidos.direccion = TipoDescarga::Emitidos;
    r.recibidos.direccion = TipoDescarga::Recibidos;

    const QDateTime inicioMes = *centro::desdeFechaSat(mes.inicioSat());
    // Fuera del dominio de MesCalendario el instante es anterior a 0001-01 o
    // posterior a 9999-12: se compara por fecha civil.
    const QDate hoy = r.instanteCentro.date();
    const std::optional<MesCalendario> enCurso = MesCalendario::de(hoy);
    if (enCurso ? mes < *enCurso : hoy > mes.ultimoDia()) {
        r.tipoMes = TipoMes::Cerrado;
        r.frontera = *centro::desdeFechaSat(mes.finSat());
    } else if (enCurso && mes == *enCurso) {
        r.tipoMes = TipoMes::EnCurso;
        r.frontera = QDateTime(r.instanteCentro.date(), QTime(0, 0), QTimeZone(QTimeZone::UTC));
    } else {
        r.tipoMes = TipoMes::Futuro;
        r.frontera = inicioMes;
    }

    // Orden estable: creada_en, id.
    QList<const EvidenciaSolicitudMensual*> ordenadas;
    ordenadas.reserve(evidencia.size());
    for (const EvidenciaSolicitudMensual& e : evidencia) {
        ordenadas.append(&e);
    }
    std::sort(ordenadas.begin(), ordenadas.end(), menorSolicitud);

    struct Canonica {
        const SolicitudPersistida* solicitud;
        Clasificacion clasificacion;
        Tramo tramo;
    };
    QList<Canonica> canonicas;
    for (const EvidenciaSolicitudMensual* e : std::as_const(ordenadas)) {
        const SolicitudPersistida& s = e->solicitud;
        if (!esSolicitudCanonicaDelMes(s, perfil, mes)) {
            if (!s.eliminadoEn && s.perfilSatId == perfil) {
                ++r.solicitudesNoCanonicas;
            }
            continue;
        }
        canonicas.append(Canonica{&s, clasificar(*e),
                                  Tramo{*centro::desdeFechaSat(s.fechaInicialSat), *centro::desdeFechaSat(s.fechaFinalSat)}});
    }

    QList<Tramo> tramosEmitidos;
    QList<Tramo> tramosRecibidos;
    bool hayTrabajo = false;
    for (qsizetype i = 0; i < canonicas.size(); ++i) {
        const Canonica& c = canonicas.at(i);
        const SolicitudPersistida& s = *c.solicitud;
        CoberturaDireccion& cobertura = s.tipoCfdi == TipoDescarga::Emitidos ? r.emitidos : r.recibidos;
        if (!c.clasificacion.causas.isEmpty()) {
            // ADR 0020: superada si acreditadas POSTERIORES (orden creada_en,
            // id) de la misma direccion cubren integramente su tramo.
            QList<Tramo> posteriores;
            for (qsizetype j = i + 1; j < canonicas.size(); ++j) {
                const Canonica& p = canonicas.at(j);
                if (p.clasificacion.acredita && p.solicitud->tipoCfdi == s.tipoCfdi) {
                    posteriores.append(p.tramo);
                }
            }
            QList<CausaMensual>& destino = cubreIntegro(unir(posteriores), c.tramo) ? r.causasSuperadas : r.causas;
            for (CausaAtencion causa : c.clasificacion.causas) {
                destino.append(CausaMensual{causa, s.tipoCfdi, s.id});
            }
        }
        if (c.clasificacion.preparando) {
            hayTrabajo = true;
            cobertura.solicitudesEnCurso.append(s.id);
            cobertura.enCurso.append(TramoEnCurso{s.id, c.tramo});
        }
        if (c.clasificacion.acredita) {
            cobertura.solicitudesAcreditadas.append(s.id);
            (s.tipoCfdi == TipoDescarga::Emitidos ? tramosEmitidos : tramosRecibidos).append(c.tramo);
        }
    }

    // Causas: prioridad, direccion (Emitidos primero) y orden estable de
    // solicitud (ya aplicado: stable_sort lo conserva).
    for (QList<CausaMensual>* lista : {&r.causas, &r.causasSuperadas}) {
        std::stable_sort(lista->begin(), lista->end(), [](const CausaMensual& a, const CausaMensual& b) {
            if (a.causa != b.causa) {
                return a.causa < b.causa;
            }
            return a.direccion < b.direccion;
        });
        lista->erase(std::unique(lista->begin(), lista->end()), lista->end());
    }

    // Objetivo de planeacion de huecos: fin del mes cerrado o el instante en
    // el mes en curso.
    const QDateTime limiteHuecos = r.tipoMes == TipoMes::Cerrado ? *centro::desdeFechaSat(mes.finSat()) : r.instanteCentro;
    for (auto [cobertura, tramos] : {std::pair{&r.emitidos, &tramosEmitidos}, std::pair{&r.recibidos, &tramosRecibidos}}) {
        calcularCobertura(*tramos, inicioMes, *cobertura);
        cobertura->alcanzaFrontera = r.tipoMes != TipoMes::Futuro && cobertura->hasta
                                     && *cobertura->hasta >= r.frontera;
        // T023 D7 / ADR 0019: en el mes en curso una cobertura que llega a hoy
        // alcanza la frontera y "Actualizar mes" no se ofrece el mismo dia, de
        // modo que no hay huecos que planear (cero huecos, no el tramo de hoy).
        if (r.tipoMes != TipoMes::Futuro && !cobertura->alcanzaFrontera) {
            calcularHuecos(inicioMes, limiteHuecos, *cobertura);
        }
    }

    if (!r.causas.isEmpty()) {
        r.estado = EstadoMes::RequiereAtencion;
        r.causa = r.causas.constFirst();
    } else if (hayTrabajo) {
        r.estado = EstadoMes::Preparando;
    } else if (r.emitidos.alcanzaFrontera && r.recibidos.alcanzaFrontera) {
        r.estado = EstadoMes::Disponibles;
    } else if (!r.emitidos.tramos.isEmpty() || !r.recibidos.tramos.isEmpty()) {
        r.estado = EstadoMes::DisponiblesParcialmente;
    } else {
        r.estado = EstadoMes::SinDescargar;
    }
    return r;
}

} // namespace satcfdi
