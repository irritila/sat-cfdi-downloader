#include "MigracionRaizPaquetes.h"

#include "infrastructure/storage/FileOps.h"
#include "infrastructure/storage/FilesystemPackageStorage.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>

#include <cerrno>
#include <memory>

namespace satcfdi {

namespace {

Q_LOGGING_CATEGORY(lcArranque, "satcfdi.arranque")

using Tipo = FileOps::TipoEntrada;

const QByteArray kPaquetes = QByteArrayLiteral("paquetes");
const QByteArray kDsStore = QByteArrayLiteral(".DS_Store");

QString visible(const char* carpeta)
{
    return QStringLiteral("~/") + QLatin1String(carpeta) + QLatin1Char('/')
           + QLatin1String(FilesystemPackageStorage::kCarpetaPaquetes);
}

bool esDirectorioOEnlace(Tipo t)
{
    return t == Tipo::Directorio || t == Tipo::Enlace;
}

// Estado del destino tras solo LEER (sin cambios).
enum class Destino { Libre, VacioPrevio, VacioPrevioConPaquetes, Conflicto, Error };

// Contexto de una resolucion: rutas nativas y paso fallido para el log.
struct Contexto {
    FileOps& ops;
    QByteArray padreLegado;
    QByteArray raizLegada;
    QByteArray padreNuevo;
    QByteArray raizNueva;
    QString absLegada;
    QString absNueva;
    // Diagnostico (paso estable y errno), sin rutas.
    const char* paso = "";
    int error = 0;
    // Origen previo que no es directorio (archivo con el nombre del padre o de
    // `paquetes`): no puede tener ZIP; se usa la raiz nueva y se avisa.
    const char* origenInvalido = nullptr;

