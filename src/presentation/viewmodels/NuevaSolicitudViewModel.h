#pragma once

#include "PerfilesDisponiblesModel.h"

#include "application/common/Errores.h"
#include "application/requests/SolicitudDtos.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <QDate>

#include <functional>
#include <optional>

namespace satcfdi {

class AccionesSolicitud;

class ConsultaPreparacionPerfiles;
class CredencialesSatService;
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
// Selector (T005.1, DA2): perfilesDisponibles contiene SOLO perfiles
// listoParaSolicitudes (ConsultaPreparacionPerfiles::
// listarListosParaSolicitudes; activo && e.firma Lista). Sin perfiles listos
// (sinPerfiles) QML ofrece "Administrar perfiles SAT" (navega a Perfiles). El
// selector no es frontera de seguridad: la creacion revalida (T009).
// Los perfiles se cargan en reiniciar() (al abrir el formulario), no al
// construir, para no consultar el almacen de credenciales en el arranque; se
// recargan con perfilesCambiaron/credencialCambio una vez cargados.
//
// Cada operacion asincrona usa su token de generacion: respuestas tardias de
// una operacion invalidada (reiniciar, cancelar, nueva peticion) se descartan.
class NuevaSolicitudViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea PresentacionViewModels.")

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

    Q_PROPERTY(bool canSubmit READ canSubmit NOTIFY canSubmitChanged)
    Q_PROPERTY(bool ocupado READ ocupado NOTIFY ocupadoChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)
    // T013 (UX-18): campo al que pertenece errorMessage, para mostrarlo bajo
    // el control y enfocarlo: "perfil", "fechaInicial", "fechaFinal",
    // "rfcContraparte", "tipoComprobante", "complemento" o "" (error general:
    // bloqueo por duplicado, persistencia, carga de perfiles).
    Q_PROPERTY(QString campoConError READ campoConError NOTIFY errorMessageChanged)

    Q_PROPERTY(bool confirmacionPendiente READ confirmacionPendiente NOTIFY estadoChanged)
    Q_PROPERTY(QString motivoDuplicado READ motivoDuplicado NOTIFY estadoChanged)
    Q_PROPERTY(QString solicitudExistenteId READ solicitudExistenteId NOTIFY estadoChanged)
    // T014.1 D4: id de la equivalente en la confirmacion de duplicado; "" si
    // no hay o si fue eliminada localmente (no tiene detalle).
    Q_PROPERTY(QString solicitudEquivalenteId READ solicitudEquivalenteId NOTIFY estadoChanged)

public:
    // Los tres servicios son obligatorios y deben vivir mas que el view
    // model. Construye su propia ConsultaPreparacionPerfiles.
    NuevaSolicitudViewModel(SolicitudesService* solicitudes,
                            PerfilesSatService* perfiles,
                            CredencialesSatService* credenciales,
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

    bool canSubmit() const;
    bool ocupado() const { return m_ocupado; }
    QString errorMessage() const;
    QString campoConError() const;

    bool confirmacionPendiente() const { return m_snapshot.has_value(); }
    QString motivoDuplicado() const { return m_motivoDuplicado; }
    QString solicitudExistenteId() const { return m_solicitudExistenteId; }
    QString solicitudEquivalenteId() const { return m_solicitudEquivalenteId; }

    // T014.1 D3: reloj inyectable para los atajos de periodo (por omision,
    // QDate::currentDate). Solo C++ (pruebas y composition root).
    void setReloj(std::function<QDate()> reloj);
    // Atajos de periodo: "mesActual" (dia 1 a hoy), "mesAnterior" (mes
    // completo) y "mismoMesAnioAnterior" (mes completo). Escribe fechas ISO en
    // fechaInicial/fechaFinal con la validacion de siempre. false si la clave
    // no existe.
    Q_INVOKABLE bool aplicarPeriodo(const QString& atajo);

    void setPerfilId(const QString& valor);
    void setTipoDescarga(const QString& valor);
    void setFechaInicial(const QString& valor);
    void setFechaFinal(const QString& valor);
    void setRfcContraparte(const QString& valor);
    void setTipoComprobante(const QString& valor);
    void setComplemento(const QString& valor);

    // T007: tras crear la solicitud, pide su envio (D4). No propietario.
    void setAccionesSolicitud(AccionesSolicitud* acciones) { m_acciones = acciones; }

    Q_INVOKABLE void submit();
    Q_INVOKABLE void confirmarDuplicado();
    Q_INVOKABLE void cancelarDuplicado();
    // Limpia el formulario, invalida operaciones en curso y recarga perfiles.
    Q_INVOKABLE void reiniciar();
    Q_INVOKABLE void cargarPerfiles();

signals:
    void perfilIdChanged();
    void tipoDescargaChanged();
    void fechaInicialChanged();
    void fechaFinalChanged();
    void rfcContraparteChanged();
    void tipoComprobanteChanged();
    void complementoChanged();
    void canSubmitChanged();
    void ocupadoChanged();
    void errorMessageChanged();
    // Cambio en estados derivados: perfiles y confirmacion.
    void estadoChanged();
    void submitted(const QString& id);

private:
    struct Observables {
        bool canSubmit;
        bool ocupado;
        QString error;
        bool cargandoPerfiles;
        bool sinPerfiles;
        bool pendiente;
        QString motivo;
        QString existente;
        QString equivalente;
        QString campo;
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

    // Valores iniciales del formulario sin cargar perfiles.
    void restablecerCampos();
    QString validar() const;
    // Campo de la primera regla de validar() que falla ("" si ninguna).
    QString campoDeValidacion() const;
    NuevaSolicitudRequest construirRequest() const;
    void crearConfirmacion(const NuevaSolicitudRequest& request, ConfirmacionDuplicado confirmacion);
    // Aplica un ErrorCrear al estado visible (incluye RequiereConfirmacion).
    void aplicarErrorCrear(const ErrorCrear& error, const NuevaSolicitudRequest& request);

    QPointer<SolicitudesService> m_solicitudes;
    AccionesSolicitud* m_acciones = nullptr;
    ConsultaPreparacionPerfiles* m_consulta;
    PerfilesDisponiblesModel* m_perfilesDisponibles;

    QString m_perfilId;
    QString m_tipoDescarga;
    QString m_fechaInicial;
    QString m_fechaFinal;
    QString m_rfcContraparte;
    QString m_tipoComprobante;
    QString m_complemento;

    bool m_ocupado = false;
    bool m_tocado = false;
    bool m_cargandoPerfiles = false;
    bool m_perfilesCargados = false;
    QString m_errorValidacion;
    QString m_errorServicio;
    QString m_campoServicio; // campo de m_errorServicio

    std::optional<NuevaSolicitudRequest> m_snapshot; // confirmacion pendiente
    QString m_motivoDuplicado;
    QString m_solicitudExistenteId;
    QString m_solicitudEquivalenteId;
    std::function<QDate()> m_reloj;

    // Tokens de generacion (DA6).
    quint64 m_genEnvio = 0;
    quint64 m_genPerfiles = 0;
};

} // namespace satcfdi
