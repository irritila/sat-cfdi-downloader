#pragma once

// Servidor HTTP/1.1 minimo sobre QTcpServer local (T006; extraido y ampliado
// en T009 para las pruebas de SatGatewayProductivo). Solo 127.0.0.1; sin red.

#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

#include <utility>

namespace satpruebas {

// Servidor HTTP/1.1 minimo para pruebas. Cuenta peticiones COMPLETAS
// (encabezados + Content-Length) y actua segun el modo.
class ServidorPrueba : public QObject {
public:
    enum class Modo {
        Responder,          // responde `estado` + `cuerpo` y cierra
        CerrarTrasPeticion, // lee la peticion completa y corta sin responder
        Silencio,           // lee la peticion y nunca responde
        SinLeer,            // acepta TCP y no lee nada (TLS nunca avanza)
        Manual,             // guarda la conexion; la prueba llama responderPendiente()
        Incompleta,         // T009: cabecera con Content-Length mayor que lo enviado y corta
    };

    explicit ServidorPrueba(Modo modo) : m_modo(modo)
    {
        QObject::connect(&m_servidor, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = m_servidor.nextPendingConnection()) {
                s->setParent(this);
                ++conexiones;
                if (m_modo == Modo::SinLeer) {
                    continue;
                }
                QObject::connect(s, &QTcpSocket::readyRead, this, [this, s]() { leer(s); });
            }
        });
        m_servidor.listen(QHostAddress::LocalHost);
    }

    quint16 puerto() const { return m_servidor.serverPort(); }
    void cerrar() { m_servidor.close(); }

    int conexiones = 0;
    int peticiones = 0;
    QByteArray ultimaCabecera;
    QList<QByteArray> cabeceras; // T009: cabecera de cada peticion completa
    QByteArray ultimoCuerpo;
    QStringList bitacora;
    int estado = 200;
    QByteArray cuerpo;
    // T009: respuestas por peticion (FIFO); vacia -> estado/cuerpo.
    QList<std::pair<int, QByteArray>> respuestas;

    int pendientes() const { return static_cast<int>(m_pendientes.size()); }
    void responderPendiente()
    {
        QTcpSocket* s = m_pendientes.takeFirst();
        bitacora.append(QStringLiteral("respondida"));
        responder(s);
    }

private:
    void leer(QTcpSocket* s)
    {
        QByteArray& buf = m_buffers[s];
        buf += s->readAll();
        const qsizetype finCabecera = buf.indexOf("\r\n\r\n");
        if (finCabecera < 0) {
            return;
        }
        const QByteArray cabecera = buf.left(finCabecera);
        qsizetype largo = 0;
        for (const QByteArray& linea : cabecera.split('\n')) {
            if (linea.toLower().startsWith("content-length:")) {
                largo = linea.mid(15).trimmed().toLongLong();
            }
        }
        if (buf.size() < finCabecera + 4 + largo) {
            return;
        }
        ++peticiones;
        ultimaCabecera = cabecera;
        cabeceras.append(cabecera);
        ultimoCuerpo = buf.mid(finCabecera + 4, largo);
        bitacora.append(QStringLiteral("recibida:") + QString::fromUtf8(ultimoCuerpo));
        buf.clear();
        switch (m_modo) {
        case Modo::Responder: responder(s); break;
        case Modo::CerrarTrasPeticion: s->abort(); break;
        case Modo::Manual: m_pendientes.append(s); break;
        case Modo::Incompleta:
            s->write("HTTP/1.1 200 OK\r\nContent-Type: text/xml\r\nContent-Length: 100000\r\n\r\n<s:Envelope");
            s->flush();
            s->abort();
            break;
        case Modo::Silencio:
        case Modo::SinLeer: break;
        }
    }

    void responder(QTcpSocket* s)
    {
        const auto [codigo, contenido] = respuestas.isEmpty() ? std::pair<int, QByteArray>(estado, cuerpo)
                                                              : respuestas.takeFirst();
        QByteArray r = "HTTP/1.1 " + QByteArray::number(codigo) + (codigo == 200 ? " OK" : " Error")
                       + "\r\nContent-Type: text/xml; charset=utf-8\r\nContent-Length: "
                       + QByteArray::number(contenido.size()) + "\r\nConnection: close\r\n\r\n" + contenido;
        s->write(r);
        s->disconnectFromHost();
    }

    Modo m_modo;
    QTcpServer m_servidor;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QList<QTcpSocket*> m_pendientes;
};

} // namespace satpruebas
