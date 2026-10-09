#include "FixturePerfiles.h"

#include <QTimeZone>

using namespace satcfdi;
using fakes::FakeCredencialesSatService;
using fakes::FakePerfilesSatService;

namespace {

// Indice fuera de rango: aviso (las pruebas QML fallan ante cualquier
// warning) en lugar de un acceso invalido.
template <typename P>
bool existe(const P& promesas, int indice, const char* que)
{
    if (indice >= 0 && indice < promesas.size()) {
        return true;
    }
    qWarning("FixturePerfiles: no hay llamada %d pendiente de %s", indice, que);
    return false;
}

const QString kDirectorio = QStringLiteral("/private/tmp/CENTINELA_DIR_quick");

std::optional<EstadoCredencial> estadoDesdeClave(const QString& clave)
{
    static const QList<std::pair<QString, EstadoCredencial>> tabla = {
        {QStringLiteral("SinCredencial"), EstadoCredencial::SinCredencial},
        {QStringLiteral("Validando"), EstadoCredencial::Validando},
        {QStringLiteral("Lista"), EstadoCredencial::Lista},
        {QStringLiteral("Vencida"), EstadoCredencial::Vencida},
        {QStringLiteral("NoVigenteAun"), EstadoCredencial::NoVigenteAun},
        {QStringLiteral("MaterialFaltante"), EstadoCredencial::MaterialFaltante},
        {QStringLiteral("MaterialDanado"), EstadoCredencial::MaterialDanado},
    };
    for (const auto& [k, v] : tabla) {
        if (k == clave) {
            return v;
        }
    }
    return std::nullopt;
}

std::optional<ErrorSecretStore::Categoria> categoriaDesdeClave(const QString& clave)
{
    for (ErrorSecretStore::Categoria c : kCategoriasErrorSecretStore) {
        if (claveEstable(c) == clave) {
            return c;
        }
    }
    return std::nullopt;
}

OrigenErrorEFirma origenDesdeClave(const QString& clave)
{
    if (clave == QStringLiteral("Certificado")) {
        return OrigenErrorEFirma::Certificado;
    }
    if (clave == QStringLiteral("Llave")) {
        return OrigenErrorEFirma::Llave;
    }
    if (clave == QStringLiteral("Contrasena")) {
        return OrigenErrorEFirma::Contrasena;
    }
    return OrigenErrorEFirma::Ninguno;
}

CredencialesSatService::ResultadoImportacion resultadoImportacion(const QString& categoria,
                                                                  const QString& origen)
{
    if (categoria.isEmpty()) {
        return CredencialesSatService::ResultadoImportacion::exito(FakeCredencialesSatService::importada());
    }
    return CredencialesSatService::ResultadoImportacion::fallo(FakeCredencialesSatService::errorAlmacen(
        categoriaDesdeClave(categoria).value_or(ErrorSecretStore::Categoria::Interno), origenDesdeClave(origen)));
}

} // namespace

struct FixturePerfiles::Grafo {
    FakePerfilesSatService perfiles;
    FakeCredencialesSatService credenciales;
    SolicitudesServiceAsincrono solicitudes;
    PresentacionViewModels vms{&solicitudes, &perfiles, &credenciales};
};

FixturePerfiles::FixturePerfiles(QObject* parent)
    : QObject(parent)
    , m_grafo(std::make_unique<Grafo>())
{
}

FixturePerfiles::~FixturePerfiles() = default;


AppViewModel* FixturePerfiles::app() const { return m_grafo->vms.app(); }
PerfilesSatViewModel* FixturePerfiles::perfiles() const { return m_grafo->vms.perfiles(); }
EFirmaFormViewModel* FixturePerfiles::eFirma() const { return m_grafo->vms.eFirma(); }
SolicitudesListModel* FixturePerfiles::solicitudesModel() const { return m_grafo->vms.solicitudes(); }
NuevaSolicitudViewModel* FixturePerfiles::nuevaSolicitud() const { return m_grafo->vms.nuevaSolicitud(); }
SolicitudDetailViewModel* FixturePerfiles::detalle() const { return m_grafo->vms.detalle(); }

