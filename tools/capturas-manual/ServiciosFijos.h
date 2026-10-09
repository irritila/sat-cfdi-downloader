#pragma once

#include <algorithm>

// Servicios deterministas del manual (T012 D3, D7): futures ya resueltos con
// datos sinteticos fijos. Sin SQLite, worker, SAT ni Keychain. Solo los usa
// satcfdi_manual_capturas.

#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/PerfilesSatService.h"
#include "application/requests/SolicitudesService.h"
#include "presentation/viewmodels/AccionesFinder.h"
#include "presentation/viewmodels/AccionesSolicitud.h"
#include "presentation/viewmodels/ConsultaExistenciaPaquetes.h"

#include <QHash>
#include <QList>

namespace capturas {

using namespace satcfdi;

template <typename T>
QFuture<T> listo(T valor)
{
    return QtFuture::makeReadyValueFuture(std::move(valor));
}

class SolicitudesFijas final : public SolicitudesService {
public:
    QList<SolicitudDetalle> detalles;
    std::optional<EvaluacionDuplicado> evaluacion; // nullopt = Libre

    QFuture<ResultadoLista> listar() override
    {
        QList<SolicitudResumen> r;
        for (const SolicitudDetalle& d : detalles) {
            r.append(d.resumen);
        }
        // Como la app real: mas reciente primero.
        std::stable_sort(r.begin(), r.end(),
                         [](const SolicitudResumen& a, const SolicitudResumen& b) { return a.creadaEn > b.creadaEn; });
        return listo(ResultadoLista::exito(r));
    }
    QFuture<ResultadoDetalle> obtener(const SolicitudId& id) override
    {
        for (const SolicitudDetalle& d : detalles) {
            if (d.resumen.id == id) {
                return listo(ResultadoDetalle::exito(d));
            }
        }
        return listo(ResultadoDetalle::fallo(ErrorObtener{}));
    }
    QFuture<ResultadoEvaluarDuplicado> evaluarDuplicado(const NuevaSolicitudRequest&) override
    {
        return listo(ResultadoEvaluarDuplicado::exito(evaluacion.value_or(EvaluacionDuplicado{})));
    }
    QFuture<ResultadoCrear> crearLocal(const NuevaSolicitudRequest&, ConfirmacionDuplicado) override
    {
        return listo(ResultadoCrear::exito(SolicitudId::desdeTexto(u"00000000-0000-4000-8000-0000000000aa").value()));
    }
    QFuture<ResultadoEliminar> eliminar(const SolicitudId&) override
    {
        return listo(ResultadoEliminar::exito(ResultadoEliminacion{}));
    }
};

class PerfilesFijos final : public PerfilesSatService {
public:
    QList<PerfilResumen> perfiles;

    QFuture<ResultadoLista> listarNoEliminados() override { return listo(ResultadoLista::exito(perfiles)); }
    QFuture<ResultadoPerfil> obtener(const PerfilId& id) override
    {
        for (const PerfilResumen& p : perfiles) {
            if (p.id == id) {
                return listo(ResultadoPerfil::exito(p));
            }
        }
        return listo(ResultadoPerfil::exito(std::nullopt));
    }
    QFuture<ResultadoCrear> crear(const QString&, const QString&) override
    {
        return listo(ResultadoCrear::exito(perfiles.value(0)));
    }
    QFuture<ResultadoActualizar> actualizarNombre(const PerfilId&, const QString&) override
    {
        return listo(ResultadoActualizar::exito(perfiles.value(0)));
    }
};

class CredencialesFijas final : public CredencialesSatService {
public:
    QHash<QString, ResumenCredencial> resumenes; // perfilId -> resumen (falta = SinCredencial)

    QFuture<ResultadoImportacion> importar(const PerfilId&, EntradaEFirma&&) override
    {
        return listo(ResultadoImportacion::fallo(ErrorCredencialSat::existente()));
    }
    QFuture<ResultadoImportacion> reemplazar(const PerfilId&, EntradaEFirma&&) override
    {
        return listo(ResultadoImportacion::fallo(ErrorCredencialSat::existente()));
    }
    QFuture<ResultadoEstado> obtenerEstado(const PerfilId& id) override
    {
        return listo(ResultadoEstado::exito(resumenes.value(id.texto()).estado));
    }
    QFuture<ResultadoResumen> obtenerResumen(const PerfilId& id) override
    {
        return listo(ResultadoResumen::exito(resumenes.value(id.texto())));
    }
    QFuture<ResultadoEliminacion> eliminar(const PerfilId&) override
    {
        return listo(ResultadoEliminacion::exito(CredencialEliminada{}));
    }
    QFuture<ResultadoReconciliacion> reconciliar() override
    {
        return listo(ResultadoReconciliacion::exito(ResumenReconciliacion{}));
    }
    ResultadoMaterial obtenerMaterialFirma(const PerfilId&) override
    {
        return ResultadoMaterial::fallo(ErrorCredencialSat::hiloNoPermitido());
    }
};

// Acciones: no hacen nada (las capturas solo muestran la UI).
class AccionesNulas final : public AccionesSolicitud, public AccionesFinder {
public:
    void enviar(const SolicitudId&) override {}
    void verificarAhora(const SolicitudId&) override {}
    void reintentarDescarga(const SolicitudId&) override {}
    QFuture<ResultadoAccionFinder> mostrarPaquete(const SolicitudId&, const QString&) override
    {
        return listo(ResultadoAccionFinder{ResultadoAccionFinder::Estado::Mostrado, {}});
    }
    QFuture<ResultadoAccionFinder> abrirCarpetaSolicitud(const SolicitudId&) override
    {
        return listo(ResultadoAccionFinder{ResultadoAccionFinder::Estado::Mostrado, {}});
    }
    QFuture<ResultadoAccionFinder> abrirCarpetaPaquetes() override
    {
        return listo(ResultadoAccionFinder{ResultadoAccionFinder::Estado::Mostrado, {}});
    }
};

class ExistenciaFija final : public ConsultaExistenciaPaquetes {
public:
    QHash<QString, ExistenciaPaquete> porPaquete; // falta = Presente
    QFuture<ExistenciaPaquete> consultar(const SolicitudId&, const QString& idPaqueteSat) override
    {
        return listo(porPaquete.value(idPaqueteSat, ExistenciaPaquete::Presente));
    }
};

} // namespace capturas
