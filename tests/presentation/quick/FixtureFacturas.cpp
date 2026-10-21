#include "FixtureFacturas.h"

#include "../ConsultaCatalogoFake.h"
#include "../ServiciosAsincronosFake.h"
#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakePerfilesSatService.h"

#include "FuenteFacturasMes.h"
#include "FuenteHistorial.h"
#include "PresentacionViewModels.h"
#include "domain/cfdi/CoberturaMensual.h"

#include <QDir>
#include <QHash>
#include <QPromise>
#include <QTimeZone>
#include <QFuture>

#include <array>

using namespace satcfdi;

namespace {

// Contrapartes sinteticas (ficticias) para nombres variados en la lista.
constexpr std::array<const char*, 8> kNombres = {
    "Servicios Contables Demo", "Arrendamientos Demo",     "Distribuidora Imaginaria", "Comercializadora Ejemplo",
    "Refacciones Ficticias",    "Telecomunicaciones Ejemplo", "Papelería Ficticia del Centro", "Transportes Muestra",
};

struct ConteoMes {
    int emitidas = 0;
    int recibidas = 0;
};

ConteoMes conteoPara(const QString& estado, int mes)
{
    if (estado == QLatin1String("Disponibles")) {
        return {18 + mes, 20 + 2 * mes};
    }
    if (estado == QLatin1String("Parcial") || estado == QLatin1String("Atencion")) {
        return {15 + mes, 0};
    }
    return {};
}

// Fuente sintetica: futures resueltos al instante salvo planificar/ejecutar.
class FuenteSintetica final : public FuenteFacturasMes {
public:
    explicit FuenteSintetica(bool conPerfiles)
        : m_conPerfiles(conPerfiles)
    {
        for (int m = 1; m <= 8; ++m) {
            m_estados.insert(m, QStringLiteral("Disponibles"));
        }
        m_estados.insert(9, QStringLiteral("SinDescargar"));
        m_estados.insert(10, QStringLiteral("Parcial"));
    }

    PerfilId perfilLista = PerfilId::generar();
    PerfilId perfilSinEFirma = PerfilId::generar();
    QHash<int, QString> m_estados;
    int planificaciones = 0;
    int consultas = 0;
    std::vector<std::unique_ptr<QPromise<ResultadoPreplan>>> preplanes;

    void cambiar() { emit datosCambiados(perfilLista); }

    QFuture<ResultadoPerfiles> perfiles() override
    {
        QList<PerfilFacturas> r;
        if (m_conPerfiles) {
            r.append({perfilLista, QStringLiteral("XAXX010101000"), QStringLiteral("Contribuyente de ejemplo"),
                      EstadoCredencial::Lista});
            r.append({perfilSinEFirma, QStringLiteral("XEXX010101000"), QStringLiteral("Proveedor de ejemplo"),
                      EstadoCredencial::SinCredencial});
        }
        return QtFuture::makeReadyValueFuture(ResultadoPerfiles::exito(r));
    }

