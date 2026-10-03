#pragma once

// CredencialesSatService falso con promesas manuales (T005.1). Header-only,
// hilo grafico. Registra cada llamada SIN conservar secretos: de la
// EntradaEFirma solo guarda si habia rutas y el largo de la contrasena; la
// entrada se destruye (y limpia) dentro de la llamada. credencialCambio se
// emite SOLO con emitirCambio(perfilId) (simula el commit).
// obtenerMaterialFirma (sincrono) devuelve `errorMaterial`.
//
// Builders: resumen(estado, vigenteHasta), importada(), errorAlmacen(cat,
// origen) -- p. ej. AlmacenBloqueado para EstadoNoDisponible, o
// ContrasenaIncorrecta/FormatoInvalido+Llave para origen de error --,
// existente(), perfilInvalido().

#include "PromesasPendientes.h"

#include "application/profiles/CredencialesSatService.h"

#include <QStringList>

namespace fakes {

class FakeCredencialesSatService final : public satcfdi::CredencialesSatService {
    Q_OBJECT

public:
    using CredencialesSatService::CredencialesSatService;

    struct LlamadaRegistro {
        satcfdi::PerfilId perfilId;
        bool esReemplazo = false;
        bool conCertificado = false;
        bool conLlave = false;
        std::size_t largoContrasena = 0;
    };

    QFuture<ResultadoImportacion> importar(const satcfdi::PerfilId& perfilId,
                                           satcfdi::EntradaEFirma&& entrada) override
    {
        registrar(perfilId, std::move(entrada), false);
        return importaciones.nueva();
    }

    QFuture<ResultadoImportacion> reemplazar(const satcfdi::PerfilId& perfilId,
                                             satcfdi::EntradaEFirma&& entrada) override
    {
        registrar(perfilId, std::move(entrada), true);
        return reemplazos.nueva();
    }

    QFuture<ResultadoEstado> obtenerEstado(const satcfdi::PerfilId& perfilId) override
    {
        historial.append(QStringLiteral("obtenerEstado"));
        idsEstado.append(perfilId);
        return estados.nueva();
    }

    QFuture<ResultadoResumen> obtenerResumen(const satcfdi::PerfilId& perfilId) override
    {
        historial.append(QStringLiteral("obtenerResumen"));
        idsResumen.append(perfilId);
        return resumenes.nueva();
    }

    QFuture<ResultadoEliminacion> eliminar(const satcfdi::PerfilId& perfilId) override
    {
        historial.append(QStringLiteral("eliminar"));
        idsEliminar.append(perfilId);
        return eliminaciones.nueva();
    }

    QFuture<ResultadoReconciliacion> reconciliar() override
    {
        historial.append(QStringLiteral("reconciliar"));
        return reconciliaciones.nueva();
    }

    ResultadoMaterial obtenerMaterialFirma(const satcfdi::PerfilId&) override
    {
        historial.append(QStringLiteral("obtenerMaterialFirma"));
        return ResultadoMaterial::fallo(errorMaterial);
    }

    void emitirCambio(const satcfdi::PerfilId& perfilId) { emit credencialCambio(perfilId.texto()); }

    // Resuelve la consulta de resumen `i` (orden de llegada) para el id
    // registrado en idsResumen[i]; util para respuestas tardias.
    void resolverResumen(int i, satcfdi::EstadoCredencial estado,
                         std::optional<QDateTime> vigenteHasta = std::nullopt)
    {
        resumenes.resolver(i, ResultadoResumen::exito(resumen(estado, std::move(vigenteHasta))));
    }
    // EstadoNoDisponible para ConsultaPreparacionPerfiles.
    void fallarResumen(int i, satcfdi::ErrorSecretStore::Categoria categoria =
                                  satcfdi::ErrorSecretStore::Categoria::AlmacenBloqueado)
    {
        resumenes.resolver(i, ResultadoResumen::fallo(errorAlmacen(categoria)));
    }

    // --- Builders ---------------------------------------------------------
    static satcfdi::ResumenCredencial resumen(satcfdi::EstadoCredencial estado,
                                              std::optional<QDateTime> vigenteHasta = std::nullopt)
    {
        satcfdi::ResumenCredencial r;
        r.estado = estado;
        r.vigenteHasta = std::move(vigenteHasta);
        return r;
    }
    static satcfdi::CredencialImportada importada(bool limpiezaPendiente = false)
    {
        satcfdi::CredencialImportada r;
        r.estado = satcfdi::EstadoCredencial::Lista;
        r.limpiezaPendiente = limpiezaPendiente;
        return r;
    }
    static satcfdi::ErrorCredencialSat errorAlmacen(
        satcfdi::ErrorSecretStore::Categoria categoria,
        satcfdi::OrigenErrorEFirma origen = satcfdi::OrigenErrorEFirma::Ninguno)
    {
        return satcfdi::ErrorCredencialSat::almacen(categoria, origen);
    }
    static satcfdi::ErrorCredencialSat existente() { return satcfdi::ErrorCredencialSat::existente(); }
    static satcfdi::ErrorCredencialSat perfilInvalido(
        satcfdi::ErrorCredencialSat::CodigoPerfil codigo = satcfdi::ErrorCredencialSat::CodigoPerfil::PerfilInexistente)
    {
        return satcfdi::ErrorCredencialSat::perfil(codigo);
    }

    PromesasPendientes<ResultadoImportacion> importaciones;
    PromesasPendientes<ResultadoImportacion> reemplazos;
    PromesasPendientes<ResultadoEstado> estados;
    PromesasPendientes<ResultadoResumen> resumenes;
    PromesasPendientes<ResultadoEliminacion> eliminaciones;
    PromesasPendientes<ResultadoReconciliacion> reconciliaciones;

    satcfdi::ErrorCredencialSat errorMaterial =
        errorAlmacen(satcfdi::ErrorSecretStore::Categoria::CredencialNoEncontrada);

    QStringList historial;
    QList<LlamadaRegistro> registros; // importar y reemplazar, en orden
    QList<satcfdi::PerfilId> idsEstado;
    QList<satcfdi::PerfilId> idsResumen;
    QList<satcfdi::PerfilId> idsEliminar;

private:
    void registrar(const satcfdi::PerfilId& perfilId, satcfdi::EntradaEFirma&& entradaMovida, bool esReemplazo)
    {
        const satcfdi::EntradaEFirma entrada = std::move(entradaMovida); // se limpia al salir
        historial.append(esReemplazo ? QStringLiteral("reemplazar") : QStringLiteral("importar"));
        registros.append({perfilId, esReemplazo, !entrada.rutaCertificado.isEmpty(),
                          !entrada.rutaLlavePrivada.isEmpty(), entrada.contrasena.tamano()});
    }
};

} // namespace fakes
