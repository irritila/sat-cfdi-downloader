#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

class CredencialesSatService;
struct ErrorCredencialSat;

// Registro o reemplazo de la e.firma de un perfil (T005.1, DA3/DA4).
//
// Fases (`fase`): Capturando, Validando, Exito y Error. Durante Validando
// enviar() se rechaza (sin doble envio) y la UI sigue respondiendo: el
// trabajo corre en CredencialesSatService fuera del hilo grafico.
//
// Datos sensibles:
// - QML entrega QUrl locales (FileDialog) y la contrasena en enviar(). Las
//   rutas se convierten con toLocalFile() y viven SOLO en miembros privados
//   hasta entregarlas en EntradaEFirma; QML solo ve los basenames
//   (nombreCertificado, nombreLlave).
// - enviar() es SINCRONO: la contrasena pasa de inmediato a BufferSecreto y
//   EntradaEFirma (move-only) se mueve al servicio, que la custodia hacia el
//   dispatcher. Nunca hay rutas ni contrasena en propiedades, senales,
//   errores ni futures. QML limpia su campo en el mismo manejador.
//
// Errores: errorKey estable (categoria de SecretStore, PerfilInexistente,
// PerfilInactivo, CredencialExistente, Persistencia, Interno o validacion
// local), errorMessage visible fijo y campoConError tomado del ORIGEN del
// adaptador ("certificado" | "llave" | "contrasena" | ""). En reemplazo
// fallido el mensaje aclara que la e.firma anterior sigue registrada.
//
// Habilitacion: puedeEnviar exige certificado, llave y contrasena. QML solo
// informa SI hay contrasena (claveEscrita, un bool), nunca su texto; la
// validacion de enviar() (ContrasenaRequerida...) se conserva como defensa.
// `faltante` explica que falta para enviar.
//
// Reintento: si la operacion falla en el servicio, las rutas ya se entregaron
// (no se retienen) y tambien se descartan los basenames: los campos vuelven a
// "Ningun archivo" y requiereNuevaSeleccion pide elegir de nuevo. El error
// especifico se conserva.
//
// Generacion por operacion/perfil: iniciar() con otro perfil, cancelar() o un
// envio nuevo invalidan respuestas tardias. operacionTerminada(perfilId,
// exito) permite reverificar el estado real del perfil (tambien tras fallo).
class EFirmaFormViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea PresentacionViewModels.")

    Q_PROPERTY(Fase fase READ fase NOTIFY cambio)
    Q_PROPERTY(QString perfilId READ perfilId NOTIFY cambio)
    Q_PROPERTY(QString perfilRfc READ perfilRfc NOTIFY cambio)
    Q_PROPERTY(bool esReemplazo READ esReemplazo NOTIFY cambio)
    Q_PROPERTY(QString nombreCertificado READ nombreCertificado NOTIFY cambio)
    Q_PROPERTY(QString nombreLlave READ nombreLlave NOTIFY cambio)
    Q_PROPERTY(bool claveEscrita READ claveEscrita WRITE setClaveEscrita NOTIFY cambio)
    Q_PROPERTY(bool puedeEnviar READ puedeEnviar NOTIFY cambio)
    Q_PROPERTY(QString faltante READ faltante NOTIFY cambio)
    Q_PROPERTY(bool requiereNuevaSeleccion READ requiereNuevaSeleccion NOTIFY cambio)
    Q_PROPERTY(QString errorKey READ errorKey NOTIFY cambio)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY cambio)
    Q_PROPERTY(QString campoConError READ campoConError NOTIFY cambio)

public:
    enum class Fase {
        Capturando,
        Validando,
        Exito,
        Error,
    };
    Q_ENUM(Fase)

    // `credenciales` es obligatorio y debe vivir mas que el view model.
    explicit EFirmaFormViewModel(CredencialesSatService* credenciales, QObject* parent = nullptr);

    Fase fase() const { return m_fase; }
    QString perfilId() const { return m_perfilId; }
    QString perfilRfc() const { return m_perfilRfc; }
    bool esReemplazo() const { return m_esReemplazo; }
    QString nombreCertificado() const { return m_nombreCertificado; }
    QString nombreLlave() const { return m_nombreLlave; }
    bool claveEscrita() const { return m_claveEscrita; }
    void setClaveEscrita(bool valor);
    bool puedeEnviar() const;
    // Que falta para enviar (vacio si nada falta o no aplica).
    QString faltante() const;
    bool requiereNuevaSeleccion() const { return m_requiereNuevaSeleccion; }
    QString errorKey() const { return m_errorKey; }
    QString errorMessage() const { return m_errorMessage; }
    QString campoConError() const { return m_campoConError; }

    // Prepara la captura para un perfil (registro o reemplazo). Descarta
    // cualquier captura u operacion anterior.
    Q_INVOKABLE void iniciar(const QString& perfilId, const QString& perfilRfc, bool esReemplazo);
    // Archivo elegido en el FileDialog; solo se expone el basename.
    Q_INVOKABLE void seleccionarCertificado(const QUrl& archivo);
    Q_INVOKABLE void seleccionarLlave(const QUrl& archivo);
    // Envia la captura. `certificado`/`llave` vacios usan la seleccion previa.
    // Devuelve false si no se envio (validando, perfil o datos faltantes).
    Q_INVOKABLE bool enviar(const QUrl& certificado, const QUrl& llave, const QString& contrasena);
    // Descarta la captura (rutas, basenames y errores). Ignorado en Validando.
    Q_INVOKABLE void cancelar();
    // Descarta la seleccion de archivos (rutas privadas y basenames) en
    // cualquier fase, sin tocar la operacion en curso. Lo llama el dialogo al
    // cerrarse por cualquier via y al destruirse.
    Q_INVOKABLE void descartarSeleccion();
    // Abandona el contexto (navegar o cambiar de perfil): invalida la
    // operacion en curso aunque este Validando (su respuesta tardia se
    // descarta) y vuelve a Capturando sin seleccion.
    Q_INVOKABLE void abandonar();

signals:
    void cambio();
    void operacionTerminada(const QString& perfilId, bool exito);

private:
    void restablecer();
    void fallarLocal(const QString& clave, const QString& mensaje, const QString& campo);
    void aplicarError(const ErrorCredencialSat& error);
    // Convierte una URL local; vacio si no es local.
    static QString rutaLocal(const QUrl& archivo);

    QPointer<CredencialesSatService> m_credenciales;
    Fase m_fase = Fase::Capturando;
    QString m_perfilId;
    QString m_perfilRfc;
    bool m_esReemplazo = false;
    QString m_nombreCertificado;
    QString m_nombreLlave;
    QString m_errorKey;
    QString m_errorMessage;
    QString m_campoConError;
    bool m_claveEscrita = false;
    bool m_requiereNuevaSeleccion = false;

    // Rutas completas: privadas, nunca expuestas.
    QString m_rutaCertificado;
    QString m_rutaLlave;

    quint64 m_generacion = 0;
};

} // namespace satcfdi
