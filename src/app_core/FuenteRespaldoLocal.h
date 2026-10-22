#pragma once

#include "application/persistence/PuertosPersistencia.h"
#include "ports/respaldo/FuenteRespaldo.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace satcfdi {

class OperacionExecutor;

// T025.2: implementacion de FuenteRespaldo (D7, D10, D11, D14).
//
// Staging: <directorioDatos>/respaldo-tmp/ (0700, directorio real) y dentro
// una carpeta por captura `captura-<uuid>` (0700) con la copia de la base y el
// ini filtrado (0600).
//
// capturar():
//   1. hilo que llama (grafico): registra la captura como activa y crea su
//      staging (solo mkdir/chmod);
//   2. hilo del ejecutor: OperacionExecutor::copiarBaseParaRespaldo (VACUUM
//      INTO) en orden de cola, detras de la operacion en curso;
//   3. hilo del pool: LectorCopiaBaseDatos::leer (integrity_check, esquema y
//      lista congelada de paquetes de ESA copia), ini filtrado y entradas.
// Ante error borra el staging y la quita de las activas antes de completar.
//
// Preferencias (D14): lista blanca de CLAVES exactas, cada una con su
// validacion de valor (kClavesPreferencias). Toda clave desconocida, sin
// valor valido o de la seccion `respaldo` se descarta. Sin claves aceptadas
// no hay entrada.
//
// Vida (C1/C2 de la revision): el lector y el estado (opciones, registro de
// capturas activas) son de propiedad compartida con las continuaciones y con
// la tarea de limpieza inicial, asi que pueden terminar despues de destruirse
// esta instancia o el composition root. El ejecutor solo se usa dentro de
// capturar(), de forma sincrona.
//
// Concurrencia (C3): un mutex protege el registro de capturas activas. La
// creacion del staging de una captura y la limpieza de huerfanos lo toman, y la
// limpieza nunca borra una captura activa, aunque corra a la vez que capturar().
class FuenteRespaldoLocal final : public respaldo::FuenteRespaldo {
public:
    struct Opciones {
        QString directorioDatos;  // AppDataLocation o --data-dir (absoluto)
        QString raizPaquetes;     // raiz efectiva de paquetes (absoluta)
        QString rutaPreferencias; // <directorioDatos>/preferencias.ini
        QString versionApp;
        RelojUtc reloj = relojSistema();
    };

    // Clave exacta del ini -> validacion de su valor. Hoy solo el ultimo perfil
    // de Facturas (UUID canonico, T023.4). Una preferencia nueva solo entra al
    // respaldo si se agrega aqui con su validacion.
    struct ClavePreferencia {
        const char* clave;
        bool (*valida)(const QString& valor);
    };
    static const QList<ClavePreferencia>& clavesPreferencias();

    static constexpr const char* kCarpetaStaging = "respaldo-tmp";
    static constexpr const char* kPrefijoCaptura = "captura-";

    FuenteRespaldoLocal(OperacionExecutor& ejecutor, std::shared_ptr<respaldo::LectorCopiaBaseDatos> lector,
                        Opciones opciones);
    ~FuenteRespaldoLocal() override;

    QFuture<Resultado<respaldo::CapturaRespaldo, respaldo::ErrorCaptura>> capturar() override;
    Resultado<Exito, respaldo::ErrorCaptura> limpiar(const respaldo::CapturaRespaldo& captura) override;
    int limpiarHuerfanos() override;

    // Tarea autocontenida (sin `this`) para encolar la limpieza inicial en otro
    // hilo; segura aunque esta instancia se destruya antes de que corra.
    std::function<int()> tareaLimpiezaHuerfanos() const;

    QString raizStaging() const;
    int capturasActivas() const;

    // Pura (pruebas): escribe en `destino` las claves aceptadas de `origen`.
    // Devuelve cuantas escribio, o -1 si fallo la lectura o la escritura.
    static int filtrarPreferencias(const QString& origen, const QString& destino);

    struct Estado;

private:
    OperacionExecutor& m_ejecutor;
    std::shared_ptr<Estado> m_estado;
};

} // namespace satcfdi
