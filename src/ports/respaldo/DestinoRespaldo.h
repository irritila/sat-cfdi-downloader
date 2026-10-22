#pragma once

// Puerto del destino de un respaldo (T025.3; T025 D1, D8, D17, D20, D21). Una
// carpeta elegida por el usuario (normalmente dentro de su carpeta
// sincronizada). El adaptador real es POSIX/macOS
// (infrastructure/storage/DestinoRespaldoPosix); las pruebas usan un fake con
// inyeccion de errores.
//
// Sesion (M2): abrir() abre la carpeta UNA vez por respaldo y fija su
// identidad (st_dev/st_ino). Todo lo demas opera con *at() sobre ese
// descriptor; comprobarIdentidad() reabre la ruta y aborta con CarpetaCambiada
// si ya no es la misma carpeta. Toda la E/S corre en el hilo de quien abrio
// (el del RespaldoService). La materializacion de archivos sin descargar se
// desactiva SOLO mientras vive el dispositivo de abrirParaLeer() (lectura de
// cabeceras en la retencion, D20); la escritura corre con la politica normal.
//
// Reglas:
// - Ningun nombre de la carpeta se sigue si es un enlace: listar() solo
//   devuelve archivos regulares; abrirParaLeer() y borrar() rechazan enlaces.
// - El `.partial` es propio, oculto y opaco: `.archivo-fiscal-<propietario>-
//   <16 hex>.partial` (<propietario> = 8 hex del id de instalacion). D21: se
//   escribe en los datos de la app si estan en el mismo volumen que la
//   carpeta; si no, dentro de la carpeta.
// - publicar(): rename exclusivo (RENAME_EXCL). Un nombre existente nunca se
//   sobrescribe (Conflicto); si el sistema de archivos no admite el rename
//   exclusivo, ExclusivoNoSoportado (destino no soportado en v1; nunca se usa
//   un rename normal).
// - Errores con diagnostico ASCII estable, sin rutas ni nombres.

#include "domain/common/Resultado.h"
#include "ports/persistence/Exito.h"

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <memory>
#include <optional>
#include <utility>

QT_BEGIN_NAMESPACE
class QIODevice;
QT_END_NAMESPACE

namespace satcfdi::respaldo {

struct ErrorDestino {
    enum class Categoria {
        CarpetaInexistente,
        NoEsCarpeta,          // existe pero no es un directorio
        EnlaceIntermedio,     // un componente intermedio de la ruta es un enlace del usuario
        DuenoDistinto,        // la carpeta no pertenece al usuario (st_uid != getuid())
        CarpetaCambiada,      // la ruta ya no apunta a la carpeta abierta (M2)
        SinEscritura,         // permisos, solo lectura o la prueba de escritura fallo
        VolumenNoDisponible,  // volumen desmontado o red caida (ENOTCONN, ESTALE, ETIMEDOUT)
        SinDescargar,         // archivo o carpeta solo en la nube (EDEADLK sin materializar, D20)
        EspacioInsuficiente,  // espacio libre < estimado + margen, o ENOSPC/EDQUOT al escribir
        Escritura,            // otra falla de E/S al escribir el .partial
        Sincronizacion,       // fsync / F_FULLFSYNC fallo
        Conflicto,            // el nombre final ya existe (RENAME_EXCL)
        ExclusivoNoSoportado, // el sistema de archivos no admite RENAME_EXCL (M1)
        Publicacion,          // rename fallo por otra causa
        Lectura,              // listar o abrir para leer
        NoRegular,            // abrir/borrar un enlace u objeto no regular
        Interno,
    };

    Categoria categoria = Categoria::Interno;
    QString diagnostico;

    static ErrorDestino de(Categoria c, QString diagnostico = {})
    {
        ErrorDestino e;
        e.categoria = c;
        e.diagnostico = std::move(diagnostico);
        return e;
    }

