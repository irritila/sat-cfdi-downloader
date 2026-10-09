#pragma once

// Definicion compartida del menu del menu bar (T012 D6). Sin AppKit ni
// QWidgets: la usan MacOSIntegration (para construir el QMenu real) y la
// herramienta de capturas del manual (para la ilustracion). Orden, textos,
// separadores, visibilidad, habilitacion, marca e icono salen SOLO de aqui;
// los textos variables vienen de MacOSMapeos. Textos segun el traspaso de
// T013 (UX-37, UX-38).

#include "infrastructure/os/macos/MacOSMapeos.h"
#include "ports/OSIntegration.h"

#include <QCoreApplication>
#include <QList>
#include <QString>

namespace satcfdi::menubar {

enum class Id {
    MostrarVentana,
    NuevaSolicitud,
    AbrirCarpetaPaquetes,
    EstadoMonitoreo,     // no seleccionable
    PendientesMonitoreo, // no seleccionable; oculta si 0
    AccionMonitoreo,     // Pausar/Reanudar
    InicioAutomatico,    // marcable
    EstadoLoginItem,     // no seleccionable
    AbrirAjustesLoginItem,
    EstadoNotificaciones, // no seleccionable
    SolicitarPermiso,
    EnviarPrueba,
    AbrirAjustesNotificaciones,
    Salir,
    Separador,
};

// Estado que determina textos y visibilidad (lo que refleja el adaptador).
struct EstadoMenu {
    bool monitoreoPausado = false;
    OSIntegration::EstadoMonitoreo monitoreo;
    bool preferenciaLoginItem = false;
    OSIntegration::LoginItemStatus loginItem = OSIntegration::LoginItemStatus::Disabled;
    OSIntegration::NotificationStatus notificaciones = OSIntegration::NotificationStatus::NotDetermined;
};

struct Entrada {
    Id id = Id::Separador;
    QString texto;
    // Icono plantilla de las lineas informativas (UX-38): nombre del SVG en
    // src/presentation/qml/assets/icons/ sin extension; vacio = sin icono.
    QString icono;
    bool separador = false;
    bool habilitada = true;
    bool visible = true;
    bool marcable = false;
    bool marcada = false;
};

// Texto visible (T013 D6: UTF-8 con ortografia completa, traducible).
inline QString textoMenu(const char* texto)
{
    return QCoreApplication::translate("MenuBar", texto);
}

// UX-38: activo -> check-circle, en pausa -> clock, trabajando ->
// arrow-down-circle-dotted. Deteniendo/detenido no llevan icono.
inline QString iconoEstadoMonitoreo(OSIntegration::EstadoMonitoreo::Fase fase)
{
    using Fase = OSIntegration::EstadoMonitoreo::Fase;
    switch (fase) {
    case Fase::ActivoEnEspera:
        return QStringLiteral("check-circle");
    case Fase::Pausado:
        return QStringLiteral("clock");
    case Fase::Ejecutando:
        return QStringLiteral("arrow-down-circle-dotted");
    case Fase::Deteniendo:
    case Fase::Detenido:
        break;
    }
    return {};
}

// Estructura fija: orden y separadores.
inline const QList<Id>& estructura()
{
    static const QList<Id> e{
        Id::MostrarVentana,       Id::NuevaSolicitud,        Id::AbrirCarpetaPaquetes, Id::Separador,
        Id::EstadoMonitoreo,      Id::PendientesMonitoreo,   Id::AccionMonitoreo,      Id::Separador,
        Id::InicioAutomatico,     Id::EstadoLoginItem,       Id::AbrirAjustesLoginItem, Id::Separador,
        Id::EstadoNotificaciones, Id::SolicitarPermiso,      Id::EnviarPrueba,
        Id::AbrirAjustesNotificaciones, Id::Separador,       Id::Salir,
    };
    return e;
}

// Entrada de `id` para `estado` (textos, visibilidad, habilitacion, marca).
inline Entrada entrada(Id id, const EstadoMenu& s)
{
    using LoginItemStatus = OSIntegration::LoginItemStatus;
    using NotificationStatus = OSIntegration::NotificationStatus;
    Entrada e;
    e.id = id;
    switch (id) {
    case Id::MostrarVentana:
        e.texto = textoMenu("Mostrar ventana");
        break;
    case Id::NuevaSolicitud:
        e.texto = textoMenu("Nueva solicitud…");
        break;
    case Id::AbrirCarpetaPaquetes:
        e.texto = textoMenu("Abrir carpeta de paquetes");
        break;
    case Id::EstadoMonitoreo:
        e.texto = macos::textoEstadoMonitoreo(s.monitoreo);
        e.icono = iconoEstadoMonitoreo(s.monitoreo.fase);
        e.habilitada = false;
        break;
    case Id::PendientesMonitoreo:
        e.texto = macos::textoPendientes(s.monitoreo.pendientes);
        e.icono = QStringLiteral("clock");
        e.habilitada = false;
        e.visible = s.monitoreo.pendientes > 0;
        break;
    case Id::AccionMonitoreo:
        e.texto = macos::textoAccionMonitoreo(s.monitoreoPausado);
        break;
    case Id::InicioAutomatico:
        e.texto = textoMenu("Abrir al iniciar sesión");
        e.marcable = true;
        e.marcada = s.preferenciaLoginItem;
        e.habilitada = s.loginItem != LoginItemStatus::Unavailable || s.preferenciaLoginItem;
        break;
    case Id::EstadoLoginItem:
        e.texto = macos::textoEstadoLoginItem(s.loginItem);
        e.habilitada = false;
        break;
    case Id::AbrirAjustesLoginItem:
        e.texto = textoMenu("Abrir ajustes de inicio de sesión…");
        e.visible = s.loginItem == LoginItemStatus::RequiresApproval || s.loginItem == LoginItemStatus::Rejected;
        break;
    case Id::EstadoNotificaciones:
        e.texto = macos::textoEstadoNotificaciones(s.notificaciones);
        e.habilitada = false;
        break;
    case Id::SolicitarPermiso:
        e.texto = textoMenu("Solicitar permiso de notificaciones");
        e.visible = s.notificaciones == NotificationStatus::NotDetermined;
        break;
    case Id::EnviarPrueba:
        e.texto = textoMenu("Enviar notificación de prueba");
        e.visible = s.notificaciones == NotificationStatus::Granted || s.notificaciones == NotificationStatus::Denied;
        break;
    case Id::AbrirAjustesNotificaciones:
        e.texto = textoMenu("Abrir ajustes de notificaciones…");
        e.visible = s.notificaciones == NotificationStatus::Denied;
        break;
    case Id::Salir:
        e.texto = textoMenu("Salir de SAT CFDI Downloader");
        break;
    case Id::Separador:
        e.separador = true;
        break;
    }
    return e;
}

// Menu completo para `estado`, en orden (incluye entradas ocultas con
// visible=false y separadores).
inline QList<Entrada> entradas(const EstadoMenu& estado)
{
    QList<Entrada> r;
    for (Id id : estructura()) {
        r.append(entrada(id, estado));
    }
    return r;
}

} // namespace satcfdi::menubar
