#include "application/profiles/ConsultaPreparacionPerfiles.h"

#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/PerfilesSatService.h"
#include "domain/credenciales/VencimientoEFirma.h"

#include <QFuture>

#include <utility>

namespace satcfdi {

QString claveEstable(PreparacionPerfil preparacion)
{
    switch (preparacion) {
    case PreparacionPerfil::Verificando: return QStringLiteral("Verificando");
    case PreparacionPerfil::SinCredencial: return QStringLiteral("SinCredencial");
    case PreparacionPerfil::Lista: return QStringLiteral("Lista");
    case PreparacionPerfil::Vencida: return QStringLiteral("Vencida");
    case PreparacionPerfil::NoVigenteAun: return QStringLiteral("NoVigenteAun");
    case PreparacionPerfil::MaterialFaltante: return QStringLiteral("MaterialFaltante");
    case PreparacionPerfil::MaterialDanado: return QStringLiteral("MaterialDanado");
    case PreparacionPerfil::EstadoNoDisponible: return QStringLiteral("EstadoNoDisponible");
    }
    return {};
}

PreparacionPerfil preparacionDesde(EstadoCredencial estado)
{
    switch (estado) {
    case EstadoCredencial::SinCredencial: return PreparacionPerfil::SinCredencial;
    case EstadoCredencial::Validando: return PreparacionPerfil::Verificando;
    case EstadoCredencial::Lista: return PreparacionPerfil::Lista;
    case EstadoCredencial::Vencida: return PreparacionPerfil::Vencida;
    case EstadoCredencial::NoVigenteAun: return PreparacionPerfil::NoVigenteAun;
    case EstadoCredencial::MaterialFaltante: return PreparacionPerfil::MaterialFaltante;
    case EstadoCredencial::MaterialDanado: return PreparacionPerfil::MaterialDanado;
    }
    return PreparacionPerfil::EstadoNoDisponible;
}

namespace {

PerfilConPreparacion noDisponible(const PerfilResumen& perfil)
{
    return PerfilConPreparacion::componer(perfil, PreparacionPerfil::EstadoNoDisponible);
}

PerfilConPreparacion desdeResumen(const PerfilResumen& perfil,
                                  const CredencialesSatService::ResultadoResumen& r, const QDateTime& ahora)
{
    if (!r.esExito()) {
        return noDisponible(perfil);
    }
    const PreparacionPerfil preparacion = preparacionDesde(r.valor().estado);
    std::optional<QDateTime> vigenteHasta;
    if (preparacion != PreparacionPerfil::SinCredencial) {
        vigenteHasta = r.valor().vigenteHasta;
    }
    PerfilConPreparacion c = PerfilConPreparacion::componer(perfil, preparacion, std::move(vigenteHasta));
    if (c.preparacion == PreparacionPerfil::Lista && c.vigenteHasta) {
        c.diasParaVencer = vencimientoefirma::diasParaVencer(*c.vigenteHasta, ahora);
    }
    return c;
}

} // namespace

ConsultaPreparacionPerfiles::ConsultaPreparacionPerfiles(PerfilesSatService& perfiles,
                                                         CredencialesSatService& credenciales,
                                                         QObject* parent, RelojUtc reloj)
    : QObject(parent)
    , m_perfiles(perfiles)
    , m_credenciales(credenciales)
    , m_reloj(reloj ? std::move(reloj) : relojSistema())
{
}

QFuture<ConsultaPreparacionPerfiles::ResultadoLista> ConsultaPreparacionPerfiles::listarPersistidos()
{
    return m_perfiles.listarNoEliminados().then(this, [](PerfilesSatService::ResultadoLista r) {
        if (!r.esExito()) {
            return ResultadoLista::fallo(std::move(r).error());
        }
        QList<PerfilConPreparacion> lista;
        lista.reserve(r.valor().size());
        for (const PerfilResumen& p : r.valor()) {
            lista.append(PerfilConPreparacion::componer(p, PreparacionPerfil::Verificando));
        }
        return ResultadoLista::exito(std::move(lista));
    });
}

QFuture<PerfilConPreparacion> ConsultaPreparacionPerfiles::verificar(const PerfilResumen& perfil)
{
    return m_credenciales.obtenerResumen(perfil.id)
        .then(this, [perfil, reloj = m_reloj](CredencialesSatService::ResultadoResumen r) {
            return desdeResumen(perfil, r, reloj());
        })
        .onFailed(this, [perfil]() { return noDisponible(perfil); })
        .onCanceled(this, [perfil]() { return noDisponible(perfil); });
}

QFuture<ConsultaPreparacionPerfiles::ResultadoLista> ConsultaPreparacionPerfiles::listarVerificados()
{
    return m_perfiles.listarNoEliminados()
        .then(this,
              [this](PerfilesSatService::ResultadoLista r) -> QFuture<ResultadoLista> {
                  if (!r.esExito()) {
                      return QtFuture::makeReadyValueFuture(ResultadoLista::fallo(std::move(r).error()));
                  }
                  const QList<PerfilResumen> perfiles = r.valor();
                  QList<QFuture<PerfilConPreparacion>> verificaciones;
                  verificaciones.reserve(perfiles.size());
                  for (const PerfilResumen& p : perfiles) {
                      verificaciones.append(verificar(p));
                  }
                  return QtFuture::whenAll(verificaciones.begin(), verificaciones.end())
                      .then(this, [perfiles](const QList<QFuture<PerfilConPreparacion>>& hechas) {
                          QList<PerfilConPreparacion> lista;
                          lista.reserve(hechas.size());
                          for (qsizetype i = 0; i < hechas.size(); ++i) {
                              const QFuture<PerfilConPreparacion>& f = hechas.at(i);
                              lista.append(f.isCanceled() || f.resultCount() == 0 ? noDisponible(perfiles.at(i))
                                                                                  : f.result());
                          }
                          return ResultadoLista::exito(std::move(lista));
                      });
              })
        .unwrap();
}

QFuture<ConsultaPreparacionPerfiles::ResultadoLista> ConsultaPreparacionPerfiles::listarListosParaSolicitudes()
{
    return listarVerificados().then(this, [](ResultadoLista r) {
        if (!r.esExito()) {
            return r;
        }
        QList<PerfilConPreparacion> listos;
        for (const PerfilConPreparacion& p : r.valor()) {
            if (p.listoParaSolicitudes) {
                listos.append(p);
            }
        }
        return ResultadoLista::exito(std::move(listos));
    });
}

} // namespace satcfdi
