#pragma once

// T025.3: "Respaldar ahora" (D1, D6, D8, D17, D18). Orquesta captura (T025.2),
// contenedor (T025.1), destino (D8) y retencion (D17). Sin archivos propios:
// todo pasa por los puertos FuenteRespaldo, ContenedorRespaldo y
// DestinoRespaldo.
//
// Ownership e hilos:
// - QObject del hilo grafico; posee un QThread propio ("satcfdi-respaldo")
//   donde corre, uno a la vez, cada respaldo y cada limpieza de .partial.
// - respaldar(), cancelar(), limpiarPartialPropios() y cerrar() se llaman
//   desde el hilo grafico. respaldar() inicia la captura en ese hilo (contrato
//   de FuenteRespaldo) y el resto corre en el hilo propio; los futures se
//   completan alli.
// - `fuente` solo se usa dentro de respaldar(); el contenedor es compartido.
//
// D18: con un respaldo en curso, respaldar() devuelve de inmediato YaEnCurso y
// no inicia otro.
//
// Cancelacion: cancelar() se observa durante la captura (al terminar esta),
// durante el cifrado y la escritura (por chunk) y antes de publicar. Despues de
// publicar ya no se cancela. Ante error manejado o cancelacion se borra SOLO el
// .partial propio de ese respaldo.
//
// Cierre (cerrar(), tambien en el destructor): cancela, deja de esperar una
// captura que aun no termina (su staging lo limpia la propia fuente cuando
// termine, con su limpiador autonomo), espera el respaldo en curso y une el
// hilo. Despues de cerrar(), respaldar() devuelve Detenido.

#include "application/persistence/PuertosPersistencia.h"
#include "application/respaldo/AlimentadorRespaldo.h"
#include "ports/respaldo/ContenedorRespaldo.h"
#include "ports/respaldo/DestinoRespaldo.h"
#include "ports/respaldo/FuenteRespaldo.h"

#include <QFuture>
#include <QObject>
#include <QUuid>

#include <functional>
#include <memory>
#include <optional>

namespace satcfdi::respaldo {

struct ResultadoRespaldo {
    enum class Estado {
        Exito,
        ExitoConAdvertencias, // faltantes al congelar, paquetes omitidos o aviso de depuracion
        YaEnCurso,            // D18
        Cancelado,
        Detenido,             // servicio cerrado
        ErrorDestino,         // ver errorDestino (inexistente, sin escritura, volumen, espacio...)
        ErrorCaptura,         // ver errorCaptura
        ErrorFuente,          // una fuente cambio, desaparecio o no es valida (D11)
        ErrorContenedor,      // cifrado, KEK o limite del contenedor
        ErrorPublicacion,     // fsync o rename (ver errorDestino)
    };

    Estado estado = Estado::Exito;
    std::optional<ErrorDestino::Categoria> errorDestino;
    std::optional<ErrorCaptura::Categoria> errorCaptura;
    std::optional<ErrorRespaldo::Categoria> errorContenedor;
    QString diagnostico; // ASCII estable, sin rutas ni datos

    QString nombrePublicado; // solo con exito
    int faltantes = 0;       // paquetes que ya faltaban al congelar (D11)
    int omitidos = 0;
    int depurados = 0;          // copias propias borradas por la retencion (D17)
    bool avisoDepuracion = false; // la depuracion fallo o se omitio; el respaldo es valido
    int copiasSinRegistro = 0;  // D20: copias sin descargar y sin registro (no se tocan)
    int duplicadosProveedor = 0; // B2: ids repetidos avisados por primera vez
    bool avisoRegistro = false; // D20: el registro local no se pudo leer o escribir
    quint64 bytesDatos = 0;
    quint64 tamanoEstimado = 0;

    bool exito() const noexcept { return estado == Estado::Exito || estado == Estado::ExitoConAdvertencias; }
};

QString claveEstable(ResultadoRespaldo::Estado e);

struct PeticionRespaldo {
    std::shared_ptr<DestinoRespaldo> destino;
    KekRespaldo kekContrasena;  // la provee el llamador (T025.4 la guardara)
    KekRespaldo kekRecuperacion;
};

class RespaldoService : public QObject {
    Q_OBJECT

public:
    enum class Fase { Captura, Escritura, Publicacion, Depuracion };
    Q_ENUM(Fase)

    struct Opciones {
        QUuid idInstalacion; // D17. Si es nulo se pide a `proveedorIdInstalacion`
        // Resuelve (y crea si falta) el id local la PRIMERA vez que se necesita,
        // en el hilo grafico; asi construir el servicio no escribe nada.
        std::function<QUuid()> proveedorIdInstalacion;
        RelojUtc reloj = relojSistema();
        int copiasConservadas = 5; // D6
        // Sufijo del nombre publicado (8 hex aleatorios). Inyectable para
        // probar conflictos de nombre.
        std::function<QString()> generadorSufijo;
        // D20: registro local de lo publicado (nulo = sin registro: una copia
        // sin descargar nunca se borra).
        std::shared_ptr<RegistroPublicados> registro;
    };

    RespaldoService(FuenteRespaldo& fuente, std::shared_ptr<ContenedorRespaldo> contenedor, Opciones opciones,
                    QObject* parent = nullptr);
    ~RespaldoService() override; // cerrar()

    RespaldoService(const RespaldoService&) = delete;
    RespaldoService& operator=(const RespaldoService&) = delete;

    QFuture<ResultadoRespaldo> respaldar(PeticionRespaldo peticion);
    void cancelar();
    bool enCurso() const;

    // Borra en `destino` solo los `.partial` propios de esta instalacion
    // (regulares, sin seguir enlaces). Corre en el hilo del servicio, nunca a
    // la vez que un respaldo. Devuelve cuantos borro (-1 si no pudo listar).
    // T025.4 lo llama al arrancar con la carpeta configurada.
    QFuture<int> limpiarPartialPropios(std::shared_ptr<DestinoRespaldo> destino);

    void cerrar();

    // 8 hex de SHA-256 del id de instalacion: dueno de los `.partial`.
    static QString propietarioDe(const QUuid& idInstalacion);
    // Tamano estimado del contenedor (datos + estructura) y espacio a exigir.
    static quint64 estimarTamano(const CapturaRespaldo& captura);
    static quint64 espacioNecesario(quint64 tamanoEstimado);

signals:
    // Desde el hilo del servicio (conexiones encoladas al grafico).
    void progreso(satcfdi::respaldo::RespaldoService::Fase fase, quint64 bytes, quint64 total);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace satcfdi::respaldo
