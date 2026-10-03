#pragma once

#include "PerfilesDisponiblesModel.h"

#include "application/common/Errores.h"
#include "application/requests/SolicitudDtos.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace satcfdi {

class PerfilesSatService;
class SolicitudesService;

// Formulario de nueva solicitud (T002, flujo T003 DA1/DA6).
//
// Campos editables como texto/claves estables: perfilId (UUID), tipoDescarga
// ("Emitidos" | "Recibidos"), fechaInicial/fechaFinal (ISO yyyy-MM-dd),
// rfcContraparte, tipoComprobante ("" | I | E | T | N | P) y complemento
// (opcionales). Validacion superficial local; las reglas siguen en el servicio.
//
// submit(): valida, toma un snapshot inmutable del request y llama
// evaluarDuplicado(). Libre -> crear(); Bloqueado -> errorMessage y
// solicitudExistenteId; RequiereConfirmacion -> confirmacionPendiente con
// motivoDuplicado. confirmarDuplicado() llama crearLocal(snapshot, Confirmada);
// cancelarDuplicado() lo descarta. Si crear() devuelve RequiereConfirmacion
// (carrera) se abre la misma confirmacion. Exito: submitted(id).
//
// Sin perfiles activos (sinPerfiles) se ofrece "Crear perfil simulado" con
// perfilSimuladoRfc/perfilSimuladoRazonSocial (prellenados con valores demo
// editables). El perfil creado se selecciona al refrescar perfilesDisponibles.
//
// Cada operacion asincrona usa su token de generacion: respuestas tardias de
// una operacion invalidada (reiniciar, cancelar, nueva peticion) se descartan.
class NuevaSolicitudViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea el composition root con SolicitudesService y PerfilesSatService.")

    Q_PROPERTY(QString perfilId READ perfilId WRITE setPerfilId NOTIFY perfilIdChanged)
    Q_PROPERTY(QString tipoDescarga READ tipoDescarga WRITE setTipoDescarga NOTIFY tipoDescargaChanged)
    Q_PROPERTY(QString fechaInicial READ fechaInicial WRITE setFechaInicial NOTIFY fechaInicialChanged)
    Q_PROPERTY(QString fechaFinal READ fechaFinal WRITE setFechaFinal NOTIFY fechaFinalChanged)
    Q_PROPERTY(QString rfcContraparte READ rfcContraparte WRITE setRfcContraparte NOTIFY rfcContraparteChanged)
    Q_PROPERTY(QString tipoComprobante READ tipoComprobante WRITE setTipoComprobante NOTIFY tipoComprobanteChanged)
    Q_PROPERTY(QString complemento READ complemento WRITE setComplemento NOTIFY complementoChanged)

    Q_PROPERTY(satcfdi::PerfilesDisponiblesModel* perfilesDisponibles READ perfilesDisponibles CONSTANT)
    Q_PROPERTY(bool cargandoPerfiles READ cargandoPerfiles NOTIFY estadoChanged)
    Q_PROPERTY(bool sinPerfiles READ sinPerfiles NOTIFY estadoChanged)

    Q_PROPERTY(QString perfilSimuladoRfc READ perfilSimuladoRfc WRITE setPerfilSimuladoRfc NOTIFY perfilSimuladoChanged)
    Q_PROPERTY(QString perfilSimuladoRazonSocial READ perfilSimuladoRazonSocial WRITE setPerfilSimuladoRazonSocial NOTIFY perfilSimuladoChanged)
    Q_PROPERTY(bool creandoPerfil READ creandoPerfil NOTIFY estadoChanged)
    Q_PROPERTY(QString errorPerfilMessage READ errorPerfilMessage NOTIFY estadoChanged)

    Q_PROPERTY(bool canSubmit READ canSubmit NOTIFY canSubmitChanged)
    Q_PROPERTY(bool ocupado READ ocupado NOTIFY ocupadoChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

    Q_PROPERTY(bool confirmacionPendiente READ confirmacionPendiente NOTIFY estadoChanged)
    Q_PROPERTY(QString motivoDuplicado READ motivoDuplicado NOTIFY estadoChanged)
    Q_PROPERTY(QString solicitudExistenteId READ solicitudExistenteId NOTIFY estadoChanged)

public:
    // Valores por defecto del perfil simulado (RFC de pruebas publicado por el
    // SAT; editable en la UI).
    static QString rfcSimuladoPorDefecto();
    static QString razonSocialSimuladaPorDefecto();

    // Ambos servicios son obligatorios y deben vivir mas que el view model.
    NuevaSolicitudViewModel(SolicitudesService* solicitudes,
                            PerfilesSatService* perfiles,
                            QObject* parent = nullptr);

    QString perfilId() const { return m_perfilId; }
    QString tipoDescarga() const { return m_tipoDescarga; }
    QString fechaInicial() const { return m_fechaInicial; }
    QString fechaFinal() const { return m_fechaFinal; }
    QString rfcContraparte() const { return m_rfcContraparte; }
    QString tipoComprobante() const { return m_tipoComprobante; }
    QString complemento() const { return m_complemento; }

    PerfilesDisponiblesModel* perfilesDisponibles() const { return m_perfilesDisponibles; }
    bool cargandoPerfiles() const { return m_cargandoPerfiles; }
    bool sinPerfiles() const;

    QString perfilSimuladoRfc() const { return m_perfilSimuladoRfc; }
    QString perfilSimuladoRazonSocial() const { return m_perfilSimuladoRazonSocial; }
    bool creandoPerfil() const { return m_creandoPerfil; }
    QString errorPerfilMessage() const { return m_errorPerfil; }

    bool canSubmit() const;
    bool ocupado() const { return m_ocupado; }
    QString errorMessage() const;

    bool confirmacionPendiente() const { return m_snapshot.has_value(); }
    QString motivoDuplicado() const { return m_motivoDuplicado; }
    QString solicitudExistenteId() const { return m_solicitudExistenteId; }

    void setPerfilId(const QString& valor);
    void setTipoDescarga(const QString& valor);
    void setFechaInicial(const QString& valor);
    void setFechaFinal(const QString& valor);
    void setRfcContraparte(const QString& valor);
    void setTipoComprobante(const QString& valor);
    void setComplemento(const QString& valor);
    void setPerfilSimuladoRfc(const QString& valor);
    void setPerfilSimuladoRazonSocial(const QString& valor);

    Q_INVOKABLE void submit();
    Q_INVOKABLE void confirmarDuplicado();
    Q_INVOKABLE void cancelarDuplicado();
    // Limpia el formulario, invalida operaciones en curso y recarga perfiles.
    Q_INVOKABLE void reiniciar();
    Q_INVOKABLE void cargarPerfiles();
    Q_INVOKABLE void crearPerfilSimulado();

signals:
    void perfilIdChanged();
    void tipoDescargaChanged();
    void fechaInicialChanged();
    void fechaFinalChanged();
    void rfcContraparteChanged();
    void tipoComprobanteChanged();
    void complementoChanged();
    void perfilSimuladoChanged();
    void canSubmitChanged();
    void ocupadoChanged();
    void errorMessageChanged();
    // Cambio en estados derivados: perfiles, perfil simulado y confirmacion.
    void estadoChanged();
    void submitted(const QString& id);

private:
    struct Observables {
        bool canSubmit;
        bool ocupado;
        QString error;
        bool cargandoPerfiles;
        bool sinPerfiles;
        bool creandoPerfil;
        QString errorPerfil;
        bool pendiente;
        QString motivo;
        QString existente;
        bool operator==(const Observables&) const = default;
    };
    Observables observables() const;

    // Ejecuta `cambio`, revalida y notifica las propiedades derivadas que
    // cambiaron.
    template <typename F>
    void actualizar(F&& cambio);
    // Cambio de un campo editable: invalida confirmacion/error de servicio.
    template <typename F>
    void editar(F&& cambio);

    QString validar() const;
    NuevaSolicitudRequest construirRequest() const;
    void crearConfirmacion(const NuevaSolicitudRequest& request, ConfirmacionDuplicado confirmacion);
    // Aplica un ErrorCrear al estado visible (incluye RequiereConfirmacion).
    void aplicarErrorCrear(const ErrorCrear& error, const NuevaSolicitudRequest& request);

    QPointer<SolicitudesService> m_solicitudes;
    QPointer<PerfilesSatService> m_perfiles;
    PerfilesDisponiblesModel* m_perfilesDisponibles;

    QString m_perfilId;
    QString m_tipoDescarga;
    QString m_fechaInicial;
    QString m_fechaFinal;
    QString m_rfcContraparte;
    QString m_tipoComprobante;
    QString m_complemento;

    QString m_perfilSimuladoRfc;
    QString m_perfilSimuladoRazonSocial;
    QString m_perfilPorSeleccionar;

    bool m_ocupado = false;
    bool m_tocado = false;
    bool m_cargandoPerfiles = false;
    bool m_perfilesCargados = false;
    bool m_creandoPerfil = false;
    QString m_errorValidacion;
    QString m_errorServicio;
    QString m_errorPerfil;

    std::optional<NuevaSolicitudRequest> m_snapshot; // confirmacion pendiente
    QString m_motivoDuplicado;
    QString m_solicitudExistenteId;

    // Tokens de generacion (DA6).
    quint64 m_genEnvio = 0;
    quint64 m_genPerfiles = 0;
    quint64 m_genPerfilSimulado = 0;
};

} // namespace satcfdi
