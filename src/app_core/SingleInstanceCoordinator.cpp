#include "SingleInstanceCoordinator.h"

#include <QDir>
#include <QLocalServer>
#include <QLockFile>
#include <QLocalSocket>

#include <unistd.h>

#ifndef SATCFDI_BUNDLE_ID
#define SATCFDI_BUNDLE_ID "mx.adenium.satcfdi-downloader"
#endif

namespace satcfdi {

QString SingleInstanceCoordinator::nombreCanalPorDefecto()
{
    return QStringLiteral("%1.%2.instance-v1")
        .arg(QStringLiteral(SATCFDI_BUNDLE_ID))
        .arg(static_cast<qulonglong>(::getuid()));
}

SingleInstanceCoordinator::SingleInstanceCoordinator(QString nombreCanal, QObject* parent)
    : QObject(parent)
    , m_nombre(std::move(nombreCanal))
{
}

SingleInstanceCoordinator::~SingleInstanceCoordinator()
{
    if (m_servidor) {
        m_servidor->close(); // retira el socket del sistema de archivos
    }
}

QString SingleInstanceCoordinator::rutaLock() const
{
    // Nombre relativo: el socket vive en el temporal del usuario (0700 en
    // macOS); el lock va junto a el. Ruta absoluta: junto al socket.
    const QString base = QDir::isAbsolutePath(m_nombre) ? m_nombre
                                                        : QDir(QDir::tempPath()).filePath(m_nombre);
    return base + QStringLiteral(".lock");
}

SingleInstanceCoordinator::Rol SingleInstanceCoordinator::adquirir(int timeoutMs)
{
    Q_ASSERT(!m_adquirido);
    m_adquirido = true;

    // Exclusion mutua entre arranques simultaneos: listen, connect y la
    // retirada del canal obsoleto ocurren con el lock tomado. QLockFile
    // recupera solo un lock cuyo PID ya no existe; kLockObsoletoMs cubre un
    // proceso vivo pero colgado.
    QLockFile lock(rutaLock());
    lock.setStaleLockTime(kLockObsoletoMs);
    if (!lock.tryLock(kEsperaLockMs)) {
        m_error = QStringLiteral("No se pudo coordinar la instancia unica (canal ocupado).");
        return m_rol = Rol::Error;
    }
    // El lock se libera al salir de esta funcion: ya escucha el servidor o ya
    // se entrego la activacion.

    if (escuchar()) {
        return m_rol = Rol::Primary;
    }

    // Con el lock tomado: si hay servidor vivo, esta es una secundaria.
    bool conecto = false;
    if (enviarActivacion(timeoutMs, &conecto)) {
        return m_rol = Rol::SecondaryActivated;
    }
    if (conecto) {
        m_error = QStringLiteral("La instancia primaria no recibio la activacion.");
        return m_rol = Rol::Error;
    }

    // Nadie escucha: canal obsoleto de un proceso terminado. Ningun otro
    // arranque puede crear el socket mientras tenemos el lock. Un reintento.
    QLocalServer::removeServer(m_nombre);
    m_obsoletoRetirado = true;
    if (escuchar()) {
        return m_rol = Rol::Primary;
    }
    m_error = QStringLiteral("No se pudo abrir el canal de instancia unica.");
    return m_rol = Rol::Error;
}

bool SingleInstanceCoordinator::escuchar()
{
    // Sin socketOptions a proposito: con opciones de acceso Qt crea el socket
    // aparte y lo RENOMBRA sobre la ruta, reemplazando el de una primaria
    // viva. Sin opciones, bind() falla si la ruta existe. El nombre relativo
    // vive en el directorio temporal del usuario (0700 en macOS).
    auto servidor = new QLocalServer(this);
    if (!servidor->listen(m_nombre)) {
        delete servidor;
        return false;
    }
    m_servidor = servidor;
    connect(m_servidor, &QLocalServer::newConnection, this,
            &SingleInstanceCoordinator::atenderConexion);
    return true;
}

bool SingleInstanceCoordinator::enviarActivacion(int timeoutMs, bool* conecto)
{
    QLocalSocket socket;
    socket.connectToServer(m_nombre);
    if (!socket.waitForConnected(timeoutMs)) {
        *conecto = false;
        return false;
    }
    *conecto = true;
    socket.write(kMensajeActivar);
    socket.write("\n");
    const bool escrito = socket.waitForBytesWritten(timeoutMs) || socket.bytesToWrite() == 0;
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState) {
        socket.waitForDisconnected(timeoutMs);
    }
    return escrito;
}

void SingleInstanceCoordinator::atenderConexion()
{
    while (QLocalSocket* socket = m_servidor->nextPendingConnection()) {
        socket->setParent(this);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { leer(socket); });
        connect(socket, &QLocalSocket::disconnected, socket, [this, socket] {
            leer(socket); // datos que llegaron junto con el cierre
            socket->deleteLater();
        });
        leer(socket);
    }
}

void SingleInstanceCoordinator::leer(QLocalSocket* socket)
{
    while (socket->canReadLine()) {
        const QByteArray linea = socket->readLine().trimmed();
        if (linea == kMensajeActivar) {
            registrarActivacion();
        }
        // Mensajes desconocidos (versiones futuras) se ignoran.
    }
}

void SingleInstanceCoordinator::registrarActivacion()
{
    if (m_entregaHabilitada) {
        emit activacionSolicitada();
    } else {
        ++m_pendientes;
    }
}

void SingleInstanceCoordinator::habilitarEntrega()
{
    m_entregaHabilitada = true;
    if (m_pendientes > 0) {
        m_pendientes = 0;
        emit activacionSolicitada();
    }
}

} // namespace satcfdi