    bool fallo(const char* p, int e)
    {
        paso = p;
        error = e;
        return false;
    }
};

AvisoRaizPaquetes aviso(AvisoRaizPaquetes::Motivo motivo, bool efectivaLegada, bool origenInvalido = false)
{
    AvisoRaizPaquetes a;
    a.motivo = motivo;
    a.rutaLegada = MigracionRaizPaquetes::rutaLegadaVisible();
    a.rutaNueva = MigracionRaizPaquetes::rutaNuevaVisible();
    a.rutaEfectiva = efectivaLegada ? a.rutaLegada : a.rutaNueva;
    switch (motivo) {
    case AvisoRaizPaquetes::Motivo::Conflicto:
        a.mensaje = QStringLiteral("Existen dos carpetas de paquetes: %1 y %2. La aplicacion sigue usando %1 "
                                   "y no movio ningun archivo. Junta o retira una de las dos y vuelve a abrir la "
                                   "aplicacion.")
                        .arg(a.rutaLegada, a.rutaNueva);
        break;
    case AvisoRaizPaquetes::Motivo::Error:
        if (origenInvalido) {
            a.mensaje = QStringLiteral("La carpeta de paquetes anterior %1 no es una carpeta y no se modifico. La "
                                       "aplicacion usa %2.")
                            .arg(a.rutaLegada, a.rutaNueva);
            break;
        }
        a.mensaje = QStringLiteral("No se pudo mover la carpeta de paquetes de %1 a %2. La aplicacion sigue "
                                   "usando %1 y no movio ningun archivo. Se volvera a intentar al abrir la "
                                   "aplicacion de nuevo.")
                        .arg(a.rutaLegada, a.rutaNueva);
        break;
    case AvisoRaizPaquetes::Motivo::RaizNoDisponible:
        a.mensaje = QStringLiteral("No se pudo preparar la carpeta de paquetes %1. Las descargas fallaran hasta "
                                   "que exista y se pueda escribir en ella.")
                        .arg(a.rutaNueva);
        break;
    }
    return a;
}

ResolucionRaizPaquetes resolucion(const Contexto& c, EstadoRaizPaquetes estado)
{
    ResolucionRaizPaquetes r;
    r.estado = estado;
    switch (estado) {
    case EstadoRaizPaquetes::UsaLegadaPorConflicto:
        r.raizEfectiva = c.absLegada;
        r.aviso = aviso(AvisoRaizPaquetes::Motivo::Conflicto, true);
        qCWarning(lcArranque, "raiz de paquetes: %s (se usa la carpeta previa; no se movio nada)",
                  qUtf8Printable(claveEstable(estado)));
        break;
    case EstadoRaizPaquetes::UsaLegadaPorError:
        r.raizEfectiva = c.absLegada;
        r.aviso = aviso(AvisoRaizPaquetes::Motivo::Error, true);
        qCWarning(lcArranque, "raiz de paquetes: %s (paso %s, errno %d; se usa la carpeta previa)",
                  qUtf8Printable(claveEstable(estado)), c.paso, c.error);
        break;
    default:
        r.raizEfectiva = c.absNueva;
        if (c.error != 0) {
            r.aviso = aviso(AvisoRaizPaquetes::Motivo::RaizNoDisponible, false);
            qCWarning(lcArranque, "raiz de paquetes: %s sin crear (paso %s, errno %d)",
                      qUtf8Printable(claveEstable(estado)), c.paso, c.error);
        } else if (c.origenInvalido) {
            r.aviso = aviso(AvisoRaizPaquetes::Motivo::Error, false, true);
            qCWarning(lcArranque, "raiz de paquetes: %s (%s; no se modifico, se usa la carpeta nueva)",
                      qUtf8Printable(claveEstable(estado)), c.origenInvalido);
        } else {
            qCInfo(lcArranque, "raiz de paquetes: %s", qUtf8Printable(claveEstable(estado)));
        }
        break;
    }
    return r;
}

// mkdir de un directorio NUEVO (0700). Si falla con algo distinto de EEXIST
// pero el directorio quedo creado (p. ej. mkdir bien y chmod mal), se retira
// con rmdir (solo si sigue vacio).
int crearDirectorioNuevo(Contexto& c, const QByteArray& ruta)
{
    const int m = c.ops.crearDirectorio(ruta, MigracionRaizPaquetes::kModoDirectorioNuevo);
    if (m != 0 && m != EEXIST) {
        Tipo t{};
        if (c.ops.tipo(ruta, t) == 0 && t == Tipo::Directorio) {
            c.ops.eliminarDirectorioVacio(ruta);
        }
    }
    return m;
}

// Origen ausente: crea lo que falte de la raiz nueva (solo directorios
// nuevos con 0700). Un fallo no impide arrancar: queda en c.paso/c.error.
EstadoRaizPaquetes asegurarRaizNueva(Contexto& c)
{
    Tipo t{};
    int e = c.ops.tipo(c.padreNuevo, t);
    bool padreCreado = false;
    if (e == ENOENT) {
        if (const int m = crearDirectorioNuevo(c, c.padreNuevo); m != 0) {
            c.fallo("crear padre nuevo", m);
            return EstadoRaizPaquetes::Nueva;
        }
        padreCreado = true;
    } else if (e != 0) {
        c.fallo("lstat padre nuevo", e);
        return EstadoRaizPaquetes::Nueva;
    } else if (!esDirectorioOEnlace(t)) {
        c.fallo("padre nuevo no es directorio", ENOTDIR);
        return EstadoRaizPaquetes::Nueva;
    }

    if (!padreCreado) {
        e = c.ops.tipo(c.raizNueva, t);
        if (e == 0) {
            if (!esDirectorioOEnlace(t)) {
                c.fallo("raiz nueva no es directorio", ENOTDIR);
            }
            return EstadoRaizPaquetes::YaNueva;
        }
        if (e != ENOENT) {
            c.fallo("lstat raiz nueva", e);
            return EstadoRaizPaquetes::YaNueva;
        }
    }
    if (const int m = crearDirectorioNuevo(c, c.raizNueva); m != 0) {
        c.fallo("crear raiz nueva", m);
        if (padreCreado) {
            c.ops.eliminarDirectorioVacio(c.padreNuevo); // vacio: lo acabamos de crear
        }
    }
    return EstadoRaizPaquetes::Nueva;
}

// Lista `padre` (sin seguir enlaces de sus entradas) y dice si solo contiene
// `paquetes` y `.DS_Store`.
int soloContienePaquetes(FileOps& ops, const QByteArray& padre, bool& solo)
{
    QList<FileOps::EntradaDirectorio> entradas;
    if (const int l = ops.listar(padre, entradas); l != 0) {
        return l;
    }
    solo = true;
    for (const auto& entrada : std::as_const(entradas)) {
        if (entrada.nombre != kPaquetes && entrada.nombre != kDsStore) {
            solo = false;
            break;
        }
    }
    return 0;
}

// Clasifica el destino SOLO leyendo.
Destino clasificarDestino(Contexto& c)
{
    Tipo t{};
    const int e = c.ops.tipo(c.padreNuevo, t);
    if (e == ENOENT) {
        return Destino::Libre;
    }
    if (e != 0) {
        c.fallo("lstat padre nuevo", e);
        return Destino::Error;
    }
    if (t != Tipo::Directorio) {
        return Destino::Conflicto; // archivo, enlace u otro con el nombre nuevo
    }
    QList<FileOps::EntradaDirectorio> entradas;
    if (const int l = c.ops.listar(c.padreNuevo, entradas); l != 0) {
        c.fallo("listar padre nuevo", l);
        return Destino::Error;
    }
    if (entradas.isEmpty()) {
        return Destino::VacioPrevio;
    }
    if (entradas.size() != 1 || entradas.first().nombre != kPaquetes
        || entradas.first().tipo != Tipo::Directorio) {
        return Destino::Conflicto;
    }
    QList<FileOps::EntradaDirectorio> dentro;
    if (const int l = c.ops.listar(c.raizNueva, dentro); l != 0) {
        c.fallo("listar raiz nueva", l);
        return Destino::Error;
    }
    return dentro.isEmpty() ? Destino::VacioPrevioConPaquetes : Destino::Conflicto;
}

} // namespace

QString claveEstable(EstadoRaizPaquetes estado)
{
    switch (estado) {
    case EstadoRaizPaquetes::NoAplica:
        return QStringLiteral("no_aplica");
    case EstadoRaizPaquetes::Nueva:
        return QStringLiteral("nueva");
    case EstadoRaizPaquetes::YaNueva:
        return QStringLiteral("ya_nueva");
    case EstadoRaizPaquetes::MigradaPadre:
        return QStringLiteral("migrada_padre");
    case EstadoRaizPaquetes::MigradaPaquetes:
        return QStringLiteral("migrada_paquetes");
    case EstadoRaizPaquetes::UsaLegadaPorConflicto:
        return QStringLiteral("usa_legada_por_conflicto");
    case EstadoRaizPaquetes::UsaLegadaPorError:
        return QStringLiteral("usa_legada_por_error");
    }
    return QStringLiteral("desconocido");
}

MigracionRaizPaquetes::MigracionRaizPaquetes(QString home, FileOps& ops)
    : m_home(QDir::cleanPath(std::move(home)))
    , m_ops(ops)
{
}

QString MigracionRaizPaquetes::rutaLegadaVisible()
{
    return visible(FilesystemPackageStorage::kCarpetaHomeLegada);
}

QString MigracionRaizPaquetes::rutaNuevaVisible()
{
    return visible(FilesystemPackageStorage::kCarpetaHome);
}

ResolucionRaizPaquetes MigracionRaizPaquetes::resolver() const
{
    const QString paquetes = QLatin1String(FilesystemPackageStorage::kCarpetaPaquetes);
    const QString padreLegado = m_home + QLatin1Char('/') + QLatin1String(FilesystemPackageStorage::kCarpetaHomeLegada);
    const QString padreNuevo = m_home + QLatin1Char('/') + QLatin1String(FilesystemPackageStorage::kCarpetaHome);
    Contexto c{m_ops,
               QFile::encodeName(padreLegado),
               QFile::encodeName(padreLegado + QLatin1Char('/') + paquetes),
               QFile::encodeName(padreNuevo),
               QFile::encodeName(padreNuevo + QLatin1Char('/') + paquetes),
               padreLegado + QLatin1Char('/') + paquetes,
               padreNuevo + QLatin1Char('/') + paquetes};
    using E = EstadoRaizPaquetes;

    // 1. Origen (lstat: un enlace se trata como entrada propia).
    bool hayOrigen = false;
    Tipo t{};
    int e = m_ops.tipo(c.padreLegado, t);
    if (e != 0 && e != ENOENT) {
        c.fallo("lstat padre previo", e);
        return resolucion(c, E::UsaLegadaPorError);
    }
    bool padreEsEnlace = false;
    if (e == 0) {
        if (!esDirectorioOEnlace(t)) {
            c.origenInvalido = "padre previo no es directorio";
            return resolucion(c, asegurarRaizNueva(c));
        }
        padreEsEnlace = t == Tipo::Enlace;
        // Con un padre enlace, este lstat lee (sin modificar) el objetivo.
        e = m_ops.tipo(c.raizLegada, t);
        if (e != 0 && e != ENOENT) {
            c.fallo("lstat raiz previa", e);
            return resolucion(c, E::UsaLegadaPorError);
        }
        if (e == 0) {
            if (!esDirectorioOEnlace(t)) {
                c.origenInvalido = "raiz previa no es directorio";
                return resolucion(c, asegurarRaizNueva(c));
            }
            hayOrigen = true;
        }
    }
    if (!hayOrigen) {
        // Sin paquetes previos (o ya migrados, incluido un padre viejo que
        // conserva otras cosas): solo la raiz nueva.
        const E estado = asegurarRaizNueva(c);
        return resolucion(c, estado);
    }

    // 2. Destino (solo lectura).
    const Destino destino = clasificarDestino(c);
    if (destino == Destino::Error) {
        return resolucion(c, E::UsaLegadaPorError);
    }
    if (destino == Destino::Conflicto) {
        return resolucion(c, E::UsaLegadaPorConflicto);
    }

    // 3. Contenido del padre viejo. Un padre ENLACE se renombra completo sin
    // listar su objetivo (D4: el objetivo de un enlace nunca se toca).
    bool soloPaquetes = true;
    if (!padreEsEnlace) {
        const int l = soloContienePaquetes(m_ops, c.padreLegado, soloPaquetes);
        if (l != 0) {
            c.fallo("listar padre previo", l);
            return resolucion(c, E::UsaLegadaPorError);
        }
    }

    // 4. Destino vacio de un intento previo: rmdir (nunca contenido).
    if (destino == Destino::VacioPrevioConPaquetes) {
        if (const int r = m_ops.eliminarDirectorioVacio(c.raizNueva); r != 0) {
            c.fallo("rmdir raiz nueva vacia", r);
            return resolucion(c, E::UsaLegadaPorError);
        }
    }
    if (destino == Destino::VacioPrevio || destino == Destino::VacioPrevioConPaquetes) {
        if (const int r = m_ops.eliminarDirectorioVacio(c.padreNuevo); r != 0) {
            c.fallo("rmdir padre nuevo vacio", r);
            return resolucion(c, E::UsaLegadaPorError);
        }
    }

    // 5. Rename sin reemplazo.
    // TOCTOU: justo antes de renombrar el padre completo se vuelve a listar;
    // si ya no contiene solo `paquetes` (y .DS_Store) se renombra solo
    // `paquetes`. Riesgo residual: algo creado entre esta segunda lectura y el
    // rename viajaria con el padre (ventana de microsegundos, usuario unico,
    // y se renombra, nunca se copia ni se borra).
    if (soloPaquetes && !padreEsEnlace) {
        const int l = soloContienePaquetes(m_ops, c.padreLegado, soloPaquetes);
        if (l != 0) {
            c.fallo("listar padre previo", l);
            return resolucion(c, E::UsaLegadaPorError);
        }
    }
    if (soloPaquetes) {
        if (const int r = m_ops.renombrarSinReemplazo(c.padreLegado, c.padreNuevo); r != 0) {
            c.fallo("renombrar padre", r);
            return resolucion(c, E::UsaLegadaPorError);
        }
        return resolucion(c, E::MigradaPadre);
    }
    if (const int m = crearDirectorioNuevo(c, c.padreNuevo); m != 0) {
        c.fallo("crear padre nuevo", m);
        return resolucion(c, E::UsaLegadaPorError);
    }
    if (const int r = m_ops.renombrarSinReemplazo(c.raizLegada, c.raizNueva); r != 0) {
        c.fallo("renombrar paquetes", r);
        // Se retira el padre recien creado (vacio). Si no se puede, el
        // siguiente arranque lo trata como destino vacio previo.
        if (const int d = m_ops.eliminarDirectorioVacio(c.padreNuevo); d != 0) {
            qCWarning(lcArranque, "raiz de paquetes: no se retiro el padre nuevo vacio (errno %d)", d);
        }
        return resolucion(c, E::UsaLegadaPorError);
    }
    return resolucion(c, E::MigradaPaquetes);
}

MigradorRaizPaquetes migradorRaizPaquetesPosix(const QString& home)
{
    return [home] {
        PosixFileOps ops;
        return MigracionRaizPaquetes(home, ops).resolver();
    };
}

} // namespace satcfdi
