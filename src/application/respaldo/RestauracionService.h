#pragma once

// T025.5: restauracion transaccional de un respaldo AFB1 (D3, D5, D13, D14,
// D15, D16; ADR 0022). Sin E/S propia: todo por ContenedorRespaldo,
// OrigenRestauracion, BaseRestauracion y AreaRestauracion.
//
// Flujo con la app en marcha:
//   1. preparar(peticion): en un hilo de trabajo propio (nunca el grafico)
//      crea los stagings (marcador `preparando`), elige la KEK (contrasena o
//      clave de recuperacion, reabriendo el archivo en cada intento, D16),
//      extrae y autentica TODO el contenedor, valida metadatos y rutas,
//      rechaza un esquema mas nuevo, migra uno anterior, borra credenciales
//      (D3), conserva el mtime de los ZIP, hace fsync, calcula la huella y
//      escribe el marcador `preparado`. Devuelve el resumen para la UI.
//      Nada de los datos actuales se toca.
//   2. solicitar(): comprueba que ni el `.afbak` ni el staging cambiaron (si
//      cambiaron: error y hay que preparar otra vez: se vuelve a verificar),
//      detiene la app (`detenerApp`: worker, ejecutor con cancelacion,
//      respaldo), escribe el marcador `solicitado`, pide el relanzamiento y
//      sale. La sustitucion la hace el siguiente arranque (RestauracionArranque).
//
// Lectura lenta o bloqueante (archivo aun en la nube): cancelar() resuelve de
// inmediato el future de preparar() con Cancelado, sin esperar al hilo de
// trabajo. Ese hilo, al volver de la lectura, ve la cancelacion y descarta su
// staging. Mientras no termine, enCurso() sigue en true y un preparar() nuevo
// devuelve EnCurso. El hilo de trabajo no usa `this`: solo estado compartido.
//
// Hilos: QObject del hilo grafico; todos sus metodos publicos se llaman desde
// ahi, igual que los callbacks de Dependencias. Los futures se completan en
// hilos de trabajo o en el grafico; continuar con then(contexto, ...).
// Secretos: el secreto de la peticion y las KEK solo viven en el hilo de
// trabajo durante preparar() y se limpian al terminar; nunca se registran.

#include "ports/respaldo/ContenedorRespaldo.h"
#include "ports/respaldo/Restauracion.h"

#include <QFuture>
#include <QObject>

#include <functional>
#include <memory>
#include <optional>

namespace satcfdi::respaldo {

struct PeticionRestauracion {
    enum class Credencial {
        Automatica,        // contrasena; si falla y el texto es una clave de recuperacion valida, la clave
        Contrasena,
        ClaveRecuperacion, // texto tecleado (Crockford Base32 con guiones o espacios)
    };

    std::shared_ptr<OrigenRestauracion> origen;
    Credencial credencial = Credencial::Automatica;
    std::shared_ptr<BufferSecreto> secreto;
};

struct ResultadoSolicitudRestauracion {
    enum class Estado {
        Relanzando,      // marcador durable escrito y relanzamiento programado; la app sale
        SinRelanzamiento, // marcador escrito pero el SO no programo el relanzamiento; la app sale
                          // y se aplica al abrirla de nuevo
        Rechazada,        // nada se detuvo (ver error)
        FalloTrasDetener, // la app ya se detuvo y el marcador no se escribio: sale y relanza sin restaurar
    };

    Estado estado = Estado::Rechazada;
    std::optional<ErrorRestauracion> error;
};

class RestauracionService : public QObject {
    Q_OBJECT

public:
    enum class Fase { Autenticando, Extrayendo, Validando, Sincronizando };
    Q_ENUM(Fase)

    struct Dependencias {
        std::shared_ptr<ContenedorRespaldo> contenedor;
        std::shared_ptr<AreaRestauracion> area;
        std::shared_ptr<BaseRestauracion> base;
        // Hilo grafico. Detiene worker, ejecutor (rechaza operaciones nuevas,
        // pide la cancelacion de la actual, espera su cierre) y servicios de
        // respaldo; el future se completa cuando ya no corre nada que escriba.
        std::function<QFuture<void>()> detenerApp;
        // Hilo grafico. true si el relanzamiento quedo programado
        // (OSIntegration::solicitarRelanzamiento() == Programado).
        std::function<bool()> relanzar;
        // Hilo grafico. Salida ordenada (AppLifecycleController::solicitarSalida).
        std::function<void()> salir;
    };

    explicit RestauracionService(Dependencias dependencias, QObject* parent = nullptr);
    ~RestauracionService() override; // cerrar()

    RestauracionService(const RestauracionService&) = delete;
    RestauracionService& operator=(const RestauracionService&) = delete;

    QFuture<Resultado<ResumenRestauracion, ErrorRestauracion>> preparar(PeticionRestauracion peticion);
    // Cancela la preparacion en curso (resuelve su future con Cancelado de
    // inmediato). Sin efecto si no hay una.
    void cancelar();
    // Hay un hilo de trabajo de preparar() o descartar() vivo (incluido uno
    // abandonado tras cancelar()). Una solicitud aceptada deja el servicio
    // tomado hasta salir: preparar() y solicitar() devuelven EnCurso.
    bool enCurso() const;
    // Hay una preparacion verificada lista para solicitar().
    bool preparada() const;

    // Descarta la preparacion lista (borra su staging y el marcador).
    QFuture<void> descartar();

    QFuture<ResultadoSolicitudRestauracion> solicitar();

    // Cancela, descarta la preparacion no solicitada y deja de aceptar
    // peticiones. No espera a un hilo bloqueado en la lectura.
    void cerrar();

    // R2: hilos de trabajo desacoplados vivos en el proceso (preparar,
    // descartar, comprobaciones de solicitar). Si al salir queda alguno (p. ej.
    // bloqueado leyendo un archivo de File Provider), main termina con
    // _exit() despues de cerrar la base, sin destructores estaticos.
    static int hilosDesacopladosVivos();

signals:
    // Desde hilos de trabajo (conexiones encoladas al grafico).
    void progreso(satcfdi::respaldo::RestauracionService::Fase fase, quint64 bytes, quint64 total);

public:
    struct Estado;

private:
    std::shared_ptr<Estado> m_estado;
};

QString claveEstable(ResultadoSolicitudRestauracion::Estado e);

} // namespace satcfdi::respaldo