    QFuture<ResultadoCatalogo> consultar(const ConsultaCatalogoMensual& c) override
    {
        ++consultas;
        const bool principal = c.perfil == perfilLista;
        auto incluye = [&c](const CfdiListado& cfdi) {
            if (c.tipo && cfdi.resumen.tipo != *c.tipo) {
                return false;
            }
            const QString t = c.texto.trimmed().toLower();
            if (t.isEmpty()) {
                return true;
            }
            for (const QString& campo : {cfdi.resumen.emisor.rfc, cfdi.resumen.emisor.nombre, cfdi.resumen.receptor.rfc,
                                         cfdi.resumen.receptor.nombre, cfdi.resumen.uuid, cfdi.resumen.folio}) {
                if (campo.toLower().contains(t)) {
                    return true;
                }
            }
            return false;
        };
        CatalogoMensual cat;
        std::array<int, 12> porMes{};
        for (int m = 1; m <= 12; ++m) {
            const auto filas = cfdisDe(principal, c.mes.anio(), m);
            for (const CfdiMensual& f : filas) {
                if (!incluye(f.cfdi)) {
                    continue;
                }
                if (!c.direccion || *c.direccion == f.direccion) {
                    ++porMes[std::size_t(m - 1)];
                }
                if (m == c.mes.mes()) {
                    (f.direccion == TipoDescarga::Emitidos ? cat.conteos.emitidos : cat.conteos.recibidos) += 1;
                    if (!c.direccion || *c.direccion == f.direccion) {
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

    QFuture<ResultadoDisponibilidad> disponibilidad(const PerfilId& perfil, MesCalendario mes, bool) override
    {
        EvaluacionCoberturaMensual e = evaluarCoberturaMensual(perfil, mes, ahoraUtc(), {});
        const QString estado = perfil == perfilLista && mes.anio() == 2026 ? m_estados.value(mes.mes())
                                                                          : QStringLiteral("SinDescargar");
        auto completa = [&e](CoberturaDireccion& d) {
            for (const HuecoCobertura& h : std::as_const(d.huecos)) {
                d.tramos.append(h.tramo);
            }
            d.huecos.clear();
            d.hasta = e.frontera;
            d.alcanzaFrontera = true;
        };
        if (e.tipoMes == TipoMes::Futuro) {
            return QtFuture::makeReadyValueFuture(ResultadoDisponibilidad::exito(e));
        }
        if (estado == QLatin1String("Disponibles")) {
            completa(e.emitidos);
            completa(e.recibidos);
            e.estado = EstadoMes::Disponibles;
        } else if (estado == QLatin1String("Parcial")) {
            completa(e.emitidos);
            e.estado = EstadoMes::DisponiblesParcialmente;
        } else if (estado == QLatin1String("Atencion")) {
            completa(e.emitidos);
            const CausaMensual causa{CausaAtencion::ZipIlegible, TipoDescarga::Recibidos, SolicitudId::generar()};
            e.causa = causa;
            e.causas = {causa};
            e.estado = EstadoMes::RequiereAtencion;
        } else if (estado == QLatin1String("Preparando")) {
            for (CoberturaDireccion* d : {&e.emitidos, &e.recibidos}) {
                const SolicitudId id = SolicitudId::generar();
                for (HuecoCobertura& h : d->huecos) {
                    h.reservadoPor = {id};
                    h.libres.clear();
                }
            }
            e.estado = EstadoMes::Preparando;
        }
        return QtFuture::makeReadyValueFuture(ResultadoDisponibilidad::exito(e));
    }

    QFuture<ResultadoPreplan> planificar(const PerfilId&, MesCalendario, AccionGuiada) override
    {
        ++planificaciones;
        preplanes.push_back(std::make_unique<QPromise<ResultadoPreplan>>());
        preplanes.back()->start();
        return preplanes.back()->future();
    }

    QFuture<ResultadoEjecucion> ejecutar(const PreplanGuiado&, const DecisionesGuiadas&) override
    {
        QPromise<ResultadoEjecucion> p;
        p.start();
        return p.future();
    }

    QDateTime ahoraUtc() const override
    {
        return QDateTime(QDate(2026, 10, 15), QTime(18, 0), QTimeZone::UTC);
    }

private:
    QList<CfdiMensual> cfdisDe(bool principal, int anio, int mes) const
    {
        QList<CfdiMensual> r;
        if (!principal || anio != 2026) {
            return r;
        }
        const ConteoMes n = conteoPara(m_estados.value(mes), mes);
        const SolicitudId solicitud = *SolicitudId::desdeTexto(u"00000000-0000-4000-8000-000000000001");
        auto agregar = [&](TipoDescarga direccion, int cuantos) {
            for (int i = 0; i < cuantos; ++i) {
                fakes::DatosCfdi d;
                const bool emitida = direccion == TipoDescarga::Emitidos;
                d.paquete = QStringLiteral("PAQ_%1_%2").arg(mes).arg(emitida ? QStringLiteral("E") : QStringLiteral("R"));
                d.posicion = i;
                d.tipo = i % 7 == 6 ? TipoComprobante::Egreso : i % 11 == 10 ? TipoComprobante::Pago : TipoComprobante::Ingreso;
                d.fecha = QStringLiteral("2026-%1-%2T10:22:31").arg(mes, 2, 10, QLatin1Char('0'))
                              .arg(28 - (i % 27), 2, 10, QLatin1Char('0'));
                d.serie = emitida ? QStringLiteral("A") : QStringLiteral("F");
                d.folio = QString::number(1000 + 37 * i);
                const QString nombre = QString::fromUtf8(kNombres[std::size_t(i) % kNombres.size()]);
                if (emitida) {
                    d.emisorRfc = QStringLiteral("XAXX010101000");
                    d.emisorNombre = QStringLiteral("Contribuyente de ejemplo");
                    d.receptorRfc = QStringLiteral("XEXX010101000");
                    d.receptorNombre = nombre;
                } else {
                    d.emisorRfc = QStringLiteral("XEXX010101000");
                    d.emisorNombre = nombre;
                    d.receptorRfc = QStringLiteral("XAXX010101000");
                    d.receptorNombre = QStringLiteral("Contribuyente de ejemplo");
                }
                d.total = QStringLiteral("%1.%2").arg(1200 + 731 * i).arg(10 + i % 89);
                // 8-4-4-4-12 hexadecimales sinteticos, distintos por mes y direccion.
                d.uuid = QStringLiteral("%1%2%3-0000-4000-8000-%4")
                             .arg(emitida ? QLatin1Char('E') : QLatin1Char('F'))
                             .arg(mes, 2, 16, QLatin1Char('0'))
                             .arg(0x3A5C7 + 7919 * i, 5, 16, QLatin1Char('0'))
                             .arg(i, 12, 10, QLatin1Char('0'))
                             .toUpper();
                r.append(CfdiMensual{fakes::cfdi(d), direccion, solicitud, true});
            }
        };
        agregar(TipoDescarga::Emitidos, n.emitidas);
        agregar(TipoDescarga::Recibidos, n.recibidas);
        return r;
    }

    bool m_conPerfiles;
};

// Historial sintetico del perfil XAXX010101000 (T023.5).
class HistorialSintetico final : public FuenteHistorial {
public:
    QString caso = QStringLiteral("mixto");
    int consultas = 0;

    void cambiar() { emit datosCambiados(PerfilId()); }

    QFuture<ResultadoEntradas> historial(const PerfilId& perfil) override
    {
        ++consultas;
        if (caso == QLatin1String("error")) {
            return QtFuture::makeReadyValueFuture(ResultadoEntradas::fallo(ErrorPersistencia::de(ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("prueba"))));
        }
        QList<EntradaHistorial> r;
        if (caso == QLatin1String("mixto")) {
            auto entrada = [&](int mes, int dia, ResultadoHistorial resultado, int emitidas, int recibidas) {
                EntradaHistorial e;
                e.loteId = QStringLiteral("lote-%1").arg(mes);
                e.perfil = perfil;
                e.mes = MesCalendario::crear(2026, mes);
                e.creadoEn = QDateTime(QDate(2026, mes == 12 ? 12 : mes + 1, dia), QTime(15, 12), QTimeZone::UTC);
                e.resultado = resultado;
                e.emitidas = emitidas;
                e.recibidas = recibidas;
                e.tieneEmitidas = true;
                e.tieneRecibidas = true;
                e.diagnosticoEmitidas = SolicitudId::generar();
                e.diagnosticoRecibidas = SolicitudId::generar();
                return e;
            };
            r.append(entrada(9, 3, ResultadoHistorial::EnPreparacion, 0, 0));
            r.append(entrada(8, 4, ResultadoHistorial::Completa, 34, 36));
            EntradaHistorial parcial = entrada(7, 5, ResultadoHistorial::Completa, 35, 57);
            parcial.eliminacionParcial = true;
            r.append(parcial);
            EntradaHistorial atencion = entrada(6, 2, ResultadoHistorial::RequiereAtencion, 32, 0);
            atencion.puedeReintentar = true;
            r.append(atencion);
            EntradaHistorial cubierta = entrada(5, 10, ResultadoHistorial::NoCompletada, 0, 0);
            cubierta.tieneEmitidas = false;
            cubierta.diagnosticoEmitidas.reset();
            r.append(cubierta);
            EntradaHistorial eliminada = entrada(4, 2, ResultadoHistorial::Eliminada, 0, 0);
            r.append(eliminada);
            r.append(entrada(2, 4, ResultadoHistorial::SinFacturas, 0, 0));
            EntradaHistorial anterior;
            anterior.tipo = EntradaHistorial::Tipo::Anterior;
            anterior.perfil = perfil;
            anterior.desde = QDate(2026, 1, 1);
            anterior.hasta = QDate(2026, 3, 15);
            anterior.creadoEn = QDateTime(QDate(2026, 3, 20), QTime(16, 0), QTimeZone::UTC);
            anterior.resultado = ResultadoHistorial::Anterior;
            anterior.emitidas = 12;
            anterior.tieneEmitidas = true;
            anterior.diagnosticoEmitidas = SolicitudId::generar();
            r.append(anterior);
        }
        return QtFuture::makeReadyValueFuture(ResultadoEntradas::exito(r));
    }
};

class PreferenciasEnMemoria final : public PreferenciasFacturas {
public:
    std::optional<QString> ultimoPerfil() const override { return m_id; }
    void setUltimoPerfil(const QString& perfilId) override { m_id = perfilId; }

private:
    std::optional<QString> m_id;
};

} // namespace

struct FixtureFacturas::Grafo {
    explicit Grafo(bool conPerfiles)
        : fuente(conPerfiles)
    {
        vms.setFuenteFacturas(&fuente, &preferencias);
        vms.setFuenteHistorial(&historial);
        vms.facturas()->setIntervaloBusqueda(0);
    }

    HistorialSintetico historial;

    fakes::FakePerfilesSatService perfiles;
    fakes::FakeCredencialesSatService credenciales;
    SolicitudesServiceAsincrono solicitudes;
    FuenteSintetica fuente;
    PreferenciasEnMemoria preferencias;
    PresentacionViewModels vms{&solicitudes, &perfiles, &credenciales};
};

FixtureFacturas::FixtureFacturas(QObject* parent)
    : QObject(parent)
    , m_grafo(std::make_unique<Grafo>(true))
    , m_franja(new FranjaMesViewModel(this))
{
}

FixtureFacturas::~FixtureFacturas() = default;

AppViewModel* FixtureFacturas::app() const { return m_grafo->vms.app(); }
FacturasViewModel* FixtureFacturas::facturas() const { return m_grafo->vms.facturas(); }
HistorialViewModel* FixtureFacturas::historial() const { return m_grafo->vms.historial(); }

void FixtureFacturas::fijarHistorial(const QString& caso)
{
    m_grafo->historial.caso = caso;
    m_grafo->historial.cambiar();
}

int FixtureFacturas::numHistoriales() const { return m_grafo->historial.consultas; }

void FixtureFacturas::reiniciar(bool conPerfiles)
{
    // El grafo anterior se retira (no se destruye): QML puede tener aun
    // bindings hacia sus objetos hasta el siguiente ciclo.
    m_retirados.push_back(std::move(m_grafo));
    m_grafo = std::make_unique<Grafo>(conPerfiles);
    emit reiniciado();
}

void FixtureFacturas::fijarEstadoMes(int mes, const QString& estado)
{
    m_grafo->fuente.m_estados.insert(mes, estado);
    // El grafo ya activo Facturas (pagina inicial): recarga como lo haria la
    // aplicacion al cambiar los datos del mes.
    m_grafo->fuente.cambiar();
}

void FixtureFacturas::emitirDatosCambiados() { m_grafo->fuente.cambiar(); }
int FixtureFacturas::numPlanificaciones() const { return m_grafo->fuente.planificaciones; }
int FixtureFacturas::numConsultas() const { return m_grafo->fuente.consultas; }
QString FixtureFacturas::perfilSinEFirma() const { return m_grafo->fuente.perfilSinEFirma.texto(); }
QString FixtureFacturas::directorioCapturas() const
{
    const QString dir = qEnvironmentVariable("SATCFDI_CAPTURAS");
    if (!dir.isEmpty()) {
        QDir().mkpath(dir);
    }
    return dir;
}

void FixtureFacturas::fijarFranja(const QVariantMap& v)
{
    FranjaMesViewModel::Datos d;
    d.estado = FranjaMesViewModel::Estado(v.value(QStringLiteral("estado")).toInt());
    d.estadoTexto = v.value(QStringLiteral("estadoTexto")).toString();
    d.tono = v.value(QStringLiteral("tono")).toString();
    d.icono = v.value(QStringLiteral("icono")).toString();
    d.titular = v.value(QStringLiteral("titular")).toString();
    d.descripcion = v.value(QStringLiteral("descripcion")).toString();
    d.accion = FranjaMesViewModel::Accion(v.value(QStringLiteral("accion")).toInt());
    d.textoAccion = v.value(QStringLiteral("textoAccion")).toString();
    d.accionHabilitada = v.value(QStringLiteral("accionHabilitada")).toBool();
    d.motivoDeshabilitada = v.value(QStringLiteral("motivoDeshabilitada")).toString();
    d.ejecutando = v.value(QStringLiteral("ejecutando")).toBool();
    d.ocupado = v.value(QStringLiteral("ocupado")).toBool();
    d.etapa = v.value(QStringLiteral("etapa")).toString();
    d.nota = v.value(QStringLiteral("nota")).toString();
    d.hasta = v.value(QStringLiteral("hasta")).toString();
    d.causa = v.value(QStringLiteral("causa")).toString();
    d.textoEnlace = v.value(QStringLiteral("textoEnlace")).toString();
    d.aviso = v.value(QStringLiteral("aviso")).toString();
    m_franja->fijar(d);
}
