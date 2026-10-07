#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

class AccionesFinder;

class EFirmaFormViewModel;
class NuevaSolicitudViewModel;
class PerfilesSatViewModel;
class SolicitudDetailViewModel;

// Navegacion del shell: pagina actual y solicitud seleccionada por id (UUID).
//
// - mostrarNueva() reinicia el formulario.
// - abrirDetalle(id) carga el detalle por id; ids no canonicos se rechazan.
// - NuevaSolicitudViewModel::submitted(id) abre el detalle de la nueva
//   solicitud. La lista se refresca sola con listaCambiada.
// - SolicitudDetailViewModel::eliminada(id) regresa a la lista.
// - mostrarPerfiles() abre Perfiles SAT (T005.1 DA5) y recarga la lista de
//   perfiles; al salir de Perfiles se descarta la captura de e.firma (rutas
//   privadas incluidas) y el formulario de perfil.
// mostrarLista(), mostrarNueva() y mostrarPerfiles() son publicas e
// invocables desde C++ (AppLifecycleController, menu bar) y QML.
class AppViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea el composition root.")

    Q_PROPERTY(Pagina pagina READ pagina NOTIFY paginaChanged)
    Q_PROPERTY(QString solicitudSeleccionadaId READ solicitudSeleccionadaId
                   NOTIFY solicitudSeleccionadaIdChanged)
    // T009 D9: las notificaciones del sistema estan denegadas o no disponibles
    // (lo fija el composition root desde OSIntegration). No afecta estados.
    Q_PROPERTY(bool notificacionesDeshabilitadas READ notificacionesDeshabilitadas
                   NOTIFY notificacionesDeshabilitadasChanged)
    // T009.1: "Abrir carpeta de paquetes" (lista y menu bar). mensajeFinder:
    // aviso D7 accesible cuando no se pudo abrir (vacio si se abrio).
    Q_PROPERTY(bool puedeAbrirCarpetaPaquetes READ puedeAbrirCarpetaPaquetes NOTIFY mensajeFinderChanged)
    Q_PROPERTY(QString mensajeFinder READ mensajeFinder NOTIFY mensajeFinderChanged)

public:
    enum class Pagina {
        Lista,
        Nueva,
        Detalle,
        Perfiles,
    };
    Q_ENUM(Pagina)

    // Todos los view models son obligatorios y deben vivir mas que este objeto.
    AppViewModel(NuevaSolicitudViewModel* nuevaSolicitud,
                 SolicitudDetailViewModel* detalle,
                 PerfilesSatViewModel* perfiles,
                 EFirmaFormViewModel* eFirma,
                 QObject* parent = nullptr);

    Pagina pagina() const { return m_pagina; }
    QString solicitudSeleccionadaId() const { return m_solicitudSeleccionadaId; }

    bool notificacionesDeshabilitadas() const { return m_notificacionesDeshabilitadas; }
    void setNotificacionesDeshabilitadas(bool valor)
    {
        if (m_notificacionesDeshabilitadas != valor) {
            m_notificacionesDeshabilitadas = valor;
            emit notificacionesDeshabilitadasChanged();
        }
    }

    void setAccionesFinder(AccionesFinder* acciones)
    {
        m_finder = acciones;
        emit mensajeFinderChanged();
    }
    bool puedeAbrirCarpetaPaquetes() const { return m_finder != nullptr; }
    QString mensajeFinder() const { return m_mensajeFinder; }
    void setMensajeFinder(const QString& mensaje)
    {
        if (m_mensajeFinder != mensaje) {
            m_mensajeFinder = mensaje;
            emit mensajeFinderChanged();
        }
    }
    Q_INVOKABLE void abrirCarpetaPaquetes();

    Q_INVOKABLE void mostrarLista();
    Q_INVOKABLE void mostrarNueva();
    Q_INVOKABLE void mostrarPerfiles();
    // Devuelve false (sin navegar) si `id` no es un UUID canonico.
    Q_INVOKABLE bool abrirDetalle(const QString& id);

signals:
    void paginaChanged();
    void solicitudSeleccionadaIdChanged();
    void notificacionesDeshabilitadasChanged();
    void mensajeFinderChanged();

private:
    void setPagina(Pagina pagina);

    QPointer<NuevaSolicitudViewModel> m_nuevaSolicitud;
    QPointer<SolicitudDetailViewModel> m_detalle;
    QPointer<PerfilesSatViewModel> m_perfiles;
    QPointer<EFirmaFormViewModel> m_eFirma;
    Pagina m_pagina = Pagina::Lista;
    QString m_solicitudSeleccionadaId;
    bool m_notificacionesDeshabilitadas = false;
    AccionesFinder* m_finder = nullptr;
    QString m_mensajeFinder;
    quint64 m_genFinder = 0;
};

} // namespace satcfdi
