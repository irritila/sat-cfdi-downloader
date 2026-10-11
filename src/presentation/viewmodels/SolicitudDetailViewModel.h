#pragma once

#include "application/profiles/PerfilConPreparacion.h"
#include "application/requests/SolicitudDtos.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace satcfdi {

class AccionesSolicitud;
class ConsultaExistenciaPaquetes;
class ConsultaPreparacionPerfiles;
class AccionesFinder;

class SolicitudesService;

// Detalle de una solicitud cargada por id (UUID texto), nunca por indice.
//
// Estados (`estado`): Ninguno, Cargando, Error, NoEncontrada y ConDatos.
// Bloques: metadata, filtros, estados (local/SAT/resumen + codigos SAT),
// paquetes y logs. Estados como claves estables; QML resuelve el texto.
// Opcionales ausentes: "" en textos, null en fechas/numeros.
//
// - solicitudActualizada(id) coincidente recarga.
// - solicitudEliminada(id) coincidente invalida cualquier lectura en curso y
//   pasa a NoEncontrada (una respuesta tardia no restaura el detalle).
// - eliminar() pide confirmacion en QML; al terminar con exito emite
//   eliminada(id) y AppViewModel regresa a la lista.
// Todas las operaciones usan tokens de generacion (DA6).
class SolicitudDetailViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea el composition root con un SolicitudesService.")

    Q_PROPERTY(QString solicitudId READ solicitudId NOTIFY solicitudIdChanged)
    Q_PROPERTY(Estado estado READ estado NOTIFY estadoChanged)
    Q_PROPERTY(bool cargando READ cargando NOTIFY estadoChanged)
    Q_PROPERTY(bool cargada READ cargada NOTIFY datosChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY estadoChanged)
    Q_PROPERTY(bool eliminando READ eliminando NOTIFY eliminacionChanged)
    Q_PROPERTY(QString errorEliminacion READ errorEliminacion NOTIFY eliminacionChanged)

    // Metadata
    Q_PROPERTY(QString perfilRfc READ perfilRfc NOTIFY datosChanged)
    Q_PROPERTY(QString rfcContraparte READ rfcContraparte NOTIFY datosChanged)
    Q_PROPERTY(QString tipoDescarga READ tipoDescarga NOTIFY datosChanged)
    Q_PROPERTY(QString fechaInicial READ fechaInicial NOTIFY datosChanged)
    Q_PROPERTY(QString fechaFinal READ fechaFinal NOTIFY datosChanged)
    Q_PROPERTY(QDateTime creadaEn READ creadaEn NOTIFY datosChanged)

    // Filtros persistidos
    Q_PROPERTY(QString fechaInicialSat READ fechaInicialSat NOTIFY datosChanged)
    Q_PROPERTY(QString fechaFinalSat READ fechaFinalSat NOTIFY datosChanged)
    Q_PROPERTY(QStringList rfcContrapartes READ rfcContrapartes NOTIFY datosChanged)
    Q_PROPERTY(QString tipoComprobante READ tipoComprobante NOTIFY datosChanged)
    Q_PROPERTY(QString complemento READ complemento NOTIFY datosChanged)

    // Estados (dos dimensiones separadas + resumen derivado)
    Q_PROPERTY(QString estadoLocal READ estadoLocal NOTIFY datosChanged)
    Q_PROPERTY(QVariant estadoSat READ estadoSat NOTIFY datosChanged)
    Q_PROPERTY(QString estadoResumen READ estadoResumen NOTIFY datosChanged)

    // Codigos y fechas SAT persistidos
    Q_PROPERTY(QString idSolicitudSat READ idSolicitudSat NOTIFY datosChanged)
    Q_PROPERTY(QString codEstatusSolicitud READ codEstatusSolicitud NOTIFY datosChanged)
    Q_PROPERTY(QString mensajeSolicitudSat READ mensajeSolicitudSat NOTIFY datosChanged)
    Q_PROPERTY(QString codigoEstadoSolicitud READ codigoEstadoSolicitud NOTIFY datosChanged)
    Q_PROPERTY(QString mensajeVerificacionSat READ mensajeVerificacionSat NOTIFY datosChanged)
    Q_PROPERTY(QVariant numeroCfdi READ numeroCfdi NOTIFY datosChanged)
    Q_PROPERTY(QVariant enviadaEn READ enviadaEn NOTIFY datosChanged)
    Q_PROPERTY(QVariant ultimaVerificacionEn READ ultimaVerificacionEn NOTIFY datosChanged)
    Q_PROPERTY(QString ultimoError READ ultimoError NOTIFY datosChanged)

    // Paquetes: {idPaqueteSat, estadoDescarga (clave), disponibleEn,
    // descargadoEn|null, vencidoEn|null, codigoDescargaSat ("" si no hay)}.
    Q_PROPERTY(QVariantList paquetes READ paquetes NOTIFY datosChanged)
    Q_PROPERTY(int totalPaquetes READ totalPaquetes NOTIFY datosChanged)
    // Logs: {tipoEvento, origen, origenCodigoSat ("" si no hay), codigoSat,
    // mensajeSat, creadoEn}; claves estables, creado_en ascendente.
    Q_PROPERTY(QVariantList logs READ logs NOTIFY datosChanged)

    // T007: acciones manuales (solo si el root conecto AccionesSolicitud).
    // puedeVerificar: Enviada sin estado SAT final. puedeReintentarDescarga:
    // algun paquete Disponible o Error. accionSolicitada: texto breve tras
    // pedir una accion (la UI se refresca por las senales del servicio).
    Q_PROPERTY(bool puedeVerificar READ puedeVerificar NOTIFY datosChanged)
    Q_PROPERTY(bool puedeReintentarDescarga READ puedeReintentarDescarga NOTIFY datosChanged)
    Q_PROPERTY(QString accionSolicitada READ accionSolicitada NOTIFY accionSolicitadaChanged)

    // T009: Enviar (solicitud Creada) solo con la credencial del perfil Lista
    // y perfil activo; motivoEnvio explica por que no (catalogo D10).
    // mensajeEstado: mensaje del catalogo para el estado actual (vacio si no
    // aplica). Cada paquete agrega "mensaje" en paquetes().
    Q_PROPERTY(bool puedeEnviar READ puedeEnviar NOTIFY datosChanged)
    Q_PROPERTY(bool envioVisible READ envioVisible NOTIFY datosChanged)
    Q_PROPERTY(QString motivoEnvio READ motivoEnvio NOTIFY datosChanged)
    Q_PROPERTY(QString mensajeEstado READ mensajeEstado NOTIFY datosChanged)

    // T013 (UX-22, UX-30): textos de ResumenEstado por estado de resumen
    // (tabla del traspaso). descripcionResumen de Terminada lleva los conteos
    // de paquetes; la de ErrorSat y Rechazada, el mensaje del catalogo.
    // todoDescargado: Terminada con paquetes y todos Descargado.
    Q_PROPERTY(QString titularResumen READ titularResumen NOTIFY datosChanged)
    Q_PROPERTY(QString descripcionResumen READ descripcionResumen NOTIFY datosChanged)
    Q_PROPERTY(bool todoDescargado READ todoDescargado NOTIFY datosChanged)

    // T009.1: Finder. paquetes()[i]["puedeMostrarFinder"] solo con existencia
    // Presente. puedeAbrirCarpeta: algun paquete Descargado. mensajeFinder:
    // aviso D7 accesible del ultimo intento (vacio si se abrio).
    Q_PROPERTY(bool puedeAbrirCarpeta READ puedeAbrirCarpeta NOTIFY datosChanged)
    Q_PROPERTY(QString mensajeFinder READ mensajeFinder NOTIFY mensajeFinderChanged)

