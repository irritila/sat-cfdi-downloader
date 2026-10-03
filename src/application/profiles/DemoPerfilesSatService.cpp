#include "application/profiles/DemoPerfilesSatService.h"

#include "domain/common/Rfc.h"

#include <QFuture>

#include <algorithm>
#include <utility>

namespace satcfdi {

namespace {

PerfilResumen perfil(const char* id, const char* rfc, const char* nombre, bool activo)
{
    PerfilResumen p;
    p.id = *PerfilId::desdeTexto(QString::fromLatin1(id));
    p.rfc = QString::fromLatin1(rfc);
    p.nombre = QString::fromUtf8(nombre);
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

namespace {

QList<PerfilResumen> ordenados(QList<PerfilResumen> lista)
{
    std::sort(lista.begin(), lista.end(),
              [](const PerfilResumen& a, const PerfilResumen& b) { return a.rfc < b.rfc; });
    return lista;
}

} // namespace

QFuture<PerfilesSatService::ResultadoLista> DemoPerfilesSatService::listarNoEliminados()
{
    return QtFuture::makeReadyValueFuture(ResultadoLista::exito(ordenados(m_perfiles)));
}

QFuture<PerfilesSatService::ResultadoPerfil> DemoPerfilesSatService::obtener(const PerfilId& id)
{
    for (const PerfilResumen& p : std::as_const(m_perfiles)) {
        if (!id.esNulo() && p.id == id) {
            return QtFuture::makeReadyValueFuture(ResultadoPerfil::exito(p));
        }
    }
    return QtFuture::makeReadyValueFuture(ResultadoPerfil::exito(std::nullopt));
}

Resultado<PerfilResumen, ErrorCrearPerfil>
DemoPerfilesSatService::sembrar(const QString& rfcCapturado, const QString& nombreCapturado, bool activo)
{
    using R = Resultado<PerfilResumen, ErrorCrearPerfil>;
    const std::optional<QString> rfc = rfc::normalizarYValidar(rfcCapturado);
    const QString nombre = nombreCapturado.trimmed();
    QList<CodigoValidacionPerfil> invalidos;
    if (!rfc) {
        invalidos.append(CodigoValidacionPerfil::RfcInvalido);
    }
    if (nombre.isEmpty()) {
        invalidos.append(CodigoValidacionPerfil::NombreRequerido);
    }
    if (!invalidos.isEmpty()) {
        return R::fallo(ErrorCrearPerfil::validacion(std::move(invalidos)));
    }
    const bool duplicado = std::any_of(m_perfiles.cbegin(), m_perfiles.cend(),
                                       [&](const PerfilResumen& p) { return p.rfc == *rfc; });
    if (duplicado) {
        return R::fallo(ErrorCrearPerfil::desdePersistencia(
            ErrorPersistencia::de(ErrorPersistencia::Tipo::Unicidad, QStringLiteral("RFC vigente duplicado."),
                                  QStringLiteral("ux_perfil_sat_rfc_vigente"))));
    }
    PerfilResumen p{PerfilId::generar(), *rfc, nombre, activo};
    m_perfiles.append(p);
    emit perfilesCambiaron();
    return R::exito(p);
}

QFuture<PerfilesSatService::ResultadoCrear> DemoPerfilesSatService::crear(const QString& rfc,
                                                                         const QString& nombre)
{
    return QtFuture::makeReadyValueFuture(sembrar(rfc, nombre, true));
}

QFuture<PerfilesSatService::ResultadoActualizar>
DemoPerfilesSatService::actualizarNombre(const PerfilId& id, const QString& nombreCapturado)
{
    const QString nombre = nombreCapturado.trimmed();
    if (nombre.isEmpty()) {
        return QtFuture::makeReadyValueFuture(ResultadoActualizar::fallo(ErrorActualizarPerfil::nombreRequerido()));
    }
    for (PerfilResumen& p : m_perfiles) {
        if (!id.esNulo() && p.id == id) {
            p.nombre = nombre;
            const PerfilResumen actualizado = p;
            emit perfilesCambiaron();
            return QtFuture::makeReadyValueFuture(ResultadoActualizar::exito(actualizado));
        }
    }
    return QtFuture::makeReadyValueFuture(ResultadoActualizar::fallo(ErrorActualizarPerfil::inexistente()));
}

} // namespace satcfdi