    bool operator==(const ErrorDestino&) const = default;
};

inline QString claveEstable(ErrorDestino::Categoria c)
{
    using C = ErrorDestino::Categoria;
    switch (c) {
    case C::CarpetaInexistente: return QStringLiteral("carpeta_inexistente");
    case C::NoEsCarpeta: return QStringLiteral("no_es_carpeta");
    case C::EnlaceIntermedio: return QStringLiteral("enlace_intermedio");
    case C::DuenoDistinto: return QStringLiteral("dueno_distinto");
    case C::CarpetaCambiada: return QStringLiteral("carpeta_cambiada");
    case C::SinEscritura: return QStringLiteral("sin_escritura");
    case C::VolumenNoDisponible: return QStringLiteral("volumen_no_disponible");
    case C::SinDescargar: return QStringLiteral("sin_descargar");
    case C::EspacioInsuficiente: return QStringLiteral("espacio_insuficiente");
    case C::Escritura: return QStringLiteral("escritura");
    case C::Sincronizacion: return QStringLiteral("sincronizacion");
    case C::Conflicto: return QStringLiteral("conflicto");
    case C::ExclusivoNoSoportado: return QStringLiteral("exclusivo_no_soportado");
    case C::Publicacion: return QStringLiteral("publicacion");
    case C::Lectura: return QStringLiteral("lectura");
    case C::NoRegular: return QStringLiteral("no_regular");
    case C::Interno: return QStringLiteral("interno");
    }
    return QStringLiteral("interno");
}

struct EstadoDestino {
    quint64 bytesLibres = 0;
};

// Archivo regular de la carpeta. `sinDescargar` (D20): el contenido esta solo
// en la nube (SF_DATALESS); leerlo lo descargaria, asi que no se abre.
struct EntradaDestino {
    QString nombre;
    quint64 tamano = 0;
    quint64 dispositivo = 0;
    quint64 inodo = 0;
    bool sinDescargar = false;

    bool operator==(const EntradaDestino&) const = default;
};

// `.partial` propio abierto para escritura. Si se destruye sin publicar ni
// descartar (terminacion forzada), el archivo queda y lo borra
// DestinoRespaldo::limpiarParciales() en el siguiente arranque.
class ArchivoParcial {
public:
    virtual ~ArchivoParcial() = default;

    virtual QString nombre() const = 0;
    // D21: true si se escribe dentro de la carpeta destino (otro volumen que
    // los datos de la app), false si en los datos de la app.
    virtual bool enCarpetaDestino() const = 0;
    // Dispositivo de escritura (secuencial); valido mientras viva el objeto.
    virtual QIODevice& dispositivo() = 0;
    // Si una escritura del dispositivo fallo: su categoria.
    virtual std::optional<ErrorDestino> errorEscritura() const = 0;
    // Vacia buffers y hace F_FULLFSYNC (fsync si el sistema no lo admite).
    virtual Resultado<Exito, ErrorDestino> sincronizar() = 0;
    // Cierra y renombra a `nombreFinal` dentro de la carpeta destino con
    // RENAME_EXCL; despues sincroniza las carpetas (best effort) y devuelve la
    // entrada publicada (tamano e identificador, para el registro D20).
    // Conflicto: el .partial sigue y se puede reintentar con otro nombre.
    virtual Resultado<EntradaDestino, ErrorDestino> publicar(const QString& nombreFinal) = 0;
    // Cierra y borra el .partial. Idempotente.
    virtual void descartar() = 0;
};

class DestinoRespaldo {
public:
    virtual ~DestinoRespaldo() = default;

    // Abre la carpeta una vez (identidad fija). Valida: solo el ultimo
    // componente puede ser un enlace del usuario (los intermedios solo si son
    // del sistema, de root, como /var o /Volumes/Macintosh HD); el destino es
    // un directorio del usuario; el volumen se deduce del descriptor y debe
    // estar montado. Idempotente.
    virtual Resultado<Exito, ErrorDestino> abrir() = 0;

    // "dev:ino" de la carpeta abierta (para el registro D20); vacio si no abrio.
    virtual QString identidad() const = 0;

    // Reabre la ruta y compara st_dev/st_ino con la carpeta abierta.
    virtual Resultado<Exito, ErrorDestino> comprobarIdentidad() = 0;

    // Antes de escribir: escritura real (un archivo de prueba que se
    // desvincula apenas se crea) y espacio libre >= `bytesNecesarios` (el
    // llamador suma el margen). Requiere abrir().
    virtual Resultado<EstadoDestino, ErrorDestino> validar(quint64 bytesNecesarios) = 0;

