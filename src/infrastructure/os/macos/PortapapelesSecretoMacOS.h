#pragma once

#include "ports/OSIntegration.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <chrono>
#include <memory>

namespace satcfdi::macos {

// T025.6 H1/M1: copia de un texto secreto (clave de recuperacion) al
// portapapeles de macOS sin que salga del equipo ni quede indefinidamente:
// - `prepareForNewContentsWithOptions:NSPasteboardContentsCurrentHostOnly`:
//   no se publica en el Portapapeles universal (Handoff).
// - Tipos `public.utf8-plain-text` mas `org.nspasteboard.ConcealedType` y
//   `org.nspasteboard.TransientType` (convencion nspasteboard.org: los gestores
//   de portapapeles no lo guardan en su historial ni lo muestran).
// - Tras `borradoTras` (90 s por omision) borra el portapapeles SOLO si su
//   changeCount sigue siendo el de nuestra escritura (si el usuario copio otra
//   cosa, no se toca). Una copia nueva reinicia el plazo.
// - borrarSiSigueSiendoNuestro(): borrado inmediato con la misma regla (lo
//   llama MacOSIntegration::prepararSalida()).
// - B6: mientras hay un borrado pendiente mantiene una actividad
//   NSActivityUserInitiatedAllowingIdleSystemSleep (App Nap no atrasa el
//   temporizador; el sistema si puede dormir). Se termina al borrar, al vencer
//   el plazo aunque no se borre, con una copia nueva (se reemplaza) y al
//   destruirse.
// Nunca registra el texto. Hilo grafico (QTimer con afinidad al hilo grafico).
// `nombrePasteboard` vacio = NSPasteboard general; un nombre propio solo en
// pruebas (no toca el portapapeles del usuario).
class PortapapelesSecretoMacOS final : public QObject {
    Q_OBJECT

public:
    static constexpr std::chrono::milliseconds kBorradoPorOmision{90'000};

    explicit PortapapelesSecretoMacOS(QString nombrePasteboard = {},
                                      std::chrono::milliseconds borradoTras = kBorradoPorOmision,
                                      QObject* parent = nullptr);
    ~PortapapelesSecretoMacOS() override;

    // true si el SO acepto la escritura. Texto vacio -> false sin tocar nada.
    bool copiar(const QString& texto);
    // true si habia contenido nuestro y se borro.
    bool borrarSiSigueSiendoNuestro();
    bool borradoPendiente() const noexcept { return m_pendiente; }
    bool actividadActiva() const noexcept { return m_actividad != nullptr; }

private:
    QString m_nombre;
    QTimer m_temporizador;
    long m_changeCount = -1;
    bool m_pendiente = false;
    std::unique_ptr<OSIntegration::ActividadSistema> m_actividad;
};

} // namespace satcfdi::macos
