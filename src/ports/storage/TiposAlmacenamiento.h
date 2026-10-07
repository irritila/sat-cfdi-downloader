#pragma once

#include "domain/common/Resultado.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <optional>
#include <utility>

namespace satcfdi {

// Tipos del puerto PackageStorage (T008). Sin rutas absolutas ni bytes ZIP en
// errores o diagnosticos: solo rutas RELATIVAS a la raiz de paquetes.

// Donde vive un paquete (D1, D2, D5). Todos los campos se validan antes de
// tocar el filesystem; cualquier valor invalido produce EntradaInvalida.
struct UbicacionPaquete {
    QString rfcSolicitante;  // RFC canonico (rfc::normalizar + rfc::esValido)
    QString fechaInicialSat; // solicitud_masiva.fecha_inicial_sat, "yyyy-MM-ddTHH:mm:ss"; D1: su mes
    QString solicitudId;     // UUID LOCAL canonico de la solicitud (D2), no el IdSolicitud SAT
    QString idPaqueteSat;    // IdPaquete SAT tal como llego (se sanea al derivar el nombre)
};

// Errores del almacenamiento (Alcance de T008). T009 los traduce a
// FallaOperacion{Almacenamiento, CausaAlmacenamiento}: ColisionDestino ->
// ColisionDestino; SinEspacio -> EspacioInsuficiente; el resto ->
// EscrituraFallida (Cancelada se aplica como cancelacion, D12 de T007).
enum class ErrorAlmacenamiento {
    EntradaInvalida, // ubicacion, ruta relativa u hallazgo invalido; no se toco el filesystem
    Permiso,         // EACCES/EPERM/EROFS al crear directorios, temporal o al escribir
    SinEspacio,      // ENOSPC/EDQUOT
    Escritura,       // otra falla de E/S antes de promover
    Durabilidad,     // fsync/F_FULLFSYNC del temporal fallo: NO se promovio (D7)
    Promocion,       // renamex_np fallo por otra causa distinta de destino existente
    ColisionDestino, // ya existe un archivo final: nunca se sobrescribe (D6)
    LecturaRaiz,     // raiz invalida o no legible (existencia, escaneo)
    Cancelada,       // cancelacion cooperativa observada antes de promover
};

QString claveEstable(ErrorAlmacenamiento error);

// Error de guardarAtomico: distingue la fuente (red/SAT, la clasifica T009)
// del almacenamiento local.
struct ErrorGuardarZip {
    enum class Origen { Fuente, Almacenamiento };
    Origen origen = Origen::Almacenamiento;
    // Solo con origen Almacenamiento.
    ErrorAlmacenamiento almacenamiento = ErrorAlmacenamiento::Escritura;
    // Breve y saneado: operacion, texto de errno y ruta RELATIVA. Nunca bytes.
    QString diagnostico;

    static ErrorGuardarZip deAlmacenamiento(ErrorAlmacenamiento e, QString diagnostico = {})
    {
        return ErrorGuardarZip{Origen::Almacenamiento, e, std::move(diagnostico)};
    }
    static ErrorGuardarZip deFuente(QString diagnostico)
    {
        return ErrorGuardarZip{Origen::Fuente, ErrorAlmacenamiento::Escritura, std::move(diagnostico)};
    }
};

// Falla de la fuente de bytes (p. ej. respuesta SAT truncada). Diagnostico
// ya saneado por quien implementa la fuente.
struct ErrorFuenteZip {
    QString diagnostico;
};

// Fuente de bytes del ZIP por chunks (streaming, sin el ZIP completo en
// memoria). La implementa T009 sobre la respuesta SAT. Se consume en el hilo
// del OperacionExecutor.
class FuenteZipPorChunks {
public:
    virtual ~FuenteZipPorChunks() = default;
    // Siguiente chunk; std::nullopt = fin de datos. Un chunk vacio es valido.
    virtual Resultado<std::optional<QByteArray>, ErrorFuenteZip> siguiente() = 0;
};

// Cancelacion cooperativa vista desde ports (no depende de application).
// Adaptacion desde SenalCancelacion (T007), que es copiable y comparte estado:
//   Cancelacion c([senal] { return senal.solicitada(); });
// Construida por defecto, nunca se cancela. La consulta debe ser barata y
// segura entre hilos.
class Cancelacion {
public:
    Cancelacion() = default;
    explicit Cancelacion(std::function<bool()> consultar) : m_consultar(std::move(consultar)) {}
    bool solicitada() const { return m_consultar && m_consultar(); }

private:
    std::function<bool()> m_consultar;
};

// Archivo final promovido.
struct ArchivoFinal {
    QString rutaRelativa;               // <RFC>/<yyyy-mm>/<UUID>/<archivo>.zip -> ruta_local
    bool advertenciaDurabilidad = false; // promovido, pero fallo el fsync del directorio (D7)
};

// Resolucion de una ruta para revelarla en Finder (T009.1 D2/D3). Solo el
// adaptador conoce la ruta absoluta; QML nunca la recibe.
struct RutaRevelable {
    enum class Estado { Disponible, NoEncontrada };
    Estado estado = Estado::NoEncontrada;
    QString rutaAbsoluta; // vacia si NoEncontrada

    static RutaRevelable disponible(QString ruta) { return RutaRevelable{Estado::Disponible, std::move(ruta)}; }
    static RutaRevelable noEncontrada() { return RutaRevelable{Estado::NoEncontrada, {}}; }
    friend bool operator==(const RutaRevelable&, const RutaRevelable&) = default;
};

// Hallazgo del escaneo de recuperacion (D9). Solo estructura propia.
enum class TipoHallazgo {
    Temporal, // .<archivo>.<16 hex>.part
    Final,    // <archivo>.zip
};

struct HallazgoFilesystem {
    TipoHallazgo tipo = TipoHallazgo::Temporal;
    QString rutaRelativa;  // relativa a la raiz
    SolicitudId solicitudId; // UUID local de la carpeta
    QString archivoFinal;  // nombre del final asociado (el propio si tipo == Final)

    friend bool operator==(const HallazgoFilesystem&, const HallazgoFilesystem&) = default;
};

inline QString claveEstable(ErrorAlmacenamiento error)
{
    switch (error) {
    case ErrorAlmacenamiento::EntradaInvalida: return QStringLiteral("EntradaInvalida");
    case ErrorAlmacenamiento::Permiso: return QStringLiteral("Permiso");
    case ErrorAlmacenamiento::SinEspacio: return QStringLiteral("SinEspacio");
    case ErrorAlmacenamiento::Escritura: return QStringLiteral("Escritura");
    case ErrorAlmacenamiento::Durabilidad: return QStringLiteral("Durabilidad");
    case ErrorAlmacenamiento::Promocion: return QStringLiteral("Promocion");
    case ErrorAlmacenamiento::ColisionDestino: return QStringLiteral("ColisionDestino");
    case ErrorAlmacenamiento::LecturaRaiz: return QStringLiteral("LecturaRaiz");
    case ErrorAlmacenamiento::Cancelada: return QStringLiteral("Cancelada");
    }
    return {};
}

} // namespace satcfdi
