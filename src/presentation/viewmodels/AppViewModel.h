#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

class NuevaSolicitudViewModel;
class SolicitudDetailViewModel;

// Navegacion del shell: pagina actual y solicitud seleccionada por id (UUID).
//
// - mostrarNueva() reinicia el formulario.
// - abrirDetalle(id) carga el detalle por id; ids no canonicos se rechazan.
// - NuevaSolicitudViewModel::submitted(id) abre el detalle de la nueva
//   solicitud. La lista se refresca sola con solicitudActualizada.
class AppViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea el composition root.")

    Q_PROPERTY(Pagina pagina READ pagina NOTIFY paginaChanged)
    Q_PROPERTY(QString solicitudSeleccionadaId READ solicitudSeleccionadaId
                   NOTIFY solicitudSeleccionadaIdChanged)

public:
    enum class Pagina {
        Lista,
        Nueva,
        Detalle,
    };
    Q_ENUM(Pagina)

    // Ambos view models son obligatorios y deben vivir mas que este objeto.
    AppViewModel(NuevaSolicitudViewModel* nuevaSolicitud,
                 SolicitudDetailViewModel* detalle,
                 QObject* parent = nullptr);

    Pagina pagina() const { return m_pagina; }
    QString solicitudSeleccionadaId() const { return m_solicitudSeleccionadaId; }

    Q_INVOKABLE void mostrarLista();
    Q_INVOKABLE void mostrarNueva();
    // Devuelve false (sin navegar) si `id` no es un UUID canonico.
    Q_INVOKABLE bool abrirDetalle(const QString& id);

signals:
    void paginaChanged();
    void solicitudSeleccionadaIdChanged();

private:
    void setPagina(Pagina pagina);

    QPointer<NuevaSolicitudViewModel> m_nuevaSolicitud;
    QPointer<SolicitudDetailViewModel> m_detalle;
    Pagina m_pagina = Pagina::Lista;
    QString m_solicitudSeleccionadaId;
};

} // namespace satcfdi