    // Crea un `.partial` propio nuevo del `propietario` (8 hex). Requiere abrir().
    virtual Resultado<std::unique_ptr<ArchivoParcial>, ErrorDestino> crearParcial(const QString& propietario) = 0;

    // Archivos REGULARES de la carpeta (sin seguir enlaces), sin orden.
    virtual Resultado<QList<EntradaDestino>, ErrorDestino> listar() = 0;

    // Abre para leer un archivo regular y descargado (O_NOFOLLOW). Un archivo
    // sin descargar da SinDescargar sin descargarlo. Mientras viva el
    // dispositivo, este hilo no materializa archivos sin descargar. Cada
    // llamada da un dispositivo nuevo en la posicion 0.
    virtual Resultado<std::unique_ptr<QIODevice>, ErrorDestino> abrirParaLeer(const QString& nombre) = 0;

    // Borra un archivo regular con unlinkat sobre la carpeta (borrar uno sin
    // descargar no lo descarga); un enlace o no regular da NoRegular.
    virtual Resultado<Exito, ErrorDestino> borrar(const QString& nombre) = 0;

    // Borra los `.partial` regulares del `propietario` en la carpeta y en los
    // datos de la app (D21), sin seguir enlaces. Devuelve cuantos borro.
    virtual Resultado<int, ErrorDestino> limpiarParciales(const QString& propietario) = 0;
};

// Registro local de lo publicado (D20): fuera de SQLite y del respaldo, por
// instalacion. Permite depurar copias que estan solo en la nube sin
// descargarlas.
struct EntradaPublicada {
    QString carpeta; // DestinoRespaldo::identidad()
    QString nombre;
    QByteArray idRespaldo;
    QDateTime fechaUtc;
    quint64 tamano = 0;
    quint64 dispositivo = 0;
    quint64 inodo = 0;

    bool operator==(const EntradaPublicada&) const = default;
};

struct EstadoRegistro {
    QList<EntradaPublicada> publicados;
    QList<QByteArray> duplicadosAvisados; // B2: idRespaldo ya avisados como duplicados
};

class RegistroPublicados {
public:
    virtual ~RegistroPublicados() = default;
    // Sin archivo: estado vacio. Archivo ilegible: Lectura.
    virtual Resultado<EstadoRegistro, ErrorDestino> cargar() = 0;
    // Escritura atomica.
    virtual Resultado<Exito, ErrorDestino> guardar(const EstadoRegistro& estado) = 0;
};

// Nombres (puros).
namespace nombres {

inline QString prefijoParcial(const QString& propietario)
{
    return QStringLiteral(".archivo-fiscal-") + propietario + QLatin1Char('-');
}
inline const QString kSufijoParcial = QStringLiteral(".partial");
inline const QString kPrefijoPublicado = QStringLiteral("archivo-fiscal-");
inline const QString kSufijoPublicado = QStringLiteral(".afbak");

inline bool esHex(QStringView s)
{
    for (const QChar c : s) {
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'))) {
            return false;
        }
    }
    return !s.isEmpty();
}

// `.archivo-fiscal-<propietario>-<16 hex>.partial` exacto.
inline bool esParcialDe(const QString& nombre, const QString& propietario)
{
    const QString prefijo = prefijoParcial(propietario);
    if (!nombre.startsWith(prefijo) || !nombre.endsWith(kSufijoParcial)) {
        return false;
    }
    const QStringView medio =
        QStringView(nombre).mid(prefijo.size(), nombre.size() - prefijo.size() - kSufijoParcial.size());
    return medio.size() == 16 && esHex(medio);
}

// Candidato a respaldo publicado: `archivo-fiscal-*.afbak`, no oculto. El
// contenido (o el registro, D20) decide si es propio.
inline bool pareceRespaldo(const QString& nombre)
{
    return nombre.startsWith(kPrefijoPublicado) && nombre.endsWith(kSufijoPublicado)
        && !nombre.contains(QLatin1Char('/'));
}

} // namespace nombres

} // namespace satcfdi::respaldo