void FixturePerfiles::resolverSolicitudes(int indice, const QVariantList& filas)
{
    if (!existe(m_grafo->solicitudes.lista, indice, "listar")) {
        return;
    }
    QList<SolicitudResumen> lista;
    for (const QVariant& v : filas) {
        const QVariantMap m = v.toMap();
        SolicitudResumen r;
        r.id = SolicitudId::generar();
        r.perfilRfc = m.value(QStringLiteral("rfc"), QStringLiteral("XAXX010101000")).toString();
        r.tipoDescarga = tipoDescargaDesdeClave(m.value(QStringLiteral("tipo"), QStringLiteral("Emitidos")).toString())
                             .value_or(TipoDescarga::Emitidos);
        r.fechaInicial = QDate::fromString(m.value(QStringLiteral("inicial")).toString(), Qt::ISODate);
        r.fechaFinal = QDate::fromString(m.value(QStringLiteral("final")).toString(), Qt::ISODate);
        r.estadoLocal = estadoLocalDesdeClave(m.value(QStringLiteral("estadoLocal"), QStringLiteral("Creada")).toString())
                            .value_or(EstadoLocal::Creada);
        if (m.contains(QStringLiteral("estadoSat"))) {
            r.estadoSat = estadoSolicitudSatDesdeClave(m.value(QStringLiteral("estadoSat")).toString());
        }
        r.totalPaquetes = m.value(QStringLiteral("paquetes"), 0).toInt();
        r.creadaEn = QDateTime::fromString(m.value(QStringLiteral("creada")).toString(), Qt::ISODate);
        lista.append(r);
    }
    m_grafo->solicitudes.lista.resolver(indice, SolicitudesService::ResultadoLista::exito(lista));
}

void FixturePerfiles::fallarSolicitudes(int indice)
{
    if (existe(m_grafo->solicitudes.lista, indice, "listar")) {
        m_grafo->solicitudes.lista.resolver(indice, SolicitudesService::ResultadoLista::fallo(ErrorPersistencia::de(
                                                        ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("fake"))));
    }
}

int FixturePerfiles::numListasSolicitudes() const { return m_grafo->solicitudes.lista.size(); }

void FixturePerfiles::resolverEvaluacion(int indice, const QString& clasificacion, const QString& motivo)
{
    if (!existe(m_grafo->solicitudes.evaluacion, indice, "evaluarDuplicado")) {
        return;
    }
    EvaluacionDuplicado e;
    e.clasificacion = clasificacion == QStringLiteral("Bloqueado") ? ClasificacionDuplicado::Bloqueado
                      : clasificacion == QStringLiteral("RequiereConfirmacion") ? ClasificacionDuplicado::RequiereConfirmacion
                                                                                : ClasificacionDuplicado::Libre;
    static const QList<std::pair<QString, MotivoDuplicado>> motivos = {
        {QStringLiteral("SolicitudEnCurso"), MotivoDuplicado::SolicitudEnCurso},
        {QStringLiteral("TerminadaDescargada"), MotivoDuplicado::TerminadaDescargada},
        {QStringLiteral("TerminadaSinPaquetes"), MotivoDuplicado::TerminadaSinPaquetes},
        {QStringLiteral("SolicitudEliminada"), MotivoDuplicado::SolicitudEliminada},
    };
    for (const auto& [clave, m] : motivos) {
        if (clave == motivo) {
            e.motivo = m;
        }
    }
    if (e.clasificacion != ClasificacionDuplicado::Libre) {
        e.solicitudReferencia = SolicitudId::generar();
    }
    m_grafo->solicitudes.evaluacion.resolver(indice, SolicitudesService::ResultadoEvaluarDuplicado::exito(e));
}

