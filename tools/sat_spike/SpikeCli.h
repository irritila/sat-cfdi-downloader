#pragma once

#include "ports/secrets/SecretStoreTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace satcfdi::sat {
class ClienteHttpSat;
}

namespace satcfdi::spike {

// CLI manual del spike SAT (T006, D1, D3-D6). Logica separada de main.cpp
// para poder probarla en proceso con costuras inyectables. Ver uso().
//
// Reglas (docs/tasks/T006-spike-sat.md, "Reglas de seguridad y operacion"):
// - .cer/.key solo desde rutas FUERA del repositorio; la contrasena solo por
//   `leerContrasena` (prompt sin eco en main.cpp), nunca por argv ni entorno.
// - validarEFirma() antes de todo; si falla: codigo kSalidaMaterial, sin red
//   (no se llama `crearCliente`) y sin escribir nada en disco.
// - --dry-run: construye y muestra los sobres ENMASCARADOS; sin red ni disco.
// - Cada operacion real muestra operacion, RFC enmascarado y rango/Id, y exige
//   que `leerLinea` devuelva exactamente "yes".
// - Ledger local (directorio de salida, fuera del repo): antes de cada
//   solicitud registra operacion, rango y hash del sobre; rechaza el mismo
//   criterio y mas de kMaxSolicitudes. Guarda RFC e Ids reales; nunca
//   secretos, tokens, sobres ni paquetes.
// - El ledger se usa bajo flock exclusivo (ledger.lock) durante toda la
//   operacion real: leer, validar, registrar, enviar y anotar el resultado.
//   Si otro proceso lo tiene, se falla de inmediato. Un ledger corrupto falla
//   cerrado. Directorios 0700 y archivos 0600 verificados con stat; si no se
//   confirman, no se escribe nada.
// - Guardias: <= kMaxVerificacionesPorSolicitud verificaciones por
//   IdSolicitud, separadas al menos kMinutosEntreVerificaciones; una sola
//   descarga por IdPaquete. El ZIP se nombra por hash (el IdPaquete real solo
//   queda en el ledger).
// - Consola: solo texto pasado por evidencia::enmascarar(); los argumentos no
//   reconocidos no se reproducen.
inline constexpr int kSalidaOk = 0;
inline constexpr int kSalidaUso = 2;        // argumentos o rutas invalidas
inline constexpr int kSalidaMaterial = 3;   // e.firma invalida (criterio 7)
inline constexpr int kSalidaCancelada = 4;  // el usuario no confirmo con "yes"
inline constexpr int kSalidaLedger = 5;     // tope o criterio repetido
inline constexpr int kSalidaSat = 6;        // fase, HTTP, Fault o CodEstatus de error
inline constexpr int kSalidaIncierta = 7;   // DespuesDeEnvio en una solicitud
inline constexpr int kSalidaPaqueteNoGuardado = 8; // descarga recibida pero el ZIP no se guardo

inline constexpr int kMaxSolicitudes = 3;
inline constexpr int kMaxVerificacionesPorSolicitud = 10;
inline constexpr int kMinutosEntreVerificaciones = 15;

// Escribe `datos` completos en `fd` (ya abierto 0600). Costura de pruebas para
// simular fallos de escritura; vacia = write()+fsync().
using EscritorArchivo = std::function<bool(int fd, const QByteArray& datos)>;

struct DependenciasSpike {
    // Contrasena de la e.firma (prompt sin eco). nullopt = no disponible.
    std::function<std::optional<BufferSecreto>()> leerContrasena;
    // Una linea de stdin (confirmaciones). nullopt = EOF.
    std::function<std::optional<QString>()> leerLinea;
    std::function<void(const QString&)> imprimir;
    std::function<void(const QString&)> imprimirError;
    // Unica forma de obtener red. Las pruebas cuentan sus llamadas.
    std::function<std::unique_ptr<sat::ClienteHttpSat>()> crearCliente;
    std::function<QDateTime()> relojUtc;
    // Directorio de salida por defecto (ledger y paquetes).
    QString directorioSalidaPorDefecto;
    // Raiz del repositorio: rutas dentro de ella se rechazan.
    QString raizRepositorio;
    // Opcional: escritura del ZIP (pruebas).
    EscritorArchivo escribirArchivo;
};

// Guarda el ZIP en <dirSalida>/paquetes/<archivo> (directorio 0700, archivo
// 0600 con O_EXCL|O_NOFOLLOW verificado con fstat). Ante cualquier fallo borra
// el archivo parcial y devuelve guardado=false con un error sin datos reales.
struct ResultadoGuardado {
    bool guardado = false;
    QString ruta;
    QString error;
};
ResultadoGuardado guardarPaquete(const QString& dirSalida, const QString& archivo, const QByteArray& datos,
                                 const EscritorArchivo& escritor = {});

// true si el ledger tiene una descarga de `idPaquete` cuyo resultado quedo
// con guardado=true. Solo esas bloquean una nueva descarga del paquete.
bool paqueteYaGuardado(const QList<QJsonObject>& entradas, const QString& idPaquete);

QString uso();

int ejecutarSpike(const QStringList& argumentos, const DependenciasSpike& deps);

// true si `ruta` (canonica o absoluta limpia) esta dentro de `raiz`.
bool rutaDentroDe(const QString& ruta, const QString& raiz);

} // namespace satcfdi::spike
