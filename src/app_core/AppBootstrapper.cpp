#include "AppBootstrapper.h"

#include "infrastructure/persistence/sqlite/SqlitePersistencia.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QThread>

#include <memory>

namespace satcfdi {

namespace {

const QString kOpcionDataDir = QStringLiteral("--data-dir");

ErrorArranque errorArranque(ErrorArranque::Tipo tipo, QString mensaje,
                            std::optional<ErrorPersistencia> causa = std::nullopt)
{
    ErrorArranque e;
    e.tipo = tipo;
    e.mensaje = std::move(mensaje);
    e.causa = std::move(causa);
    return e;
}

QString mensajeBaseDatos(const ErrorPersistencia& causa)
{
    QString base;
    switch (causa.tipo) {
    case ErrorPersistencia::Tipo::Migracion:
        base = QStringLiteral("La base de datos local no es compatible con esta version de la "
                              "aplicacion o no se pudo migrar. No se modifico.");
        break;
    case ErrorPersistencia::Tipo::Ocupado:
        base = QStringLiteral("La base de datos local esta en uso por otro proceso.");
        break;
    default:
        base = QStringLiteral("No se pudo abrir la base de datos local.");
        break;
    }
    if (!causa.mensaje.isEmpty()) {
        base += QStringLiteral("\n\nDetalle: ") + causa.mensaje;
    }
    return base;
}

} // namespace

void configurarIdentidadAplicacion()
{
    QCoreApplication::setOrganizationName(QStringLiteral("Adenium"));
    QCoreApplication::setApplicationName(QStringLiteral("SAT CFDI Downloader"));
}

Resultado<AppBootstrapper::Opciones, ErrorArranque>
AppBootstrapper::opcionesDesdeArgumentos(const QStringList& argumentos)
{
    Opciones opciones;
    const QString prefijo = kOpcionDataDir + QLatin1Char('=');
    for (qsizetype i = 1; i < argumentos.size(); ++i) {
        const QString& arg = argumentos.at(i);
        QString valor;
        if (arg == kOpcionDataDir) {
            if (i + 1 >= argumentos.size()) {
                return Resultado<Opciones, ErrorArranque>::fallo(errorArranque(
                    ErrorArranque::Tipo::ArgumentoInvalido,
                    QStringLiteral("La opcion --data-dir requiere un directorio.")));
            }
            valor = argumentos.at(++i);
        } else if (arg.startsWith(prefijo)) {
            valor = arg.mid(prefijo.size());
        } else {
            continue;
        }
        if (valor.trimmed().isEmpty()) {
            return Resultado<Opciones, ErrorArranque>::fallo(
                errorArranque(ErrorArranque::Tipo::ArgumentoInvalido,
                              QStringLiteral("La opcion --data-dir requiere un directorio.")));
        }
        opciones.directorioDatos = valor;
    }
    return Resultado<Opciones, ErrorArranque>::exito(std::move(opciones));
}

QString AppBootstrapper::directorioDatosPorDefecto()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

AppBootstrapper::AppBootstrapper(Opciones opciones)
    : m_opciones(std::move(opciones))
{
}

Resultado<ArranquePreparado, ErrorArranque> AppBootstrapper::preparar() const
{
    using R = Resultado<ArranquePreparado, ErrorArranque>;

    QString directorio = m_opciones.directorioDatos.isEmpty() ? directorioDatosPorDefecto()
                                                              : m_opciones.directorioDatos;
    if (directorio.isEmpty()) {
        return R::fallo(errorArranque(ErrorArranque::Tipo::DirectorioDatos,
                                      QStringLiteral("No se pudo determinar el directorio de "
                                                     "datos de la aplicacion.")));
    }
    directorio = QDir::cleanPath(QDir(directorio).absolutePath());

    // inicializarBaseSqlite no crea el directorio padre.
    if (!QDir().mkpath(directorio)) {
        return R::fallo(errorArranque(ErrorArranque::Tipo::DirectorioDatos,
                                      QStringLiteral("No se pudo crear el directorio de datos "
                                                     "de la aplicacion.")));
    }

    const QString rutaBase = QDir(directorio).filePath(QString::fromLatin1(kNombreArchivoBase));

    Inicializador inicializador = m_opciones.inicializador;
    if (!inicializador) {
        const auto migraciones = m_opciones.migraciones;
        inicializador = [migraciones](const QString& ruta) {
            return migraciones ? inicializarBaseSqlite(ruta, *migraciones)
                               : inicializarBaseSqlite(ruta);
        };
    }

    // SQL de bootstrap fuera del hilo grafico: hilo temporal unido aqui.
    std::optional<Resultado<InformeInicializacionSqlite, ErrorPersistencia>> salida;
    const std::unique_ptr<QThread> hilo(
        QThread::create([&salida, &inicializador, &rutaBase] { salida = inicializador(rutaBase); }));
    hilo->setObjectName(QStringLiteral("satcfdi-bootstrap"));
    hilo->start();
    hilo->wait();

    if (!salida) {
        return R::fallo(errorArranque(
            ErrorArranque::Tipo::BaseDatos, QStringLiteral("No se pudo abrir la base de datos local.")));
    }
    auto inicializacion = std::move(*salida);
    if (!inicializacion) {
        const ErrorPersistencia& causa = inicializacion.error();
        return R::fallo(
            errorArranque(ErrorArranque::Tipo::BaseDatos, mensajeBaseDatos(causa), causa));
    }

    ArranquePreparado preparado;
    preparado.directorioDatos = directorio;
    preparado.rutaBase = rutaBase;
    preparado.informe = std::move(inicializacion).valor();
    return R::exito(std::move(preparado));
}

} // namespace satcfdi
