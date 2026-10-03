#pragma once

#include "application/profiles/PerfilesSatService.h"

#include <QList>

namespace satcfdi {

// DEMO (T002): catalogo de perfiles en memoria, solo lectura, SOLO para el
// shell. Sin credenciales, Keychain ni persistencia. Futures ya completados.
class DemoPerfilesSatService final : public PerfilesSatService {
    Q_OBJECT

public:
    // Catalogo demo: dos perfiles activos y uno inactivo (para probar que el
    // inactivo no aparece ni se acepta al crear). Ids y RFC deterministas.
    static QList<PerfilResumen> perfilesDemo();

    explicit DemoPerfilesSatService(QList<PerfilResumen> perfiles = perfilesDemo(),
                                    QObject* parent = nullptr);

    QFuture<ResultadoLista> listarActivos() override;

private:
    QList<PerfilResumen> m_perfiles;
};

} // namespace satcfdi
