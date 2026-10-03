#include "application/profiles/DemoPerfilesSatService.h"

#include "domain/common/Rfc.h"

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

QFuture<PerfilesSatService::ResultadoCrearPerfil>
DemoPerfilesSatService::crearPerfilSimulado(const NuevoPerfilSimuladoRequest& request)
{
    ErrorCrearPerfil error;
    const std::optional<QString> rfc = rfc::normalizarYValidar(request.rfc);
    const QString razonSocial = request.razonSocial.trimmed();
    if (!rfc) {
        error.validaciones.append(ErrorCrearPerfil::CodigoValidacion::RfcInvalido);
    }
    if (razonSocial.isEmpty()) {
        error.validaciones.append(ErrorCrearPerfil::CodigoValidacion::RazonSocialRequerida);
    }
    if (!error.validaciones.isEmpty()) {
        error.tipo = ErrorCrearPerfil::Tipo::Validacion;
        error.mensaje = QStringLiteral("El perfil simulado no es valido.");
        return QtFuture::makeReadyValueFuture(ResultadoCrearPerfil::fallo(std::move(error)));
    }
    const bool duplicado = std::any_of(m_perfiles.cbegin(), m_perfiles.cend(),
                                       [&](const PerfilResumen& p) { return p.rfc == *rfc; });
    if (duplicado) {
        error.tipo = ErrorCrearPerfil::Tipo::Integridad;
        error.causa = ErrorPersistencia::de(ErrorPersistencia::Tipo::Unicidad,
                                            QStringLiteral("RFC vigente duplicado."),
                                            QStringLiteral("ux_perfil_sat_rfc_vigente"));
        error.mensaje = error.causa->mensaje;
        return QtFuture::makeReadyValueFuture(ResultadoCrearPerfil::fallo(std::move(error)));
    }

    PerfilResumen p;
    p.id = PerfilId::generar();
    p.rfc = *rfc;
    p.razonSocial = razonSocial;
    p.activo = request.activo;
    m_perfiles.append(p);
    emit perfilesCambiaron();
    return QtFuture::makeReadyValueFuture(ResultadoCrearPerfil::exito(p.id));
}

} // namespace satcfdi
