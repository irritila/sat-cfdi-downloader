#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <optional>

namespace satcfdi {

// T025.5 (D5, D13, ADR 0022): marcador durable de la restauracion y diario de
// fases. Vive en `<directorioDatos>/restauracion/marcador.json` (carpeta
// 0700, archivo 0600) y se escribe de forma ATOMICA: `marcador.json.tmp`
// (O_CREAT|O_TRUNC|O_NOFOLLOW) -> write -> fsync + F_FULLFSYNC -> rename sobre
// `marcador.json` -> fsync de la carpeta. Un lector ve la version anterior o
// la nueva completa, nunca una mezcla.
//
// Fases (en orden; cada una se escribe ANTES de empezar el paso siguiente):
//   preparando        el proceso en marcha crea stagings y verifica.
//   preparado         staging verificado; falta la confirmacion del usuario.
//   solicitado        confirmado: el siguiente arranque aplica.
//   pre_elegida       arranque: nombres `pre-restauracion-<fecha>[-N]` elegidos.
//   moviendo_actuales base, -wal, -shm, credentials/ y la raiz de paquetes
//                     actuales van a pre-restauracion (renames sin reemplazo).
//   actuales_movidos  todo lo actual esta en pre-restauracion.
//   promoviendo       staging reverificado; renames de promocion en curso.
//   promovido         staging promovido y preferencias aplicadas; falta limpiar.
//   revirtiendo       un paso fallo: se deshace hasta dejar los datos actuales.
// Al terminar (aplicada, revertida o descartada) se borra el marcador.
struct MarcadorRestauracion {
    enum class Fase {
        Preparando,
        Preparado,
        Solicitado,
        PreElegida,
        MoviendoActuales,
        ActualesMovidos,
        Promoviendo,
        Promovido,
        Revirtiendo,
    };

    static constexpr int kFormato = 1;

    QString id; // UUID canonico de la restauracion
    Fase fase = Fase::Preparando;
    QString directorioDatos; // absoluto, limpio
    QString raizPaquetes;    // absoluta, canonica (sin enlaces)
    QString stagingDatos;    // <datos>/restauracion/staging-<id>
    QString stagingExtraccion; // <padre de la raiz>/.restauracion-<id>.noindex
    QByteArray huellaStaging;  // AreaRestauracion::huella (bytes)
    // Huella del `.afbak` (tamano, mtime ns, inodo, dispositivo) al verificar.
    qint64 origenTamano = -1;
    qint64 origenMtimeNs = 0;
    qint64 origenInodo = 0;
    qint64 origenDispositivo = 0;
    QByteArray idRespaldo;
    QDateTime creadoRespaldoUtc;
    // Arranque.
    QString fechaPre;    // yyyyMMdd'T'HHmmss'Z'
    QString preDatos;    // <datos>/pre-restauracion-<fecha>[-N]
    QString prePaquetes; // <padre de la raiz>/pre-restauracion-<fecha>[-N]
    Fase faseAntesDeRevertir = Fase::Solicitado;

    QByteArray serializar() const;
    static std::optional<MarcadorRestauracion> desdeBytes(const QByteArray& json);

    static QString claveFase(Fase f);
    static std::optional<Fase> faseDesde(const QString& clave);
};

// E/S del marcador. Hilo: el del llamador (nunca el grafico).
namespace marcador_restauracion {

inline constexpr const char* kCarpeta = "restauracion";
inline constexpr const char* kArchivo = "marcador.json";
inline constexpr const char* kPrefijoStagingDatos = "staging-";
inline constexpr const char* kPrefijoStagingExtraccion = ".restauracion-";
// R8: Spotlight no indexa una carpeta con este sufijo.
inline constexpr const char* kSufijoStagingExtraccion = ".noindex";
inline constexpr const char* kPrefijoPre = "pre-restauracion-";

QString rutaCarpeta(const QString& directorioDatos);
QString rutaArchivo(const QString& directorioDatos);

// Crea (si falta) `<datos>/restauracion` con 0700; rechaza un enlace o un
// archivo con ese nombre.
bool asegurarCarpeta(const QString& directorioDatos);

enum class Lectura { Ausente, Leido, Invalido };
Lectura leer(const QString& directorioDatos, MarcadorRestauracion& marcador);

// Escritura atomica y durable (ver arriba). false si algun paso fallo; en ese
// caso el marcador anterior queda intacto.
bool escribir(const QString& directorioDatos, const MarcadorRestauracion& marcador);

// unlink + fsync de la carpeta. true si ya no existe.
bool borrar(const QString& directorioDatos);

} // namespace marcador_restauracion

} // namespace satcfdi
