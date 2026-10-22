#pragma once

// Puertos de la restauracion transaccional de un respaldo AFB1 (T025.5; T025
// D3, D5, D13, D14, D15; ADR 0022). La aplicacion (RestauracionService) no
// hace E/S: todo pasa por estos puertos.
//
// - OrigenRestauracion: el `.afbak` elegido por el usuario (se puede reabrir
//   en cada intento de clave y leerse aunque el proveedor aun no lo haya
//   descargado: la lectura puede ser lenta o bloquear).
// - BaseRestauracion: valida, migra y limpia la copia de SQLite en el staging
//   (SQLite real en infraestructura).
// - AreaRestauracion: stagings privados en el mismo volumen que cada destino,
//   mtime, fsync, huella y marcador durable (diario de fases).
//
// Errores: categoria + diagnostico ASCII estable, sin rutas, RFC, contrasenas
// ni contenido.

#include "domain/common/Resultado.h"
#include "ports/persistence/Exito.h"
#include "ports/respaldo/ContenedorRespaldo.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QUuid>
#include <QtGlobal>

#include <memory>
#include <optional>
#include <utility>

QT_BEGIN_NAMESPACE
class QIODevice;
QT_END_NAMESPACE

namespace satcfdi::respaldo {

struct ErrorRestauracion {
    enum class Categoria {
        EnCurso,                     // ya hay una preparacion o solicitud en curso
        Detenido,                    // servicio cerrado
        NoPreparada,                 // solicitar() sin una preparacion vigente
        RestauracionPendiente,       // ya hay una restauracion solicitada (marcador)
        Origen,                      // no se pudo abrir o leer el archivo elegido
        OrigenCambio,                // el `.afbak` cambio entre la verificacion y la solicitud
        StagingCambio,               // el staging cambio despues de verificarse
        CredencialIncorrecta,        // contrasena o clave de recuperacion incorrecta (generico)
        ClaveRecuperacionMalFormada, // texto que no es una clave AFB1
        Contenedor,                  // ver `contenedor` (danado, formato, version, limite...)
        MetadatosInvalidos,          // metadatos del respaldo ausentes o sin formato 1
        EsquemaMasNuevo,             // esquema del respaldo > el que esta app conoce (D13)
        EsquemaInvalido,             // base sin schema_migrations o version incoherente
        Integridad,                  // integrity_check o foreign_key_check fallaron
        RutaInesperada,              // entrada fuera de base/, preferencias/ y paquetes/
        Base,                        // SQLite (abrir, migrar, borrar credenciales)
        Staging,                     // crear, colocar, fsync o borrar el staging
        Destino,                     // raiz de paquetes inexistente o punto de montaje
        EspacioInsuficiente,         // falta espacio libre en el volumen de la raiz o de los datos
        Marcador,                    // leer o escribir el marcador durable
        Cancelado,
        Interno,
    };

    Categoria categoria = Categoria::Interno;
    std::optional<ErrorRespaldo::Categoria> contenedor; // con Contenedor (y CredencialIncorrecta)
    QString diagnostico;                                 // ASCII estable

    static ErrorRestauracion de(Categoria c, QString diagnostico = {})
    {
        ErrorRestauracion e;
        e.categoria = c;
        e.diagnostico = std::move(diagnostico);
        return e;
    }

