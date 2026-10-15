#pragma once

#include <algorithm>

// Servicios deterministas del manual (T012 D3, D7): futures ya resueltos con
// datos sinteticos fijos. Sin SQLite, worker, SAT ni Keychain. Solo los usa
// satcfdi_manual_capturas.

#include "application/primeruso/ConsultaPrimerUso.h"
#include "application/profiles/CredencialesSatService.h"
#include "application/profiles/PerfilesSatService.h"
#include "application/requests/SolicitudesService.h"
#include "presentation/viewmodels/AccionesFinder.h"
#include "presentation/viewmodels/AccionesSolicitud.h"
#include "presentation/viewmodels/ConsultaCatalogoCfdi.h"
#include "presentation/viewmodels/ConsultaExistenciaPaquetes.h"
#include "presentation/viewmodels/PrevisualizacionCfdi.h"

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
    void reintentarDescargaPaquete(const SolicitudId&, const QString&) override {}
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
    QFuture<ResultadoAccionFinder> abrirEnVistaPrevia(const QString&) override // T015.4
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

// T015.3: indice de CFDI fijo. listar aplica el texto (subcadena sin
// mayusculas en RFC, nombres, UUID, serie y folio) y el tipo como la app.
class CatalogoFijo final : public ConsultaCatalogoCfdi {
public:
    QList<CfdiListado> cfdis;
    ResumenIndiceSolicitud resumenIndice;

    QFuture<ResultadoLista> listar(const SolicitudId&, const FiltroListaCfdi& filtro) override
    {
        QList<CfdiListado> r;
        for (const CfdiListado& c : cfdis) {
            const ResumenCfdi& x = c.resumen;
            if (filtro.tipo && x.tipo != *filtro.tipo) {
                continue;
            }
            const QString texto = QStringList{x.emisor.rfc, x.emisor.nombre, x.receptor.rfc, x.receptor.nombre, x.uuid,
                                              x.serie, x.folio}
                                      .join(QLatin1Char(' '));
            if (!filtro.texto.isEmpty() && !texto.contains(filtro.texto, Qt::CaseInsensitive)) {
                continue;
            }
            r.append(c);
        }
        return listo(ResultadoLista::exito(r));
    }
    QFuture<ResultadoResumen> resumen(const SolicitudId&) override
    {
        return listo(ResultadoResumen::exito(resumenIndice));
    }
    QFuture<void> revisarSolicitud(const SolicitudId&) override { return QtFuture::makeReadyVoidFuture(); }
};

// T014.3 D1 (T016): pasos fijos de la guia "Empieza en tres pasos". Solo se
// conecta en la captura de la lista vacia; sin ella la app no muestra la guia.
class PrimerUsoFijo final : public ConsultaPrimerUso {
public:
    EstadoPrimerUso estado;

    QFuture<ResultadoPrimerUso> consultar() override { return listo(ResultadoPrimerUso::exito(estado)); }
};

// T015.4: PDF fijo (generado con el renderer real a partir de un XML
// sintetico, ver main.cpp). Guardar no escribe nada.
class PrevisualizacionFija final : public PrevisualizacionCfdi {
public:
    QString rutaPdf; // vacio = Fallo

    QFuture<ResultadoGenerar> generar(const SolicitudId&, const QString&, int) override
    {
        if (rutaPdf.isEmpty()) {
            return listo(ResultadoGenerar::fallo(MotivoPrevisualizacion::Fallo));
        }
        return listo(ResultadoGenerar::exito(PdfPrevisualizado{rutaPdf, QStringLiteral("CFDI-A1187-5F2B1A30.pdf")}));
    }
    QFuture<ResultadoGuardar> guardarComo(const QString&, const QString&) override
    {
        return listo(ResultadoGuardar::exito(true));
    }
};

} // namespace capturas
