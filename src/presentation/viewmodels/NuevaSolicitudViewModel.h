#pragma once

#include "PerfilesDisponiblesModel.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

class PerfilesSatService;
class SolicitudesService;

// Formulario de nueva solicitud (T002).
//
// Campos editables como texto/claves estables: perfilId (UUID), tipoDescarga
// ("Emitidos" | "Recibidos"), fechaInicial/fechaFinal (ISO yyyy-MM-dd) y
// rfcContraparte (opcional). Validacion superficial: perfil requerido y
// disponible, fechas validas y fechaFinal >= fechaInicial. Las reglas de
// negocio siguen en SolicitudesService::crear().
//
// errorMessage: error del servicio, o error de validacion una vez que el
// usuario edito el formulario o intento enviar. submit() con formulario
// invalido muestra el error y NO llama al servicio. Exito: submitted(id).
class NuevaSolicitudViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea el composition root con SolicitudesService y PerfilesSatService.")

    Q_PROPERTY(QString perfilId READ perfilId WRITE setPerfilId NOTIFY perfilIdChanged)
    Q_PROPERTY(QString tipoDescarga READ tipoDescarga WRITE setTipoDescarga NOTIFY tipoDescargaChanged)
    Q_PROPERTY(QString fechaInicial READ fechaInicial WRITE setFechaInicial NOTIFY fechaInicialChanged)
    Q_PROPERTY(QString fechaFinal READ fechaFinal WRITE setFechaFinal NOTIFY fechaFinalChanged)
    Q_PROPERTY(QString rfcContraparte READ rfcContraparte WRITE setRfcContraparte NOTIFY rfcContraparteChanged)
    Q_PROPERTY(satcfdi::PerfilesDisponiblesModel* perfilesDisponibles READ perfilesDisponibles CONSTANT)
    Q_PROPERTY(bool canSubmit READ canSubmit NOTIFY canSubmitChanged)
    Q_PROPERTY(bool ocupado READ ocupado NOTIFY ocupadoChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    // Ambos servicios son obligatorios y deben vivir mas que el view model.
    NuevaSolicitudViewModel(SolicitudesService* solicitudes,
                            PerfilesSatService* perfiles,
                            QObject* parent = nullptr);

    QString perfilId() const { return m_perfilId; }
    QString tipoDescarga() const { return m_tipoDescarga; }
    QString fechaInicial() const { return m_fechaInicial; }
    QString fechaFinal() const { return m_fechaFinal; }
    QString rfcContraparte() const { return m_rfcContraparte; }
    PerfilesDisponiblesModel* perfilesDisponibles() const { return m_perfilesDisponibles; }
    bool canSubmit() const { return m_errorValidacion.isEmpty() && !m_ocupado; }
    bool ocupado() const { return m_ocupado; }
    QString errorMessage() const;

    void setPerfilId(const QString& valor);
    void setTipoDescarga(const QString& valor);
    void setFechaInicial(const QString& valor);
    void setFechaFinal(const QString& valor);
    void setRfcContraparte(const QString& valor);

    Q_INVOKABLE void submit();
    // Limpia el formulario (valores por defecto) y recarga perfiles.
    Q_INVOKABLE void reiniciar();
    Q_INVOKABLE void cargarPerfiles();

signals:
    void perfilIdChanged();
    void tipoDescargaChanged();
    void fechaInicialChanged();
    void fechaFinalChanged();
    void rfcContraparteChanged();
    void canSubmitChanged();
    void ocupadoChanged();
    void errorMessageChanged();
    void submitted(const QString& id);

private:
    // Captura canSubmit/errorMessage, ejecuta `cambio` y notifica diferencias.
    template <typename F>
    void actualizar(F&& cambio);
    QString validar() const;
    void setOcupado(bool valor);

    QPointer<SolicitudesService> m_solicitudes;
    QPointer<PerfilesSatService> m_perfiles;
    PerfilesDisponiblesModel* m_perfilesDisponibles;

    QString m_perfilId;
    QString m_tipoDescarga;
    QString m_fechaInicial;
    QString m_fechaFinal;
    QString m_rfcContraparte;

    bool m_ocupado = false;
    bool m_tocado = false;
    QString m_errorValidacion;
    QString m_errorServicio;
};

} // namespace satcfdi