void FixturePerfiles::resolverDetalleEjemplo(int indice, const QString& escenario)
{
    if (!existe(m_grafo->solicitudes.detalle, indice, "obtener")) {
        return;
    }
    const auto utc = [](int dia, int h, int m) { return QDateTime(QDate(2026, 10, dia), QTime(h, m), QTimeZone::UTC); };
    SolicitudDetalle d;
    d.resumen.id = m_grafo->solicitudes.idsObtener.value(indice);
    d.resumen.perfilRfc = QStringLiteral("XAXX010101000");
    d.resumen.tipoDescarga = TipoDescarga::Recibidos;
    d.resumen.fechaInicial = QDate(2026, 9, 3);
    d.resumen.fechaFinal = QDate(2026, 9, 3);
    d.resumen.creadaEn = utc(1, 16, 3);
    d.fechaInicialSat = QStringLiteral("2026-09-03T00:00:00");
    d.fechaFinalSat = QStringLiteral("2026-09-03T23:59:59");
    const auto log = [&](TipoEventoLog t, OrigenLog o, int h, int m) {
        LogResumen l;
        l.tipoEvento = t;
        l.origen = o;
        l.creadoEn = utc(1, h, m);
        d.logs.append(l);
    };
    log(TipoEventoLog::SolicitudCreada, OrigenLog::Usuario, 16, 3);
    if (escenario == QStringLiteral("creada")) {
        d.resumen.estadoLocal = EstadoLocal::Creada;
    } else {
        d.resumen.estadoLocal = EstadoLocal::Enviada;
        d.idSolicitudSat = QStringLiteral("4e80345d-917f-40bb-a98f-4a7393934303");
        d.codEstatusSolicitud = QStringLiteral("5000");
        d.mensajeSolicitudSat = QStringLiteral("Solicitud Aceptada");
        d.enviadaEn = utc(1, 16, 4);
        log(TipoEventoLog::SolicitudEnviada, OrigenLog::Usuario, 16, 4);
        if (escenario == QStringLiteral("enProceso")) {
            d.resumen.estadoSat = EstadoSolicitudSat::EnProceso;
            d.ultimaVerificacionEn = utc(1, 16, 35);
        } else {
            d.resumen.estadoSat = EstadoSolicitudSat::Terminada;
            d.codigoEstadoSolicitud = QStringLiteral("5000");
            d.numeroCfdi = 12;
            d.ultimaVerificacionEn = utc(1, 16, 35);
            log(TipoEventoLog::VerificacionRealizada, OrigenLog::Worker, 16, 35);
            log(TipoEventoLog::PaquetesRegistrados, OrigenLog::Worker, 16, 35);
            const auto paquete = [&](const char* sufijo, EstadoDescarga e) {
                PaqueteResumen p;
                p.idPaqueteSat = QStringLiteral("0F1E2D3C-4B5A-4978-8796-A5B4C3D2E103_") + QLatin1String(sufijo);
                p.estadoDescarga = e;
                p.disponibleEn = utc(1, 16, 40);
                if (e == EstadoDescarga::Descargado) {
                    p.descargadoEn = utc(1, 16, 45);
                }
                if (e == EstadoDescarga::Vencido) {
                    p.vencidoEn = utc(4, 16, 0);
                    p.codigoDescargaSat = QStringLiteral("5007");
                }
                d.paquetes.append(p);
            };
            paquete("01", EstadoDescarga::Descargado);
            if (escenario == QStringLiteral("errorPaquete")) {
                paquete("02", EstadoDescarga::Error);
                log(TipoEventoLog::DescargaFallida, OrigenLog::Worker, 16, 46);
            } else {
                paquete("02", EstadoDescarga::Disponible);
                paquete("03", EstadoDescarga::Vencido);
                log(TipoEventoLog::PaqueteDescargado, OrigenLog::Worker, 16, 45);
            }
            d.resumen.totalPaquetes = int(d.paquetes.size());
        }
    }
    m_grafo->solicitudes.detalle.resolver(indice, SolicitudesService::ResultadoDetalle::exito(d));
}

int FixturePerfiles::numDetalles() const { return m_grafo->solicitudes.detalle.size(); }

int FixturePerfiles::numEvaluaciones() const { return m_grafo->solicitudes.evaluacion.size(); }

void FixturePerfiles::fijarNotificacionesDeshabilitadas(bool valor)
{
    m_grafo->vms.app()->setNotificacionesDeshabilitadas(valor);
}

void FixturePerfiles::reiniciar()
{
    m_retirados.push_back(std::exchange(m_grafo, std::make_unique<Grafo>()));
    emit reiniciado();
}

