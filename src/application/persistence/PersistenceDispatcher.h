#pragma once

#include <QFuture>
#include <QObject>
#include <QPromise>
#include <QThread>

#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace satcfdi {

// Ejecuta tareas de persistencia local en un QThread dedicado (ADR 0016, DA4).
// No es el OperacionExecutor de T007.
//
// Ownership/hilo:
// - QObject con afinidad al hilo grafico; lo crea y posee el composition
//   root. Es dueno de un unico QThread y de un objeto de contexto movido a ese
//   hilo; el hilo arranca en el constructor.
// - despachar() y cerrar() solo se llaman desde el hilo del dispatcher
//   (normalmente el grafico).
// - Las tareas se ejecutan en serie, en orden de despacho, en el hilo de
//   persistencia. Cada tarea contiene TODA la secuencia repositorio/UnitOfWork
//   y devuelve un valor (normalmente un Resultado tipado). Repositorios y
//   conexiones SQLite solo se tocan desde esas tareas.
// - El future se completa en el hilo de persistencia; el consumidor continua
//   con QFuture::then(contextoGrafico, ...).
//
// Cierre (teardown controlado): primero destruir consumidores (QML, view
// models, servicios), luego despachar la tarea que cierra la conexion del
// hilo, luego cerrar(). cerrar() espera a que terminen las tareas ya
// despachadas y une el hilo; despues se pueden destruir repositorios.
class PersistenceDispatcher final : public QObject {
    Q_OBJECT

public:
    explicit PersistenceDispatcher(QObject* parent = nullptr);
    // Llama cerrar() si no se llamo antes.
    ~PersistenceDispatcher() override;

    PersistenceDispatcher(const PersistenceDispatcher&) = delete;
    PersistenceDispatcher& operator=(const PersistenceDispatcher&) = delete;

    // Encola `tarea` en el hilo de persistencia. Si la tarea lanza, el future
    // propaga la excepcion. Tras cerrar(), devuelve un future cancelado sin
    // ejecutar la tarea.
    template <typename T>
    QFuture<T> despachar(std::function<T()> tarea);

    // Ejecuta las tareas ya encoladas, detiene el event loop del hilo y lo une
    // (bloquea al llamador hasta entonces). Idempotente.
    void cerrar();

    bool cerrado() const noexcept { return m_cerrado; }

    // Para pruebas y Q_ASSERT de infraestructura.
    const QThread* hilo() const noexcept { return &m_hilo; }

private:
    void encolar(std::function<void()> trabajo);

    QThread m_hilo;
    QObject* m_contexto = nullptr; // vive en m_hilo; se destruye al terminar el hilo
    bool m_cerrado = false;
};

template <typename T>
QFuture<T> PersistenceDispatcher::despachar(std::function<T()> tarea)
{
    if (m_cerrado) {
        return QFuture<T>(); // construido por defecto: cancelado y terminado
    }
    auto promesa = std::make_shared<QPromise<T>>();
    QFuture<T> future = promesa->future();
    encolar([promesa, tarea = std::move(tarea)]() {
        promesa->start();
        try {
            if constexpr (std::is_void_v<T>) {
                tarea();
            } else {
                promesa->addResult(tarea());
            }
        } catch (...) {
            promesa->setException(std::current_exception());
        }
        promesa->finish();
    });
    return future;
}

} // namespace satcfdi
