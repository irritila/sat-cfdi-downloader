#pragma once

#include "infrastructure/sat/SatOperaciones.h"
#include "infrastructure/sat/TokenSat.h"

#include <QByteArray>
#include <QFuture>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QUrl>

#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <optional>

class QNetworkReply;

namespace satcfdi::sat {

// Fase en la que termino una peticion (T006, D5).
// - AntesDeEnvio: fallo o venció el deadline SIN observar requestSent (DNS,
//   conexion rechazada, TLS); el SAT no recibio la peticion.
// - DespuesDeEnvio: se observo requestSent y no hubo HTTP completo (corte,
//   timeout, respuesta parcial). Resultado INCIERTO: una solicitud en esta
//   fase NUNCA se reenvia.
// - RespuestaExplicita: HTTP completo con codigo de estado (200 con resultado
//   o 500 con Fault, entre otros); el cuerpo se parsea aparte.
enum class FaseResultado {
    AntesDeEnvio,
    DespuesDeEnvio,
    RespuestaExplicita,
};

QString claveEstable(FaseResultado fase);

struct PeticionSat {
    QUrl url;
    QString soapAction; // sin comillas; el cliente agrega las comillas del header
    QByteArray sobre;   // SOAP completo
};

struct ResultadoHttp {
    FaseResultado fase = FaseResultado::AntesDeEnvio;
    std::optional<int> estadoHttp; // solo en RespuestaExplicita
    QByteArray cuerpo;             // solo en RespuestaExplicita (descomprimido por Qt)
    bool deadlineVencido = false;
    bool cancelado = false;        // T009: abortada por cancelarEnCurso()
    QString diagnostico;           // enum de QNetworkReply::NetworkError, sin URL ni cuerpo
};

// Cliente HTTP SOAP 1.1 del spike (D5). QObject del hilo que lo crea (con
// event loop; la CLI usa QCoreApplication). Peticiones SERIALES: una a la
// vez, en orden de llamada; las demas esperan en cola. Deadline por peticion
// con QTimer (por defecto 120 s). Sin redirecciones (politica manual), sin
// HTTP/2 y sin cache. El token se recibe por referencia y solo se copia al
// header Authorization: WRAP access_token="..." de la peticion en curso.
// Higiene de memoria (best effort, D8): el valor del header es un QByteArray
// compartido con QNetworkRequest; el cliente suelta su referencia al iniciar
// la peticion y Qt la libera al destruir el reply, sin borrado garantizado.
// El cuerpo de la respuesta de Autentica contiene el token: el consumidor debe
// parsearlo con parsearAutenticaYLimpiar() y no conservar ResultadoHttp.
//
// Advertencia de reenvio: si Qt reabre la conexion por su cuenta, la prueba
// `sinReenvioTrasCorte` lo detecta; el cliente nunca reintenta.
class ClienteHttpSat final : public QObject {
    Q_OBJECT

public:
    explicit ClienteHttpSat(QObject* parent = nullptr);
    ~ClienteHttpSat() override;

    void establecerDeadline(std::chrono::milliseconds deadline) { m_deadline = deadline; }
    std::chrono::milliseconds deadline() const noexcept { return m_deadline; }

    // `token` puede ser nullptr (Autentica). El header se construye al
    // encolar; el token no se retiene.
    QFuture<ResultadoHttp> enviar(const PeticionSat& peticion, const TokenSat* token = nullptr);

    // T009: aborta la peticion EN CURSO (QNetworkReply::abort). Su resultado
    // conserva la fase segun requestSent (AntesDeEnvio/DespuesDeEnvio) con
    // cancelado=true; nunca es RespuestaExplicita. Sin peticion en curso, no
    // hace nada. Mismo hilo que el cliente.
    void cancelarEnCurso();

    // Peticiones en curso o en cola.
    int pendientes() const noexcept { return static_cast<int>(m_cola.size()) + (m_enCurso ? 1 : 0); }

private:
    struct Pendiente {
        PeticionSat peticion;
        QByteArray autorizacion;
        std::shared_ptr<QPromise<ResultadoHttp>> promesa;
    };

    void iniciarSiguiente();

    QNetworkAccessManager m_red;
    std::deque<Pendiente> m_cola;
    bool m_enCurso = false;
    std::function<void()> m_abortarEnCurso;
    std::chrono::milliseconds m_deadline{120000};
};

} // namespace satcfdi::sat
