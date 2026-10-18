#pragma once

#include <QString>

#include <functional>
#include <optional>

namespace satcfdi {

class FileOps;

// T019 D8: como quedo la raiz de paquetes al arrancar.
enum class EstadoRaizPaquetes {
    NoAplica,              // --data-dir o raiz inyectada: no se consulto el home
    Nueva,                 // sin carpeta previa: se creo ~/Archivo-Fiscal/paquetes
    YaNueva,               // ya existia la raiz nueva y no habia paquetes previos
    MigradaPadre,          // ~/SAT-CFDI-Downloader completo -> ~/Archivo-Fiscal
    MigradaPaquetes,       // solo paquetes -> ~/Archivo-Fiscal/paquetes
    UsaLegadaPorConflicto, // origen y destino con contenido: no se toco nada
    UsaLegadaPorError,     // fallo de E/S: no se movio nada
};

// Clave estable para logs y pruebas ("nueva", "migrada_padre", ...).
QString claveEstable(EstadoRaizPaquetes estado);

// Aviso para la UI (T019 D8). Rutas SIEMPRE relativas al home (`~/...`),
// nunca absolutas. `mensaje` es un texto sugerido, saneado (sin RFC, rutas
// absolutas ni nombres de ZIP); la presentacion puede componer el suyo con
// `motivo` y las rutas.
struct AvisoRaizPaquetes {
    enum class Motivo {
        Conflicto,        // UsaLegadaPorConflicto
        Error,            // UsaLegadaPorError; o bien origen previo que no es
                          // directorio (Nueva/YaNueva, rutaEfectiva = nueva)
        RaizNoDisponible, // Nueva/YaNueva pero la raiz nueva no se pudo crear o no es directorio
    };

    Motivo motivo = Motivo::Error;
    QString rutaLegada;   // "~/SAT-CFDI-Downloader/paquetes"
    QString rutaNueva;    // "~/Archivo-Fiscal/paquetes"
    QString rutaEfectiva; // la que usa la app en esta sesion (una de las dos)
    QString mensaje;

    bool operator==(const AvisoRaizPaquetes&) const = default;
};

// T019 D8: resultado de la resolucion de la raiz de paquetes. Viaja en
// ArranquePreparado. `raizEfectiva` es absoluta y alimenta TODO lo que lee o
// escribe paquetes (almacenamiento, recuperacion, existencia, indice, PDF y
// "Abrir carpeta de paquetes"). `aviso` solo existe ante conflicto o fallo.
struct ResolucionRaizPaquetes {
    QString raizEfectiva;
    EstadoRaizPaquetes estado = EstadoRaizPaquetes::NoAplica;
    std::optional<AvisoRaizPaquetes> aviso;
};

// T019 D4: politica de migracion de ~/SAT-CFDI-Downloader/paquetes a
// ~/Archivo-Fiscal/paquetes. Sin marcador: el estado se deduce del
// filesystem con lstat (FileOps::tipo, sin seguir el ultimo componente).
// Solo renombra con FileOps::renombrarSinReemplazo (RENAME_EXCL), crea
// directorios nuevos con 0700 y hace rmdir de directorios vacios; nunca
// copia, abre, escribe ni borra archivos, ni cambia permisos de lo existente.
//
// Casos (en orden; toda lectura antes de cualquier cambio):
// - Origen ausente (sin ~/SAT-CFDI-Downloader o sin su `paquetes`): crea lo
//   que falte de la raiz nueva (Nueva) o la encuentra (YaNueva). Si no se
//   puede crear, sigue con la raiz nueva y avisa (RaizNoDisponible).
// - Origen (padre o `paquetes`) que no es directorio ni enlace: ahi no puede
//   haber ZIP; se crea/usa la raiz nueva (Nueva/YaNueva) sin tocar el archivo
//   viejo y se avisa con Motivo::Error (o RaizNoDisponible si no se pudo
//   crear). Error de lstat del origen: UsaLegadaPorError.
// - Destino ~/Archivo-Fiscal archivo/enlace, con contenido distinto de un
//   `paquetes` vacio, o `paquetes` del destino con contenido/archivo/enlace:
//   UsaLegadaPorConflicto.
// - Destino vacio de un intento previo: rmdir y se sigue.
// - Padre viejo ENLACE con `paquetes` (lstat a traves del enlace): se
//   renombra el enlace completo (MigradaPadre) sin listar ni modificar su
//   objetivo. Si su objetivo no contiene `paquetes` no hay origen: el enlace
//   y su objetivo no se tocan y solo se prepara la raiz nueva (Nueva o
//   YaNueva), igual que con un padre normal sin `paquetes`.
// - Padre viejo con solo `paquetes` (y `.DS_Store`), comprobado otra vez
//   justo antes del rename: renombra el padre (MigradaPadre). Si tiene mas
//   cosas: crea ~/Archivo-Fiscal (0700) y renombra solo `paquetes`
//   (MigradaPaquetes); si el rename falla, rmdir del padre recien creado.
// - Un directorio nuevo cuyo mkdir falla tras crearse (p. ej. chmod) se
//   retira con rmdir.
// - Error de listado, mkdir, rmdir o rename (EXDEV incluido):
//   UsaLegadaPorError.
// Un enlace (padre o `paquetes`) se renombra a si mismo; su objetivo no se toca.
//
// Diagnostico en la categoria "satcfdi.arranque": estado, paso y errno; sin
// rutas absolutas, RFC ni nombres de ZIP. No escribe LogSolicitud.
//
// Hilo: el del llamador (hilo temporal de AppBootstrapper). `ops` no es
// propiedad y debe vivir durante resolver().
class MigracionRaizPaquetes {
public:
    static constexpr unsigned kModoDirectorioNuevo = 0700;

    MigracionRaizPaquetes(QString home, FileOps& ops);

    ResolucionRaizPaquetes resolver() const;

    // Rutas mostradas al usuario (relativas al home).
    static QString rutaLegadaVisible();
    static QString rutaNuevaVisible();

private:
    QString m_home;
    FileOps& m_ops;
};

// Migrador inyectable en AppBootstrapper::Opciones. Se invoca una vez en el
// hilo de E/S del bootstrap.
using MigradorRaizPaquetes = std::function<ResolucionRaizPaquetes()>;

// Migrador de produccion: MigracionRaizPaquetes(home, PosixFileOps).
MigradorRaizPaquetes migradorRaizPaquetesPosix(const QString& home);

} // namespace satcfdi
