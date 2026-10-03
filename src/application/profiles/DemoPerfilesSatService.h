#pragma once

#include "application/profiles/PerfilesSatService.h"

#include <QList>

namespace satcfdi {

// DEMO (T002, adaptado al contrato T003): catalogo de perfiles en memoria
// SOLO para el shell y pruebas de presentacion. Sin credenciales, Keychain ni
// persistencia. Futures ya completados; senales sincronas antes de devolver.
class DemoPerfilesSatService final : public PerfilesSatService {
    Q_OBJECT

public:
    // Catalogo demo: dos perfiles activos y uno inactivo (para probar que el
    // inactivo no aparece ni se acepta al crear). Ids y RFC deterministas.
    static QList<PerfilResumen> perfilesDemo();

    explicit DemoPerfilesSatService(QList<PerfilResumen> perfiles = perfilesDemo(),
                                    QObject* parent = nullptr);

    QFuture<ResultadoLista> listarActivos() override;

    // Valida RFC (normalizado) y razon social; RFC ya existente ->
    // ErrorCrearPerfil::Integridad. Agrega el perfil y emite perfilesCambiaron().
    QFuture<ResultadoCrearPerfil> crearPerfilSimulado(const NuevoPerfilSimuladoRequest& request) override;

    // Catalogo completo actual (incluye inactivos), para sincronizar
    // DemoSolicitudesService::setPerfiles().
    const QList<PerfilResumen>& perfiles() const noexcept { return m_perfiles; }

private:
    QList<PerfilResumen> m_perfiles;
};

} // namespace satcfdi