public:
    enum class Estado {
        Ninguno,
        Cargando,
        Error,
        NoEncontrada,
        ConDatos,
    };
    Q_ENUM(Estado)

    // `servicio` no debe ser nulo y debe vivir mas que el view model.
    explicit SolicitudDetailViewModel(SolicitudesService* servicio, QObject* parent = nullptr);

    QString solicitudId() const { return m_solicitudId; }
    Estado estado() const { return m_estado; }
    bool cargando() const { return m_estado == Estado::Cargando; }
    bool cargada() const { return m_detalle.has_value(); }
    QString errorMessage() const { return m_errorMessage; }
    bool eliminando() const { return m_eliminando; }
    QString errorEliminacion() const { return m_errorEliminacion; }

    QString perfilRfc() const;
    QString rfcContraparte() const;
    QString tipoDescarga() const;
    QString fechaInicial() const;
    QString fechaFinal() const;
    QDateTime creadaEn() const;
    QString fechaInicialSat() const;
    QString fechaFinalSat() const;
    QStringList rfcContrapartes() const;
    QString tipoComprobante() const;
    QString complemento() const;
    QString estadoLocal() const;
    QVariant estadoSat() const;
    QString estadoResumen() const;
    QString idSolicitudSat() const;
    QString codEstatusSolicitud() const;
    QString mensajeSolicitudSat() const;
    QString codigoEstadoSolicitud() const;
    QString mensajeVerificacionSat() const;
    QVariant numeroCfdi() const;
    QVariant enviadaEn() const;
    QVariant ultimaVerificacionEn() const;
    QString ultimoError() const;
    QVariantList paquetes() const;
    int totalPaquetes() const;
    QVariantList logs() const;

    // Carga el detalle del id. Un id no canonico pasa a NoEncontrada sin consultar.
    Q_INVOKABLE void cargar(const QString& id);
    Q_INVOKABLE void recargar();
    Q_INVOKABLE void limpiar();
    // Eliminacion logica local de la solicitud actual (confirmada en QML).
    Q_INVOKABLE void eliminar();

    void setAccionesSolicitud(AccionesSolicitud* acciones);
    // T008 D11: existencia del ZIP de cada paquete Descargado. No propietario.
    // paquetes()[i]["existencia"]: "" (no aplica), "Comprobando", "Presente",
    // "NoEncontrado" o "ErrorComprobacion". Nunca cambia el estado persistido.
    void setConsultaExistencia(ConsultaExistenciaPaquetes* consulta);
    // T009: preparacion de credenciales para habilitar Enviar. No propietario.
    void setConsultaPreparacion(ConsultaPreparacionPerfiles* consulta);
    // Reconsulta la preparacion (p. ej. al cambiar una credencial).
    void recalcularPreparacion();
    bool envioVisible() const;
    bool puedeEnviar() const;
    QString motivoEnvio() const;
    QString mensajeEstado() const;
    QString titularResumen() const;
    QString descripcionResumen() const;
    bool todoDescargado() const;
    Q_INVOKABLE void enviar();

    void setAccionesFinder(AccionesFinder* acciones);
    bool puedeAbrirCarpeta() const;
    QString mensajeFinder() const { return m_mensajeFinder; }
    Q_INVOKABLE void mostrarEnFinder(const QString& idPaqueteSat);
    Q_INVOKABLE void abrirCarpetaSolicitud();
    // T014.4: aviso D7 de un "Mostrar en Finder" pedido fuera del detalle
    // (accion de notificacion). Invalida una accion de Finder en curso. Se
    // limpia, como el resto, al cargar otra solicitud.
    void mostrarAvisoFinder(const QString& mensaje);
    bool puedeVerificar() const;
    bool puedeReintentarDescarga() const;
    QString accionSolicitada() const { return m_accionSolicitada; }
    Q_INVOKABLE void verificarAhora();
    Q_INVOKABLE void reintentarDescarga();
    // T014.2 D1: reintenta solo ese paquete (prioridad Manual en el worker).
    // Solo si paquetes()[i]["puedeReintentar"]; con el monitoreo pausado
    // queda pendiente por paquete (D2) y se ve con "reintentoPendiente".
    Q_INVOKABLE void reintentarDescargaPaquete(const QString& idPaqueteSat);

