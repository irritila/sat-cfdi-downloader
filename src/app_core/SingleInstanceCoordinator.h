#pragma once

#include <QObject>
#include <QString>

class QLocalServer;
class QLocalSocket;

namespace satcfdi {

// Instancia unica por usuario (T004 DA3, ADR 0003). IPC de runtime Qt con
// QLocalServer/QLocalSocket; no es infraestructura nativa.
//
// adquirir() (hilo grafico, ANTES del bootstrap SQLite), serializado entre
// procesos con un QLockFile (<socket>.lock) que se libera al volver:
// 1. listen(nombre). Exito -> Primary.
// 2. Si falla: conectar al canal. Si hay servidor -> enviar ActivateWindow ->
//    SecondaryActivated (el llamador termina con codigo 0).
// 3. Si no hay servidor (canal obsoleto de un proceso muerto): eliminar el
//    socket y reintentar listen() una sola vez. Exito -> Primary.
// 4. Cualquier otro caso -> Error.
//
// Primaria: las activaciones recibidas antes de habilitarEntrega() se
// encolan (coalescidas) y se entregan como una sola activacionSolicitada()
// al habilitar; despues se emite una por mensaje.
//
// El nombre es inyectable para pruebas (nombre relativo -> directorio
// temporal del usuario; ruta absoluta -> se usa tal cual).
class SingleInstanceCoordinator final : public QObject {
    Q_OBJECT

public:
    enum class Rol { Primary, SecondaryActivated, Error };
    Q_ENUM(Rol)

    // Mensaje del protocolo v1 (una linea UTF-8).
    static constexpr const char* kMensajeActivar = "ActivateWindow";
    // Espera maxima por el lock de adquisicion y antiguedad a partir de la
    // cual un lock de un proceso vivo se considera colgado.
    static constexpr int kEsperaLockMs = 5000;
    static constexpr int kLockObsoletoMs = 10000;

    // mx.adenium.satcfdi-downloader.<uid>.instance-v1
    static QString nombreCanalPorDefecto();

    explicit SingleInstanceCoordinator(QString nombreCanal = nombreCanalPorDefecto(),
                                       QObject* parent = nullptr);
    ~SingleInstanceCoordinator() override;

    SingleInstanceCoordinator(const SingleInstanceCoordinator&) = delete;
    SingleInstanceCoordinator& operator=(const SingleInstanceCoordinator&) = delete;

    // Una sola vez. `timeoutMs` acota cada espera de conexion/escritura.
    Rol adquirir(int timeoutMs = 1000);

    Rol rol() const noexcept { return m_rol; }
    const QString& nombreCanal() const noexcept { return m_nombre; }
    // Diagnostico breve (sin rutas) cuando rol() == Error.
    const QString& error() const noexcept { return m_error; }
    // Diagnostico: true si adquirir() tuvo que retirar un canal obsoleto.
    bool canalObsoletoRetirado() const noexcept { return m_obsoletoRetirado; }

    // Activaciones recibidas aun no entregadas.
    int activacionesPendientes() const noexcept { return m_pendientes; }

    // El consumidor (AppLifecycleController) ya esta conectado: entrega lo
    // encolado y, en adelante, emite en cuanto llega cada mensaje.
    void habilitarEntrega();

signals:
    void activacionSolicitada();

private:
    QString rutaLock() const;
    bool escuchar();
    bool enviarActivacion(int timeoutMs, bool* conecto);
    void atenderConexion();
    void leer(QLocalSocket* socket);
    void registrarActivacion();

    QString m_nombre;
    QLocalServer* m_servidor = nullptr;
    Rol m_rol = Rol::Error;
    bool m_adquirido = false;
    bool m_entregaHabilitada = false;
    bool m_obsoletoRetirado = false;
    int m_pendientes = 0;
    QString m_error;
};

} // namespace satcfdi
