#pragma once

// T023.4: FuenteFacturasMes falsa (respuestas inmediatas desde datos en
// memoria y evaluaciones reales de evaluarCoberturaMensual) y preferencias en
// memoria, compartidas por TestFacturasViewModel y TestPresentacion.

#include "presentation/viewmodels/FuenteFacturasMes.h"

#include "domain/solicitudes/OperacionSat.h"

#include <QHash>
#include <QPromise>
#include <QTimeZone>

#include <array>
#include <functional>
#include <memory>

namespace pruebasfacturas {

using namespace satcfdi;


// Datos sinteticos: RFC genericos del SAT, nombres y UUID inventados.
const QString kRfcA = QStringLiteral("XAXX010101000");
const QString kRfcB = QStringLiteral("XEXX010101000");
const PerfilId kPerfilA = *PerfilId::desdeTexto(u"0f000000-0000-4000-8000-00000000000a");
const PerfilId kPerfilB = *PerfilId::desdeTexto(u"0f000000-0000-4000-8000-00000000000b");

inline QDateTime utc(int a, int m, int d, int h, int mi = 0)
{
    return QDateTime(QDate(a, m, d), QTime(h, mi), QTimeZone(QTimeZone::UTC));
}

inline int g_n = 0;

inline EvidenciaSolicitudMensual evidencia(TipoDescarga d, const QString& inicio, const QString& fin, const PerfilId& perfil = kPerfilA)
{
    EvidenciaSolicitudMensual e;
    SolicitudPersistida& s = e.solicitud;
    const int n = ++g_n;
    s.id = *SolicitudId::desdeTexto(QStringLiteral("5e000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')));
    s.perfilSatId = perfil;
    s.tipoCfdi = d;
    s.operacionSat = operacionPara(d);
    s.rfcSolicitante = kRfcA;
    (d == TipoDescarga::Emitidos ? s.rfcEmisor : s.rfcReceptor) = kRfcA;
    s.fechaInicialSat = inicio;
    s.fechaFinalSat = fin;
    s.estadoLocal = EstadoLocal::Enviada;
    s.estadoSolicitudSat = EstadoSolicitudSat::Terminada;
    s.creadaEn = utc(2026, 1, 1, 0).addSecs(n);
    s.conjuntoPaquetesVacioConfirmadoEn = s.creadaEn;
    return e;
}

inline EvidenciaSolicitudMensual completa(TipoDescarga d, MesCalendario m)
{
    return evidencia(d, m.inicioSat(), m.finSat());
}

inline EvidenciaSolicitudMensual enCurso(TipoDescarga d, MesCalendario m, EstadoLocal local = EstadoLocal::Enviada)
{
    EvidenciaSolicitudMensual e = completa(d, m);
    e.solicitud.estadoLocal = local;
    e.solicitud.estadoSolicitudSat = local == EstadoLocal::Enviada ? std::optional(EstadoSolicitudSat::EnProceso)
                                                                   : std::nullopt;
    e.solicitud.conjuntoPaquetesVacioConfirmadoEn.reset();
    return e;
}

inline EvidenciaSolicitudMensual fallida(TipoDescarga d, MesCalendario m, const char* codigo = "301")
{
    EvidenciaSolicitudMensual e = completa(d, m);
    e.solicitud.estadoLocal = EstadoLocal::EnvioFallido;
    e.solicitud.estadoSolicitudSat.reset();
    e.solicitud.codEstatusSolicitud = QString::fromLatin1(codigo);
    e.solicitud.conjuntoPaquetesVacioConfirmadoEn.reset();
    return e;
}

inline CfdiMensual cfdi(TipoDescarga d, int n, const QString& fecha, const QString& emisor, const QString& receptor,
                 TipoComprobante tipo = TipoComprobante::Ingreso)
{
    CfdiMensual c;
    c.direccion = d;
    c.solicitud = *SolicitudId::desdeTexto(u"5e000000-0000-4000-8000-0000000000ff");
    c.cfdi.paqueteId = QStringLiteral("pa-%1").arg(n);
    c.cfdi.posicion = n;
    ResumenCfdi& r = c.cfdi.resumen;
    r.estado = EstadoParseo::Completo;
    r.tipo = tipo;
    r.fecha = fecha;
    r.emisor.rfc = d == TipoDescarga::Emitidos ? kRfcA : kRfcB;
    r.emisor.nombre = emisor;
    r.receptor.rfc = d == TipoDescarga::Emitidos ? kRfcB : kRfcA;
    r.receptor.nombre = receptor;
    r.total = ImporteCfdi::desdeTexto(u"1160.00");
    r.moneda = QStringLiteral("MXN");
    r.metodoPago = QStringLiteral("PUE");
    r.uuid = QStringLiteral("AAAAAAAA-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0'));
    return c;
}

// Fuente falsa: respuestas inmediatas desde datos en memoria; evaluaciones
// reales de evaluarCoberturaMensual con la evidencia configurada por mes.
class FuenteFalsa final : public FuenteFacturasMes {
public:
    QDateTime ahora = utc(2026, 9, 15, 18); // 15 sep 2026 12:00 Centro
    QList<PerfilFacturas> listaPerfiles{{kPerfilA, kRfcA, QStringLiteral("Perfil A"), EstadoCredencial::Lista},
                                        {kPerfilB, kRfcB, QStringLiteral("Perfil B"), EstadoCredencial::Lista}};
    QHash<QString, QList<EvidenciaSolicitudMensual>> evidenciaPorMes; // "YYYY-MM"
    QHash<QString, QList<CfdiMensual>> cfdisPorMes;
    std::array<int, 12> porMes{};
    bool fallarCatalogo = false;
    // Planear/ejecutar: respuestas programadas (FIFO); sin programar -> exito vacio.
    QList<ResultadoPreplan> planes;
    QList<ResultadoEjecucion> ejecuciones;
    QList<AccionGuiada> accionesPlanificadas;
    QList<DecisionesGuiadas> decisionesEjecutadas;
    QList<ConsultaCatalogoMensual> consultas;
    int disponibilidades = 0;
    // Pendiente: la siguiente disponibilidad queda sin responder.
    QList<std::shared_ptr<QPromise<ResultadoDisponibilidad>>> disponibilidadesPendientes; // en orden de llamada
    QList<int> mesesPendientes; // mes de cada disponibilidad pendiente
    // Retiene las consultas con conteos anuales (para responder en desorden).
    bool retenerConteosAnio = false;
    QList<std::shared_ptr<QPromise<ResultadoCatalogo>>> conteosPendientes;
    bool retenerDisponibilidad = false;
    // Evaluacion fabricada a mano (como la del fixture de QML); nullptr = regla real.
    std::function<void(EvaluacionCoberturaMensual&)> ajustar;

    QFuture<ResultadoPerfiles> perfiles() override
    {
        return QtFuture::makeReadyValueFuture(ResultadoPerfiles::exito(listaPerfiles));
    }

    QFuture<ResultadoCatalogo> consultar(const ConsultaCatalogoMensual& c) override
    {
        consultas.append(c);
        if (fallarCatalogo) {
            return QtFuture::makeReadyValueFuture(ResultadoCatalogo::fallo(
                ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("disco"))));
        }
        CatalogoMensual r;
        const QString texto = c.texto.trimmed().toLower();
        for (const CfdiMensual& m : cfdisPorMes.value(c.mes.texto())) {
            const bool coincide = texto.isEmpty() || m.cfdi.resumen.emisor.nombre.toLower().contains(texto)
                                  || m.cfdi.resumen.receptor.nombre.toLower().contains(texto);
            if (!coincide || (c.tipo && m.cfdi.resumen.tipo != *c.tipo)) {
                continue;
            }
            (m.direccion == TipoDescarga::Emitidos ? r.conteos.emitidos : r.conteos.recibidos)++;
            if ((!c.direccion || *c.direccion == m.direccion) && (c.limite == 0 || r.cfdis.size() < c.limite)) {
                r.cfdis.append(m);
            }
        }
        r.total = int(r.cfdis.size());
        if (c.conConteosAnuales) {
            r.conteos.porMes = porMes;
            if (retenerConteosAnio) {
                auto promesa = std::make_shared<QPromise<ResultadoCatalogo>>();
                promesa->start();
                conteosPendientes.append(promesa);
                return promesa->future();
            }
        }
        return QtFuture::makeReadyValueFuture(ResultadoCatalogo::exito(r));
    }

    QFuture<ResultadoDisponibilidad> disponibilidad(const PerfilId& perfil, MesCalendario mes, bool) override
    {
        ++disponibilidades;
        auto e = evaluarCoberturaMensual(perfil, mes, ahora, evidenciaPorMes.value(mes.texto()));
        if (ajustar && mes.mes() == 8) {
            ajustar(e);
        }
        if (retenerDisponibilidad) {
            auto promesa = std::make_shared<QPromise<ResultadoDisponibilidad>>();
            promesa->start();
            disponibilidadesPendientes.append(promesa);
            mesesPendientes.append(mes.mes());
            return promesa->future();
        }
        return QtFuture::makeReadyValueFuture(ResultadoDisponibilidad::exito(e));
    }

    QFuture<ResultadoPreplan> planificar(const PerfilId& perfil, MesCalendario mes, AccionGuiada accion) override
    {
        accionesPlanificadas.append(accion);
        if (!planes.isEmpty()) {
            return QtFuture::makeReadyValueFuture(planes.takeFirst());
        }
        PreplanGuiado p;
        p.perfil = perfil;
        p.mes = mes;
        p.accion = accion;
        return QtFuture::makeReadyValueFuture(ResultadoPreplan::exito(p));
    }

    QFuture<ResultadoEjecucion> ejecutar(const PreplanGuiado&, const DecisionesGuiadas& d) override
    {
        decisionesEjecutadas.append(d);
        if (!ejecuciones.isEmpty()) {
            return QtFuture::makeReadyValueFuture(ejecuciones.takeFirst());
        }
        return QtFuture::makeReadyValueFuture(ResultadoEjecucion::exito({}));
    }

    QDateTime ahoraUtc() const override { return ahora; }
};

class PreferenciasMemoria final : public PreferenciasFacturas {
public:
    std::optional<QString> valor;
    std::optional<QString> ultimoPerfil() const override { return valor; }
    void setUltimoPerfil(const QString& id) override { valor = id; }
};


} // namespace pruebasfacturas
