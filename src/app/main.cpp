#include "OSIntegrationFactory.h"
#include "RespaldoPlataformaFactory.h"
#include "SecretStoreFactory.h"
#include "app_core/AppBootstrapper.h"
#include "app_core/ArranqueProceso.h"
#include "app_core/AppCompositionRoot.h"
#include "app_core/AppLifecycleController.h"
#include "app_core/IdInstalacionRespaldo.h"
#include "app_core/SingleInstanceCoordinator.h"
#include "presentation/estilo/EstiloVisual.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>

#include <cstdio>
#include <unistd.h>

namespace {

// Error fatal de arranque: no se carga QML contra una base no lista.
int errorFatal(const QString& mensaje)
{
    qCritical("Error de arranque: %s", qUtf8Printable(mensaje));
    QMessageBox::critical(nullptr, QApplication::applicationDisplayName(), mensaje);
    return 2;
}

} // namespace

// Punto de entrada de satcfdi_app (T004). Orden obligatorio:
// 1. QApplication e identidad Qt.  2. Argumentos.  3. Instancia unica (la
// secundaria envia ActivateWindow y termina con 0 sin tocar SQLite).
// 3b. Restauracion pendiente (T025.5).  4. Bootstrap SQLite (con T019).  5. Composition root, OSIntegration y
// AppLifecycleController.  Cerrar la ventana la oculta; solo Salir termina.
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    satcfdi::configurarIdentidadAplicacion();
    // T013 D1: estilo Basic antes de cargar QML.
    satcfdi::presentacion::fijarEstiloBasico();

    auto opciones = satcfdi::AppBootstrapper::opcionesDesdeArgumentos(QApplication::arguments());
    if (!opciones) {
        return errorFatal(opciones.error().mensaje);
    }

    // Vive todo el proceso: mientras exista, esta es la instancia primaria.
    satcfdi::SingleInstanceCoordinator instancia;
    // T008 D8 / T019 D4: con --data-dir los ZIP van a <dir>/paquetes (sin
    // migracion ni rutas del home); sin el, a ~/Archivo-Fiscal/paquetes (no a
    // AppDataLocation), migrando la carpeta de paquetes previa si existe
    // (MigracionRaizPaquetes). El bootstrapper lo resuelve en su hilo de E/S,
    // despues de adquirir la instancia unica y antes de crear el worker.
    satcfdi::AppBootstrapper::Opciones arranque = std::move(opciones).valor();
    satcfdi::AppBootstrapper::configurarRaizPaquetes(arranque, QDir::homePath());
    // T025.5 (D13): una restauracion solicitada se aplica tras adquirir la
    // instancia unica y antes de T019, SQLite y el worker (prepararProceso).
    const satcfdi::AplicadorRestauracion restauracion = satcfdi::aplicadorRestauracionPosix(
        arranque.directorioDatos, !arranque.directorioDatos.isEmpty(), QDir::homePath());
    const satcfdi::AppBootstrapper bootstrapper(std::move(arranque));
    const auto preparacion = satcfdi::prepararProceso(instancia, bootstrapper, restauracion);
    switch (preparacion.tipo) {
    case satcfdi::PreparacionProceso::Tipo::Lista:
        break;
    case satcfdi::PreparacionProceso::Tipo::Secundaria:
        return preparacion.codigoSalida();
    case satcfdi::PreparacionProceso::Tipo::Error:
        return errorFatal(preparacion.mensajeError);
    }

    // Adaptador de SO: solo en la primaria y ANTES de cualquier procesamiento
    // de eventos (lee el Apple Event de arranque para LaunchContext). Hasta
    // aqui no se proceso ningun evento: adquirir() solo hace listen() (o, en
    // la secundaria, E/S bloqueante de su propio socket) y el bootstrap espera
    // su hilo con wait(). Se declara despues de `app` y antes de `root`: vive
    // mas que el root/controlador y se destruye antes que QApplication.
    const std::unique_ptr<satcfdi::OSIntegration> os = satcfdi::crearOSIntegracion();
    // SecretStore (T005): lo posee main y vive mas que el root y que toda
    // CredencialPreparada (se destruye despues de cerrar el dispatcher).
    const std::unique_ptr<satcfdi::SecretStore> secretStore =
        satcfdi::crearSecretStore(preparacion.arranque.directorioDatos);
    if (!secretStore) {
        return errorFatal(QStringLiteral("El almacen de credenciales no esta disponible en esta "
                                         "plataforma."));
    }
    // T025.4: KEK del respaldo y carpeta por bookmark; los posee main y viven
    // mas que el root. El almacen se acota al id de instalacion (seguridad 1).
    // obtenerOCrearIdInstalacion escribe preferencias.ini solo en el primer
    // arranque de la instalacion (si el id falta), antes de construir el root:
    // es el mismo id que el root usara despues (lo lee del mismo ini). El
    // composition root sigue sin escribir archivos al construirse.
    const QUuid idInstalacion = satcfdi::obtenerOCrearIdInstalacion(
        QDir(QFileInfo(preparacion.arranque.rutaBase).absolutePath()).filePath(QStringLiteral("preferencias.ini")));
    const std::unique_ptr<satcfdi::respaldo::AlmacenKekRespaldo> almacenKek =
        satcfdi::crearAlmacenKekRespaldo(idInstalacion);
    const std::unique_ptr<satcfdi::respaldo::CarpetaRespaldoSistema> carpetaRespaldo =
        satcfdi::crearCarpetaRespaldoSistema();
    satcfdi::OpcionesMonitoreo monitoreo;
    monitoreo.almacenKekRespaldo = almacenKek.get();
    monitoreo.carpetaRespaldoSistema = carpetaRespaldo.get();
    monitoreo.raizPaquetes = preparacion.arranque.raizPaquetes;
    // T019 D8: aviso de conflicto o fallo de la carpeta de paquetes (UI).
    monitoreo.avisoRaizPaquetes = preparacion.arranque.resolucionRaizPaquetes.aviso;
    if (preparacion.restauracion.estado != satcfdi::ResultadoRestauracionArranque::Estado::SinMarcador) {
        monitoreo.restauracionAlArrancar = preparacion.restauracion;
    }
    int codigo = 0;
    {
        satcfdi::AppCompositionRoot root(preparacion.arranque.rutaBase, *secretStore, monitoreo);
        if (!root.cargar()) {
            return 1;
        }

        QApplication::setQuitOnLastWindowClosed(false);
        satcfdi::AppLifecycleController& ciclo = root.iniciarCicloDeVida(*os, &instancia);
        // T025.6 C7: tras una restauracion aplicada la ventana se muestra y se
        // activa al frente aunque el arranque fuera por Login Item u oculto
        // (iniciar() solo la muestra con LaunchContext::Manual). Mostrarla pasa
        // la app a modo Regular y la activa (MacOSIntegration, ADR 0009).
        if (preparacion.restauracion.estado == satcfdi::ResultadoRestauracionArranque::Estado::Aplicada) {
            ciclo.mostrarVentana();
        }

        codigo = QApplication::exec();
    } // el root cierra el ejecutor, el dispatcher y la base

    // T025.5 R2: un hilo de restauracion puede seguir bloqueado leyendo un
    // archivo que el proveedor no entrega (fileproviderd). Ya con la base
    // cerrada, se termina sin destructores estaticos para no esperar ni
    // destruir objetos que ese hilo aun referencia.
    if (satcfdi::debeTerminarSinDestructores()) {
        qWarning("salida inmediata: hilo de restauracion aun bloqueado en lectura");
        std::fflush(nullptr);
        ::_exit(codigo);
    }
    return codigo;
}
