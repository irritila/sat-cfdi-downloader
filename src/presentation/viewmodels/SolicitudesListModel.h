#pragma once

#include "application/requests/SolicitudDtos.h"

#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

class SolicitudesService;

// Lista de solicitudes para SolicitudesPage. Roles fijados por T002:
// id (UUID texto), perfilRfc, rfcContraparte (texto o ""), tipoDescarga
// (clave), fechaInicial/fechaFinal (ISO yyyy-MM-dd), estadoLocal (clave),
// estadoSat (clave o null), estadoResumen (clave derivada), creadaEn
// (QDateTime UTC) y totalPaquetes.
//
// Estados (propiedad `estado`): Cargando (primera carga o reintento tras
// error), Vacia, Error y ConDatos. Un refresco con datos ya visibles conserva
// ConDatos y solo activa `cargando`.
//
// Se refresca al construirse y con SolicitudesService::listaCambiada y
// solicitudActualizada. Consume QFuture con then(this, ...) y un token de
// generacion: respuestas tardias de una carga anterior se descartan.
class SolicitudesListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea el composition root con un SolicitudesService.")

    Q_PROPERTY(Estado estado READ estado NOTIFY estadoChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool cargando READ cargando NOTIFY cargandoChanged)
    Q_PROPERTY(bool vacio READ vacio NOTIFY vacioChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    enum class Estado {
        Cargando,
        Vacia,
        Error,
        ConDatos,
    };
    Q_ENUM(Estado)

    enum Rol {
        IdRole = Qt::UserRole + 1,
        PerfilRfcRole,
        RfcContraparteRole,
        TipoDescargaRole,
        FechaInicialRole,
        FechaFinalRole,
        EstadoLocalRole,
        EstadoSatRole,
        EstadoResumenRole,
        CreadaEnRole,
        TotalPaquetesRole,
        // T013 D8 (UX-09): nombre del perfil bajo el RFC. Vacio mientras el
        // DTO SolicitudResumen no lo traiga (pendiente de application).
        PerfilNombreRole,
    };
    Q_ENUM(Rol)

    // `servicio` no debe ser nulo y debe vivir mas que el modelo.
    explicit SolicitudesListModel(SolicitudesService* servicio, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Estado estado() const;
    int count() const { return int(m_filas.size()); }
    bool cargando() const { return m_cargando; }
    // Verdadero solo despues de una carga exitosa sin filas.
    bool vacio() const { return m_cargado && m_filas.isEmpty(); }
    QString errorMessage() const { return m_errorMessage; }

    // Fila de un id (UUID texto) o -1. La identidad nunca es el indice.
    Q_INVOKABLE int filaDe(const QString& id) const;
    // Id (UUID texto) de la fila visible `fila`, o "" si no existe. Permite a
    // QML abrir el detalle por id a partir de la fila con foco.
    Q_INVOKABLE QString idEn(int fila) const;

public slots:
    void refrescar();

signals:
    void estadoChanged();
    void countChanged();
    void cargandoChanged();
    void vacioChanged();
    void errorMessageChanged();

private:
    void setCargando(bool valor);
    void setErrorMessage(const QString& mensaje);
    // Ejecuta `cambio` y notifica estado/count/vacio si cambiaron.
    template <typename F>
    void notificar(F&& cambio);

    QPointer<SolicitudesService> m_servicio;
    QList<SolicitudResumen> m_filas;
    bool m_cargando = false;
    bool m_cargado = false;
    quint64 m_generacion = 0;
    QString m_errorMessage;
};

} // namespace satcfdi
