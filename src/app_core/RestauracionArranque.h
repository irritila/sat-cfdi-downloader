#pragma once

#include "app_core/MarcadorRestauracion.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <functional>

namespace satcfdi {

class FileOps;

// T025.5: resultado de la restauracion al arrancar (D5, D13, ADR 0022).
struct ResultadoRestauracionArranque {
    enum class Estado {
        SinMarcador, // nada pendiente
        Aplicada,    // datos restaurados; los anteriores en preDatos/prePaquetes
        Descartada,  // preparacion no confirmada, incompleta o staging cambiado: datos actuales intactos
        Revertida,   // un paso fallo y se deshizo: datos actuales intactos
        Error,       // no se pudo completar ni revertir: NO se arranca (sin estado mixto)
        Interrumpida, // solo pruebas: corte simulado (puntoDeCorte devolvio false)
    };

    Estado estado = Estado::SinMarcador;
    QString diagnostico; // ASCII estable, sin rutas
    QString preDatos;    // con Aplicada
    QString prePaquetes; // con Aplicada (igual a preDatos si comparten carpeta)
    QByteArray idRespaldo;
    QDateTime creadoRespaldoUtc;

    // Texto para el dialogo de error fatal (sin rutas ni datos).
    QString mensajeError() const;
};

QString claveEstable(ResultadoRestauracionArranque::Estado e);

// T025.5: aplica al arrancar una restauracion solicitada. Se ejecuta DESPUES
// de adquirir la instancia unica y ANTES de MigracionRaizPaquetes (T019), del
// bootstrap de SQLite y del worker (D13): ArranqueProceso.
//
// Diario de fases (MarcadorRestauracion; cada fase se escribe de forma
// atomica y durable antes del paso siguiente):
//   preparando / preparado -> se descartan los stagings y el marcador
//                             (el usuario no confirmo): Descartada.
//   solicitado            -> reverifica la huella del staging (cambio:
//                             Descartada) y que la raiz siga siendo una de las
//                             de esta sesion; elige `pre-restauracion-<fecha>`
//                             (sufijo -2, -3... si existe; nunca sobrescribe).
//   pre_elegida           -> crea esas carpetas (0700).
//   moviendo_actuales     -> renames SIN reemplazo (RENAME_EXCL) de base,
//                             -wal, -shm y credentials/ a la de datos, y de la
//                             raiz de paquetes a `<pre>/paquetes` en su
//                             volumen; copia preferencias.ini. Cada rename es
//                             idempotente: destino presente = hecho.
//   actuales_movidos      -> promueve base y `paquetes/` del staging con
//                             renames en el mismo volumen y aplica de las
//                             preferencias restauradas SOLO la lista blanca
//                             (facturas/ultimoPerfil); la seccion `respaldo`
//                             (idInstalacion incluido), `respaldo/` y el
//                             Keychain del equipo no se tocan.
//   promovido             -> borra los stagings y el marcador: Aplicada.
//   revirtiendo           -> devuelve lo restaurado al staging y lo actual a
//                             su lugar, borra stagings y marcador: Revertida.
// Un corte en cualquier punto deja una fase escrita desde la que el
// siguiente arranque continua (o revierte) sin estado mixto. Si ni avanzar ni
// revertir es posible, Error y la app no arranca.
//
// Hilo: el del llamador (un hilo temporal de ArranqueProceso).
class RestauracionArranque {
public:
    struct Opciones {
        QString directorioDatos; // absoluto (el de AppBootstrapper)
        // Raices de paquetes validas en esta sesion: la nueva y la legada del
        // home (T019) o `<data-dir>/paquetes`. Se comparan canonicas.
        QStringList raicesPermitidas;
        std::function<QDateTime()> reloj; // vacio: hora del sistema
        FileOps* ops = nullptr;           // vacio: PosixFileOps
        // Pruebas: se consulta antes de cada paso que cambia algo; false
        // simula que el proceso termino ahi (Interrumpida, sin revertir).
        std::function<bool(const char* paso)> puntoDeCorte;
    };

    explicit RestauracionArranque(Opciones opciones);

    ResultadoRestauracionArranque aplicar();

private:
    Opciones m_opciones;
};

using AplicadorRestauracion = std::function<ResultadoRestauracionArranque()>;

// Produccion: con `directorioDatos` vacio, AppDataLocation; raices segun
// --data-dir (`<dir>/paquetes`) o el home (nueva y legada).
AplicadorRestauracion aplicadorRestauracionPosix(const QString& directorioDatos, bool conDataDir, const QString& home);

} // namespace satcfdi