signals:
    void solicitudIdChanged();
    void estadoChanged();
    void datosChanged();
    void eliminacionChanged();
    void accionSolicitadaChanged();
    void mensajeFinderChanged();
    // La eliminacion pedida desde este view model termino con exito
    // (incluye cambio=false: ya no existia).
    void eliminada(const QString& id);

private:
    void setEstado(Estado estado, const QString& mensaje = {});
    void setDetalle(std::optional<SolicitudDetalle> detalle);
    void setEliminacion(bool eliminando, const QString& error);

    QPointer<SolicitudesService> m_servicio;
    QString m_solicitudId;
    std::optional<SolicitudDetalle> m_detalle;
    Estado m_estado = Estado::Ninguno;
    QString m_errorMessage;
    bool m_eliminando = false;
    QString m_errorEliminacion;
    quint64 m_genCarga = 0;
    quint64 m_genEliminar = 0;
    AccionesSolicitud* m_acciones = nullptr;
    ConsultaExistenciaPaquetes* m_existencia = nullptr;
    ConsultaPreparacionPerfiles* m_preparacion = nullptr;
    // Resultado de la consulta de preparacion del perfil (por RFC).
    std::optional<std::pair<PreparacionPerfil, bool>> m_credencial; // (preparacion, activo)
    quint64 m_genPreparacion = 0;
    AccionesFinder* m_finder = nullptr;
    QString m_mensajeFinder;
    quint64 m_genFinder = 0;
    void setMensajeFinder(const QString& mensaje);
    template <typename Futuro>
    void atenderFinder(Futuro futuro);
    QHash<QString, QString> m_existencias; // idPaqueteSat -> clave
    quint64 m_genExistencia = 0;
    void consultarExistencias();
    QString m_accionSolicitada;
};

} // namespace satcfdi
