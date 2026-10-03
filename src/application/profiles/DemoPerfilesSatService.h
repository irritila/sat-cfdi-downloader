#pragma once

#include "application/profiles/PerfilesSatService.h"

#include <QList>

namespace satcfdi {

// DEMO (T002, adaptado a T003 y T005.1): catalogo de perfiles en memoria SOLO
// para el shell demo y pruebas de presentacion; el root productivo no lo
// usa. Sin credenciales, Keychain ni persistencia. Futures ya completados;
// senales sincronas antes de devolver. Siembra con sembrar().
class DemoPerfilesSatService final : public PerfilesSatService {
    Q_OBJECT

public:
    // Catalogo demo: dos perfiles activos y uno inactivo. Ids y RFC
    // deterministas.
    static QList<PerfilResumen> perfilesDemo();

    explicit DemoPerfilesSatService(QList<PerfilResumen> perfiles = perfilesDemo(),
                                    QObject* parent = nullptr);

    QFuture<ResultadoLista> listarNoEliminados() override;
    QFuture<ResultadoPerfil> obtener(const PerfilId& id) override;
    QFuture<ResultadoCrear> crear(const QString& rfc, const QString& nombre) override;
    QFuture<ResultadoActualizar> actualizarNombre(const PerfilId& id, const QString& nombre) override;

    // Siembra de datos demo/prueba (fuera del contrato): misma validacion que
    // crear() y RFC existente -> RfcDuplicado, pero respeta `activo`. Emite
    // perfilesCambiaron() al agregar.
    Resultado<PerfilResumen, ErrorCrearPerfil> sembrar(const QString& rfc, const QString& nombre, bool activo);

    // Catalogo completo actual (incluye inactivos), para sincronizar
    // DemoSolicitudesService::setPerfiles().
    const QList<PerfilResumen>& perfiles() const noexcept { return m_perfiles; }

private:
    QList<PerfilResumen> m_perfiles;
};

} // namespace satcfdi
