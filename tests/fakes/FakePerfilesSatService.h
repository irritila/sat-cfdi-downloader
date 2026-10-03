#pragma once

// PerfilesSatService falso con promesas manuales (T005.1). Header-only, hilo
// grafico. Cada llamada queda registrada y pendiente hasta que la prueba la
// resuelva; perfilesCambiaron se emite SOLO con emitirCambio() (simula el
// commit). Builders de resultados tipicos: perfil(), rfcDuplicado(),
// validacion(), inexistente(), errorPersistencia().

#include "PromesasPendientes.h"

#include "application/profiles/PerfilesSatService.h"

#include <QStringList>

namespace fakes {

class FakePerfilesSatService final : public satcfdi::PerfilesSatService {
    Q_OBJECT

public:
    using PerfilesSatService::PerfilesSatService;

    struct LlamadaCrear {
        QString rfc;
        QString nombre;
    };
    struct LlamadaActualizar {
        satcfdi::PerfilId id;
        QString nombre;
    };

    QFuture<ResultadoLista> listarNoEliminados() override
    {
        historial.append(QStringLiteral("listarNoEliminados"));
        return listas.nueva();
    }

    QFuture<ResultadoPerfil> obtener(const satcfdi::PerfilId& id) override
    {
        historial.append(QStringLiteral("obtener"));
        idsObtener.append(id);
        return obtenciones.nueva();
    }

    QFuture<ResultadoCrear> crear(const QString& rfc, const QString& nombre) override
    {
        historial.append(QStringLiteral("crear"));
        llamadasCrear.append({rfc, nombre});
        return creaciones.nueva();
    }

    QFuture<ResultadoActualizar> actualizarNombre(const satcfdi::PerfilId& id, const QString& nombre) override
    {
        historial.append(QStringLiteral("actualizarNombre"));
        llamadasActualizar.append({id, nombre});
        return actualizaciones.nueva();
    }

    // Siembra (fuera del contrato): resuelve la llamada `i` de
    // listarNoEliminados() con `perfiles`.
    void resolverLista(int i, QList<satcfdi::PerfilResumen> perfiles)
    {
        listas.resolver(i, ResultadoLista::exito(std::move(perfiles)));
    }

    // Simula el commit de un cambio.
    void emitirCambio() { emit perfilesCambiaron(); }

    // --- Builders ---------------------------------------------------------
    static satcfdi::PerfilResumen perfil(const char* rfc, const char* nombre, bool activo = true)
    {
        return {satcfdi::PerfilId::generar(), QString::fromLatin1(rfc), QString::fromUtf8(nombre), activo};
    }
    static satcfdi::ErrorCrearPerfil rfcDuplicado()
    {
        return satcfdi::ErrorCrearPerfil::desdePersistencia(satcfdi::ErrorPersistencia::de(
            satcfdi::ErrorPersistencia::Tipo::Unicidad, QStringLiteral("fake"),
            QStringLiteral("ux_perfil_sat_rfc_vigente")));
    }
    static satcfdi::ErrorCrearPerfil validacion(QList<satcfdi::CodigoValidacionPerfil> codigos)
    {
        return satcfdi::ErrorCrearPerfil::validacion(std::move(codigos));
    }
    static satcfdi::ErrorCrearPerfil errorPersistencia()
    {
        return satcfdi::ErrorCrearPerfil::desdePersistencia(
            satcfdi::ErrorPersistencia::de(satcfdi::ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("fake")));
    }
    static satcfdi::ErrorActualizarPerfil inexistente() { return satcfdi::ErrorActualizarPerfil::inexistente(); }

    PromesasPendientes<ResultadoLista> listas;
    PromesasPendientes<ResultadoPerfil> obtenciones;
    PromesasPendientes<ResultadoCrear> creaciones;
    PromesasPendientes<ResultadoActualizar> actualizaciones;

    QStringList historial;
    QList<satcfdi::PerfilId> idsObtener;
    QList<LlamadaCrear> llamadasCrear;
    QList<LlamadaActualizar> llamadasActualizar;
};

} // namespace fakes
