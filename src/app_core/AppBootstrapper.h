#pragma once

#include "domain/common/Resultado.h"
#include "infrastructure/persistence/sqlite/SqliteTipos.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace satcfdi {

// Error fatal de arranque. `mensaje` es apto para mostrarse al usuario: no
// contiene rutas, SQL ni secretos (ErrorPersistencia::mensaje ya es saneado).
struct ErrorArranque {
    enum class Tipo {
        ArgumentoInvalido, // --data-dir sin valor
        DirectorioDatos,   // no se pudo resolver o crear el directorio
        BaseDatos,         // inicializarBaseSqlite fallo (DM4, migracion, E/S)
    };

    Tipo tipo = Tipo::BaseDatos;
    QString mensaje;
    std::optional<ErrorPersistencia> causa;
};

// Base lista para el composition root.
struct ArranquePreparado {
    QString directorioDatos;
    QString rutaBase; // <directorioDatos>/satcfdi.sqlite3
    InformeInicializacionSqlite informe;
    QString raizPaquetes; // T008 D8: raiz efectiva de los ZIP (absoluta)
};

// T008 D8: crea `raiz` y sus componentes FALTANTES con 0700 (no cambia
// permisos de carpetas existentes). Un fallo solo deja un warning. E/S: se
// llama desde el hilo temporal de AppBootstrapper, nunca desde el grafico.
void crearRaizPaquetesPrivada(const QString& raiz);

// Fija organizationName ("Adenium") y applicationName ("SAT CFDI Downloader").
// main.cpp la llama justo despues de crear QApplication y antes de resolver la
// ruta de datos (determinan QStandardPaths::AppDataLocation).
void configurarIdentidadAplicacion();

// Arranque de T003 (DA5): resuelve el directorio de datos, lo crea e
// inicializa la base SQLite (DM4, PRAGMAs, migraciones, WAL). No muestra
// dialogos: main.cpp decide como presentar ErrorArranque.
//
// Hilos (criterio "ningun SQL en el hilo grafico"): preparar() ejecuta la
// inicializacion en un QThread temporal y lo une antes de volver; la conexion
// de bootstrap se abre, usa, cierra y retira en ese hilo. El llamador (hilo
// grafico, antes de crear dispatcher y QML) solo espera.
//
// Precondicion: configurarIdentidadAplicacion() ya llamada.
class AppBootstrapper {
public:
    static constexpr const char* kNombreArchivoBase = "satcfdi.sqlite3";

    // Inicializa la base en `rutaBase`. Corre en el hilo temporal.
    using Inicializador = std::function<Resultado<InformeInicializacionSqlite, ErrorPersistencia>(
        const QString& rutaBase)>;

    struct Opciones {
        // Vacio: QStandardPaths::AppDataLocation.
        QString directorioDatos;
        // Vacio: migraciones embebidas (`:/migrations/...`). Inyectable en pruebas.
        std::optional<QList<MigracionSql>> migraciones;
        // Vacio: inicializarBaseSqlite con `migraciones`. Inyectable en pruebas
        // (p. ej. para registrar el hilo y delegar en la real).
        Inicializador inicializador;
        // T008 D8: raiz de paquetes; vacia = <directorioDatos>/paquetes.
        QString raizPaquetes;
        // Crea la raiz en el hilo temporal; vacia = crearRaizPaquetesPrivada.
        // Costura para pruebas (p. ej. registrar el hilo y delegar).
        std::function<void(const QString&)> crearRaizPaquetes;
    };

    // Acepta `--data-dir <dir>` y `--data-dir=<dir>`; ignora otros argumentos
    // (Qt ya retiro los suyos de QCoreApplication::arguments()).
    static Resultado<Opciones, ErrorArranque> opcionesDesdeArgumentos(const QStringList& argumentos);

    static QString directorioDatosPorDefecto();

    explicit AppBootstrapper(Opciones opciones);

    Resultado<ArranquePreparado, ErrorArranque> preparar() const;

private:
    Opciones m_opciones;
};

} // namespace satcfdi
