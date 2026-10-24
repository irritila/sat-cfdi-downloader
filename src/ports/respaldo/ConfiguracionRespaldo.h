#pragma once

// Configuracion local del respaldo (T025.4; T025 D4, D12, D14, D16). Vive en
// preferencias.ini, seccion `respaldo`, que la captura (T025.2) nunca copia:
// solo exporta la lista blanca exacta `facturas/ultimoPerfil`. Es propia de
// este equipo.
//
// Lo que NUNCA guarda: la contrasena, la clave de recuperacion (ni formateada
// ni en bytes) ni la KEK. Las KEK viven en el almacen seguro (Keychain) y aqui
// solo queda su referencia opaca.

#include "domain/common/Resultado.h"
#include "ports/persistence/Exito.h"
#include "ports/respaldo/AlmacenKekRespaldo.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <optional>
#include <utility>

namespace satcfdi::respaldo {

// T025.6 (C6): resumen saneado del ultimo respaldo terminado (manual o
// diario), para mostrarlo al abrir la app. Solo claves estables, conteos y
// banderas: nunca diagnostico libre, rutas, nombre publicado ni ids.
struct ResumenUltimoRespaldo {
    QDateTime fechaUtc;      // fin del respaldo (UTC, ms)
    QString estado;          // claveEstable(ResultadoRespaldo::Estado)
    quint64 bytesDatos = 0;
    int faltantes = 0;           // ZIP que ya faltaban al congelar (D11)
    int omitidos = 0;            // ruta_local invalida (no se copiaron)
    int depurados = 0;           // copias propias borradas por la retencion (D17)
    int copiasSinRegistro = 0;   // D20: sin descargar y sin registro (no se tocan)
    int duplicadosProveedor = 0; // ids repetidos avisados por primera vez
    bool avisoDepuracion = false;
    bool avisoRegistro = false;
    // Categoria del error, como clave estable; vacia si no aplica.
    QString errorDestino;    // claveEstable(ErrorDestino::Categoria)
    QString errorCaptura;    // claveEstable(ErrorCaptura::Categoria)
    QString errorContenedor; // claveEstable(ErrorRespaldo::Categoria)
    // T025.6 (Q1): SOLO en memoria (nunca se escribe ni se lee del ini). true
    // si este resumen no se pudo guardar: al reabrir la app no se vera.
    bool avisoPersistencia = false;

    bool operator==(const ResumenUltimoRespaldo&) const = default;
};

struct ConfiguracionRespaldo {
    // Carpeta (CarpetaRespaldoSistema::crearReferencia).
    QString rutaCanonica;
    QByteArray bookmark;
    QString uuidVolumen; // vacio si el volumen no lo reporta (registro D20 con dev:ino)

    bool diarioActivado = false;

    // KEK en el almacen seguro (D4, D16).
    ReferenciaKek kekContrasena;
    ReferenciaKek kekRecuperacion;

    // Ultimo respaldo (manual o diario).
    std::optional<QDateTime> ultimoIntentoUtc;
    std::optional<QDateTime> ultimoExitoUtc;
    QString ultimoResultado; // claveEstable(ResultadoRespaldo::Estado) o diagnostico del gestor
    // T025.6 (C6): ausente si nunca hubo respaldo o si lo guardado no es
    // valido (una lectura corrupta lo deja ausente; no afecta `integra`).
    std::optional<ResumenUltimoRespaldo> ultimoResumen;

    bool carpetaConfigurada() const noexcept { return !bookmark.isEmpty() && !rutaCanonica.isEmpty(); }
    bool contrasenaConfigurada() const noexcept { return !kekContrasena.nula() && !kekRecuperacion.nula(); }

    bool operator==(const ConfiguracionRespaldo&) const = default;
};

struct ErrorConfiguracionRespaldo {
    QString diagnostico; // ASCII estable

    static ErrorConfiguracionRespaldo de(QString d)
    {
        ErrorConfiguracionRespaldo e;
        e.diagnostico = std::move(d);
        return e;
    }
};

// Resultado de leer la configuracion. `integra` (seguridad 1 de T025.4): el
// archivo existe, se leyo sin error y trae las DOS referencias de KEK
// presentes y validas. Una referencia presente pero mal escrita NO se
// convierte en "ausente": deja la lectura no integra (y esa referencia nula en
// `configuracion`). Sin lectura integra nunca se borra ninguna KEK.
struct ConfiguracionLeida {
    ConfiguracionRespaldo configuracion;
    bool integra = false;
};

// Persistencia de la configuracion. Bloquea (archivo): fuera del hilo grafico.
class AlmacenConfiguracionRespaldo {
public:
    virtual ~AlmacenConfiguracionRespaldo() = default;
    virtual Resultado<ConfiguracionLeida, ErrorConfiguracionRespaldo> cargar() = 0;
    virtual Resultado<Exito, ErrorConfiguracionRespaldo> guardar(const ConfiguracionRespaldo& configuracion) = 0;
};

} // namespace satcfdi::respaldo
