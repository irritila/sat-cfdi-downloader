#include "application/profiles/DemoPerfilesSatService.h"

#include <QFuture>

#include <algorithm>
#include <utility>

namespace satcfdi {

namespace {

PerfilResumen perfil(const char* id, const char* rfc, const char* razonSocial, bool activo)
{
    PerfilResumen p;
    p.id = *PerfilId::desdeTexto(QString::fromLatin1(id));
    p.rfc = QString::fromLatin1(rfc);
    p.razonSocial = QString::fromUtf8(razonSocial);
    p.activo = activo;
    return p;
}

} // namespace

QList<PerfilResumen> DemoPerfilesSatService::perfilesDemo()
{
    return {
        perfil("00000000-0000-4000-9000-000000000001", "EKU9003173C9",
               "Demo Escuela Kemper Urgate", true),
        perfil("00000000-0000-4000-9000-000000000002", "CACX7605101P8",
               "Demo Persona Fisica", true),
        perfil("00000000-0000-4000-9000-000000000003", "IIA040805DZ4",
               "Demo Perfil Inactivo", false),
    };
}

DemoPerfilesSatService::DemoPerfilesSatService(QList<PerfilResumen> perfiles, QObject* parent)
    : PerfilesSatService(parent)
    , m_perfiles(std::move(perfiles))
{
}

QFuture<PerfilesSatService::ResultadoLista> DemoPerfilesSatService::listarActivos()
{
    QList<PerfilResumen> activos;
    std::copy_if(m_perfiles.cbegin(), m_perfiles.cend(), std::back_inserter(activos),
                 [](const PerfilResumen& p) { return p.activo; });
    std::sort(activos.begin(), activos.end(),
              [](const PerfilResumen& a, const PerfilResumen& b) { return a.rfc < b.rfc; });
    return QtFuture::makeReadyValueFuture(ResultadoLista::exito(std::move(activos)));
}

} // namespace satcfdi
