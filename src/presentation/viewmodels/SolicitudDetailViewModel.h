#pragma once

#include "application/requests/SolicitudDtos.h"

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace satcfdi {

class SolicitudesService;

// Detalle de una solicitud cargada por id (UUID texto), nunca por indice.
// Separa tres bloques: metadata, estado local / estado SAT y paquetes.
// Estados como claves estables; QML resuelve el texto visible.
// Se recarga si SolicitudesService::solicitudActualizada coincide con el id.
class SolicitudDetailViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea el composition root con un SolicitudesService.")

    Q_PROPERTY(QString solicitudId READ solicitudId NOTIFY solicitudIdChanged)
    Q_PROPERTY(bool cargando READ cargando NOTIFY cargandoChanged)
    Q_PROPERTY(bool cargada READ cargada NOTIFY datosChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

    // Metadata
    Q_PROPERTY(QString perfilRfc READ perfilRfc NOTIFY datosChanged)
    Q_PROPERTY(QString rfcContraparte READ rfcContraparte NOTIFY datosChanged)
    Q_PROPERTY(QString tipoDescarga READ tipoDescarga NOTIFY datosChanged)
    Q_PROPERTY(QString fechaInicial READ fechaInicial NOTIFY datosChanged)
    Q_PROPERTY(QString fechaFinal READ fechaFinal NOTIFY datosChanged)
    Q_PROPERTY(QDateTime creadaEn READ creadaEn NOTIFY datosChanged)

    // Estados (dos dimensiones separadas + resumen derivado)
    Q_PROPERTY(QString estadoLocal READ estadoLocal NOTIFY datosChanged)
    Q_PROPERTY(QVariant estadoSat READ estadoSat NOTIFY datosChanged)
    Q_PROPERTY(QString estadoResumen READ estadoResumen NOTIFY datosChanged)

    // Paquetes: lista de mapas {idPaqueteSat, estadoDescarga (clave),
    // disponibleEn (QDateTime), descargadoEn (QDateTime o null)}.
    Q_PROPERTY(QVariantList paquetes READ paquetes NOTIFY datosChanged)
    Q_PROPERTY(int totalPaquetes READ totalPaquetes NOTIFY datosChanged)

public:
    // `servicio` no debe ser nulo y debe vivir mas que el view model.
    explicit SolicitudDetailViewModel(SolicitudesService* servicio, QObject* parent = nullptr);

    QString solicitudId() const { return m_solicitudId; }
    bool cargando() const { return m_cargando; }
    bool cargada() const { return m_detalle.has_value(); }
    QString errorMessage() const { return m_errorMessage; }

    QString perfilRfc() const;
    QString rfcContraparte() const;
    QString tipoDescarga() const;
    QString fechaInicial() const;
    QString fechaFinal() const;
    QDateTime creadaEn() const;
    QString estadoLocal() const;
    QVariant estadoSat() const;
    QString estadoResumen() const;
    QVariantList paquetes() const;
    int totalPaquetes() const;

    // Carga el detalle del id. Un id no canonico muestra error sin consultar.
    Q_INVOKABLE void cargar(const QString& id);
    Q_INVOKABLE void limpiar();

signals:
    void solicitudIdChanged();
    void cargandoChanged();
    void datosChanged();
    void errorMessageChanged();

private:
    void setCargando(bool valor);
    void setErrorMessage(const QString& mensaje);
    void setDetalle(std::optional<SolicitudDetalle> detalle);

    QPointer<SolicitudesService> m_servicio;
    QString m_solicitudId;
    std::optional<SolicitudDetalle> m_detalle;
    bool m_cargando = false;
    quint64 m_generacion = 0;
    QString m_errorMessage;
};

} // namespace satcfdi
