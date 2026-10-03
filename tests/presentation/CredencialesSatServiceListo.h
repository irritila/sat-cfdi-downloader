#pragma once

#include "application/profiles/CredencialesSatService.h"

#include <QFuture>

// Doble de prueba (solo tests/presentation): CredencialesSatService con
// futures ya completados en el que TODO perfil tiene la e.firma Lista. Lo usan
// los escenarios con servicios demo para que el selector de nueva solicitud
// ofrezca los perfiles activos sin promesas manuales. Las operaciones de
// escritura fallan; obtenerMaterialFirma no se usa en presentacion.
class CredencialesSatServiceListo final : public satcfdi::CredencialesSatService {
    Q_OBJECT

public:
    using CredencialesSatService::CredencialesSatService;

    QFuture<ResultadoImportacion> importar(const satcfdi::PerfilId&, satcfdi::EntradaEFirma&&) override
    {
        return QtFuture::makeReadyValueFuture(ResultadoImportacion::fallo(noDisponible()));
    }
    QFuture<ResultadoImportacion> reemplazar(const satcfdi::PerfilId&, satcfdi::EntradaEFirma&&) override
    {
        return QtFuture::makeReadyValueFuture(ResultadoImportacion::fallo(noDisponible()));
    }
    QFuture<ResultadoEstado> obtenerEstado(const satcfdi::PerfilId&) override
    {
        return QtFuture::makeReadyValueFuture(ResultadoEstado::exito(satcfdi::EstadoCredencial::Lista));
    }
    QFuture<ResultadoResumen> obtenerResumen(const satcfdi::PerfilId&) override
    {
        satcfdi::ResumenCredencial r;
        r.estado = satcfdi::EstadoCredencial::Lista;
        return QtFuture::makeReadyValueFuture(ResultadoResumen::exito(r));
    }
    QFuture<ResultadoEliminacion> eliminar(const satcfdi::PerfilId&) override
    {
        return QtFuture::makeReadyValueFuture(ResultadoEliminacion::fallo(noDisponible()));
    }
    QFuture<ResultadoReconciliacion> reconciliar() override
    {
        return QtFuture::makeReadyValueFuture(ResultadoReconciliacion::fallo(noDisponible()));
    }
    ResultadoMaterial obtenerMaterialFirma(const satcfdi::PerfilId&) override
    {
        return ResultadoMaterial::fallo(noDisponible());
    }

private:
    static satcfdi::ErrorCredencialSat noDisponible()
    {
        return satcfdi::ErrorCredencialSat::almacen(satcfdi::ErrorSecretStore::Categoria::AlmacenNoDisponible);
    }
};