    bool operator==(const ErrorRestauracion&) const = default;
};

QString claveEstable(ErrorRestauracion::Categoria c);

// Lo que la UI (T025.6) muestra antes de confirmar. Fecha e id vienen de la
// cabecera AUTENTICADA (ADR 0021): el usuario confirma cual copia restaura.
struct ResumenRestauracion {
    QByteArray idRespaldo;         // kTamanoIdRespaldo bytes
    QDateTime creadoEnUtc;         // fecha autenticada del respaldo
    QUuid idInstalacionRespaldo;   // instalacion que lo creo
    QString versionAppRespaldo;    // metadatos
    QDateTime capturadaEnUtc;      // metadatos
    int esquemaRespaldo = 0;       // version de la base en el respaldo
    int esquemaFinal = 0;          // version tras migrar en el staging
    bool migrada = false;          // esquemaFinal > esquemaRespaldo
    int perfiles = 0;              // visibles (eliminado_en IS NULL)
    int solicitudes = 0;           // visibles
    int paquetes = 0;              // ZIP restaurados
    int faltantes = 0;             // ZIP que ya faltaban al respaldar (D11)
    int credencialesBorradas = 0;  // filas de credencial_sat retiradas (D3)
    int mtimesConservados = 0;     // ZIP con mtime restaurado
    quint64 bytesDatos = 0;
};

// --- Origen ------------------------------------------------------------------

class OrigenRestauracion {
public:
    virtual ~OrigenRestauracion() = default;
    // Dispositivo nuevo, abierto para lectura y posicionado al inicio.
    // Puede leer despacio o bloquear (archivo aun en la nube).
    virtual Resultado<std::unique_ptr<QIODevice>, ErrorRestauracion> abrir() = 0;
    // stat del archivo sin leerlo (no lo materializa): tamano, mtime, inodo.
    virtual Resultado<HuellaArchivo, ErrorRestauracion> huella() = 0;
};

// --- Base en el staging ---------------------------------------------------------

// Fecha a conservar como mtime de un ZIP restaurado.
struct MtimePaquete {
    QString rutaLocal;         // relativa a la raiz de paquetes
    QByteArray sha256;         // del indice (bytes); vacio si no indexado
    qint64 tamano = -1;        // del indice; -1 si no indexado
    std::optional<qint64> modificadoMs; // huella del indice (T015.2)
    std::optional<qint64> descargadoMs; // paquete_solicitud.descargado_en
};

struct InformeBaseRestauracion {
    int versionInicial = 0;
    int versionFinal = 0;
    int perfiles = 0;
    int solicitudes = 0;
    int paquetesDescargados = 0;
    int credencialesBorradas = 0;
    int avisosBorrados = 0;
    QList<MtimePaquete> paquetes; // Descargado con ruta_local valida
};

class BaseRestauracion {
public:
    virtual ~BaseRestauracion() = default;
    // Ultima version de esquema que esta app sabe migrar.
    virtual int versionMaximaConocida() const = 0;
    // Sobre la copia en el staging, con una conexion propia del hilo que llama
    // (nunca el grafico): rechaza un esquema mas nuevo SIN modificarla, exige
    // integrity_check, migra un esquema anterior, borra credencial_sat y sus
    // dependientes (aviso_vencimiento_efirma), vuelve a exigir integrity_check
    // y foreign_key_check y la deja en journal_mode=DELETE (un solo archivo).
    virtual Resultado<InformeBaseRestauracion, ErrorRestauracion> preparar(const QString& rutaBase) = 0;
};

// --- Area (staging y marcador) ---------------------------------------------------

struct StagingRestauracion {
    QString id;         // UUID canonico
    QString extraccion; // carpeta privada en el volumen de la raiz de paquetes
    QString datos;      // carpeta privada en la carpeta de datos (base y preferencias)

    QString rutaBase() const;         // <datos>/satcfdi.sqlite3
    bool operator==(const StagingRestauracion&) const = default;
};

// Lo que el marcador `preparado` guarda para la UI y la reverificacion.
struct DatosPreparado {
    QByteArray huellaStaging;   // AreaRestauracion::huella()
    HuellaArchivo huellaOrigen; // del `.afbak` al terminar de verificarlo
    QByteArray idRespaldo;
    QDateTime creadoEnUtc;
};

class AreaRestauracion {
public:
    virtual ~AreaRestauracion() = default;

    // Escribe el marcador `preparando` (con las rutas) y DESPUES crea los dos
    // stagings privados (0700, vacios): asi todo staging lo referencia un
    // marcador y el arranque puede borrarlo. Error RestauracionPendiente si ya
    // hay una restauracion solicitada. Un marcador `preparando`/`preparado`
    // previo se descarta.
    // `tamanoRespaldo` (bytes del `.afbak`) acota lo que se extraera: se exige
    // ese espacio mas un margen libre en el volumen de la raiz antes de crear
    // nada (EspacioInsuficiente).
    virtual Resultado<StagingRestauracion, ErrorRestauracion> crearStaging(quint64 tamanoRespaldo) = 0;
    // Mueve base y preferencias extraidas al staging de datos (rename en el
    // mismo volumen; copia con fsync si son volumenes distintos, exigiendo
    // antes espacio libre para la base en el volumen de los datos).
    virtual Resultado<Exito, ErrorRestauracion> colocarBase(const StagingRestauracion& s) = 0;
    // futimens del ZIP `paquetes/<rutaLocal>` en la extraccion.
    virtual Resultado<Exito, ErrorRestauracion> fijarMtime(const StagingRestauracion& s, const QString& rutaLocal,
                                                           qint64 ms) = 0;
    // fsync (F_FULLFSYNC) de cada archivo y carpeta de ambos stagings.
    virtual Resultado<Exito, ErrorRestauracion> sincronizar(const StagingRestauracion& s) = 0;
    // Huella de ambos stagings: metadatos de cada entrada (tamano, inodo,
    // mtime, ctime) y SHA-256 de la base. Cambia ante cualquier modificacion.
    virtual Resultado<QByteArray, ErrorRestauracion> huella(const StagingRestauracion& s) = 0;
    virtual Resultado<Exito, ErrorRestauracion> marcarPreparado(const StagingRestauracion& s,
                                                                const DatosPreparado& datos) = 0;
    // Recalcula la huella del staging (StagingCambio si no coincide) y escribe
    // el marcador `solicitado` con fsync: desde aqui el siguiente arranque
    // aplica la restauracion.
    virtual Resultado<Exito, ErrorRestauracion> marcarSolicitado(const StagingRestauracion& s) = 0;
    // Borra ambos stagings y despues el marcador. Idempotente.
    virtual void descartar(const StagingRestauracion& s) = 0;
};

} // namespace satcfdi::respaldo
