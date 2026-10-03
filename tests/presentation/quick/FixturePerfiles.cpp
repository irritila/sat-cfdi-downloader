#include "FixturePerfiles.h"

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
    m_grafo->credenciales.resolverResumen(indice, estadoDesdeClave(estado).value_or(EstadoCredencial::SinCredencial));
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