QStringList FixturePerfiles::resolverLista(int indice, const QVariantList& perfiles)
{
    QList<PerfilResumen> lista;
    QStringList ids;
    for (const QVariant& v : perfiles) {
        const QVariantMap m = v.toMap();
        PerfilResumen p{PerfilId::generar(), m.value(QStringLiteral("rfc")).toString(),
                        m.value(QStringLiteral("nombre")).toString(),
                        m.value(QStringLiteral("activo"), true).toBool()};
        ids.append(p.id.texto());
        lista.append(p);
    }
    if (existe(m_grafo->perfiles.listas, indice, "listarNoEliminados")) {
        m_grafo->perfiles.listas.resolver(indice, PerfilesSatService::ResultadoLista::exito(lista));
    }
    return ids;
}

int FixturePerfiles::numListas() const { return m_grafo->perfiles.listas.size(); }

void FixturePerfiles::resolverResumen(int indice, const QString& estado)
{
    if (!existe(m_grafo->credenciales.resumenes, indice, "obtenerResumen")) {
        return;
    }
    const auto clave = estadoDesdeClave(estado).value_or(EstadoCredencial::SinCredencial);
    // Con e.firma registrada se informa una vigencia fija de ejemplo.
    std::optional<QDateTime> vigencia;
    if (clave == EstadoCredencial::Lista) {
        vigencia = QDateTime(QDate(2029, 1, 1), QTime(12, 0));
    }
    m_grafo->credenciales.resolverResumen(indice, clave, vigencia);
}

void FixturePerfiles::fallarResumen(int indice)
{
    if (existe(m_grafo->credenciales.resumenes, indice, "obtenerResumen")) {
        m_grafo->credenciales.fallarResumen(indice);
    }
}

int FixturePerfiles::numResumenes() const { return m_grafo->credenciales.resumenes.size(); }

void FixturePerfiles::resolverCreacion(int indice, const QString& rfc, const QString& nombre)
{
    if (!existe(m_grafo->perfiles.creaciones, indice, "crear")) {
        return;
    }
    m_grafo->perfiles.creaciones.resolver(
        indice, PerfilesSatService::ResultadoCrear::exito(PerfilResumen{PerfilId::generar(), rfc, nombre, true}));
}

void FixturePerfiles::resolverCreacionDuplicado(int indice)
{
    if (!existe(m_grafo->perfiles.creaciones, indice, "crear")) {
        return;
    }
    m_grafo->perfiles.creaciones.resolver(
        indice, PerfilesSatService::ResultadoCrear::fallo(FakePerfilesSatService::rfcDuplicado()));
}

int FixturePerfiles::numCreaciones() const { return m_grafo->perfiles.creaciones.size(); }

int FixturePerfiles::numRegistros() const { return int(m_grafo->credenciales.registros.size()); }

int FixturePerfiles::largoContrasena(int indice) const
{
    const auto& r = m_grafo->credenciales.registros;
    return indice >= 0 && indice < r.size() ? int(r.at(indice).largoContrasena) : -1;
}

bool FixturePerfiles::registroConRutas(int indice) const
{
    const auto& r = m_grafo->credenciales.registros;
    return indice >= 0 && indice < r.size() && r.at(indice).conCertificado && r.at(indice).conLlave;
}

bool FixturePerfiles::registroEsReemplazo(int indice) const
{
    const auto& r = m_grafo->credenciales.registros;
    return indice >= 0 && indice < r.size() && r.at(indice).esReemplazo;
}

void FixturePerfiles::resolverImportacion(int indice, const QString& categoria, const QString& origen)
{
    if (existe(m_grafo->credenciales.importaciones, indice, "importar")) {
        m_grafo->credenciales.importaciones.resolver(indice, resultadoImportacion(categoria, origen));
    }
}

void FixturePerfiles::resolverReemplazo(int indice, const QString& categoria)
{
    if (existe(m_grafo->credenciales.reemplazos, indice, "reemplazar")) {
        m_grafo->credenciales.reemplazos.resolver(indice, resultadoImportacion(categoria, QString()));
    }
}

QUrl FixturePerfiles::urlCentinela(const QString& nombre) const
{
    return QUrl::fromLocalFile(kDirectorio + QLatin1Char('/') + nombre);
}

QString FixturePerfiles::directorioCentinela() const { return kDirectorio; }
