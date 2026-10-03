# T002: Shell Qt/QML ejecutable

## Estado

Completada

## Prioridad y tamano

- Prioridad: Alta.
- Tamano: Mediana.

## Objetivo

Crear una app macOS ejecutable con Qt 6, QML, C++ y CMake que permita validar el puente entre la presentacion y los view models con datos simulados.

## Contexto

Se necesita un resultado visible temprano, pero todavia no deben introducirse SQLite, Keychain, SAT ni reglas productivas. La app debe seguir la estructura por capas definida para evitar que el prototipo contamine la arquitectura.

## Alcance

### Incluye

- Proyecto Qt 6.8 LTS con CMake y C++20.
- Target ejecutable `satcfdi_app` como bundle macOS.
- Targets internos minimos para dominio, puertos, aplicacion y presentacion.
- Esqueletos iniciales de los contratos en `satcfdi_ports`: `SatGateway`, `SecretStore`, `OSIntegration`, `PackageStorage`, `LogSanitizer` y repositorios.
- Composition root inicial.
- Modulo QML propio `SatCfdiDownloader`.
- Ventana principal QML.
- Navegacion base entre solicitudes, nueva solicitud y detalle.
- View models y datos simulados en memoria.
- Pruebas basicas de compilacion, dominio/aplicacion demo y carga de QML.

### No incluye

- Persistencia SQLite real.
- Repositorios productivos.
- Credenciales SAT o Keychain.
- Llamadas SAT, worker o ejecutor serial productivo.
- Menu bar, autostart y notificaciones; pertenecen a `T004`.
- Formularios completos de perfiles y credenciales.
- Pagina de perfiles SAT; T002 solo incluye un modelo demo de perfiles activos para el selector de nueva solicitud.
- Worker, temporizadores, cola serial, backoff, recuperacion o transiciones asincronas.
- Escritura o lectura de ZIP.

## Dependencias

- `docs/design/operational-rules.md` para nombres y estados del dominio.
- `docs/design/qt-project-structure.md`.
- ADR 0011 y ADR 0012.

## Trabajo esperado

1. Crear `CMakeLists.txt` raiz y targets definidos por la estructura Qt.
2. Crear `main.cpp`, composition root y carga de recursos QML.
3. Registrar el modulo `SatCfdiDownloader` con `qt_add_qml_module`.
4. Crear los esqueletos de contratos en `satcfdi_ports`, sin fijar detalles SOAP que dependan del spike SAT.
5. Crear un modelo fake de solicitudes y view models para lista, nueva solicitud y detalle.
6. Implementar navegacion minima entre las vistas.
7. Agregar pruebas automatizadas minimas separadas para dominio/aplicacion y presentacion.
8. Documentar prerrequisitos locales para encontrar Qt 6.8 mediante `CMAKE_PREFIX_PATH` cuando aplique.

## Decisiones cerradas del refinamiento

- T002 usa `QApplication` para quedar alineada con el stack futuro, pero no implementa menu bar ni ciclo de vida macOS. En T002, cerrar la ventana termina el proceso.
- `satcfdi_infrastructure` existe solo como target minimo para conservar el grafo; no contiene adaptadores SQLite, SAT, Keychain, archivos ni macOS.
- Los puertos de `satcfdi_ports` son fronteras compilables con destructor virtual y documentacion de responsabilidad. En T002 no declaran metodos, DTO SAT, codigos, envelopes SOAP, rutas ni detalles de firma.
- El contrato funcional consumido por presentacion vive en `satcfdi_application` como `SolicitudesService` demo, con `listar`, `obtener`, `crear` y notificacion de solicitud actualizada.
- `DemoSolicitudesService` se arma exclusivamente en el composition root y se marca como demo. T003 podra reemplazarlo sin cambiar las vistas.
- Los view models se inyectan desde el composition root mediante propiedades iniciales o `required`; no se usan singletons globales.
- Todo corre en el hilo grafico. No se crean hilos ni timers de simulacion.
- Los datos demo cubren estados representativos de `docs/design/operational-rules.md`, pero no implementan transiciones reales.
- No se incluye `PerfilesSatPage`. `NuevaSolicitudViewModel` expone `perfilesDisponibles` como modelo demo de solo lectura con perfiles activos.
- Las pruebas de presentacion van en un ejecutable separado `satcfdi_presentation_tests`.

## Restricciones tecnicas

- QML solo maneja layout, navegacion y estado visual.
- Los view models C++ exponen propiedades, comandos y modelos a QML.
- Ningun archivo QML importa repositorios, SQL, SAT, filesystem o Keychain.
- El composition root es el unico punto que conecta implementaciones concretas.
- El shell debe poder reemplazar los datos fake por repositorios sin redisenar las vistas.
- Los contratos iniciales son fronteras compilables; los DTOs SAT pueden ajustarse despues de `T006` sin quebrar los targets.
- `estado_local` y `estado_solicitud_sat` se modelan por separado desde el primer shell.
- QML recibe claves estables de estado, no texto de negocio persistido; el texto visible se resuelve en presentacion.
- La seleccion de detalle se hace por `id`, nunca por indice de fila.

## Contrato minimo de presentacion

- `SolicitudesListModel` expone roles `id`, `perfilRfc`, `rfcContraparte`, `tipoDescarga`, `fechaInicial`, `fechaFinal`, `estadoLocal`, `estadoSat`, `estadoResumen`, `creadaEn` y `totalPaquetes`.
- `estadoSat` es nulo cuando no existe estado SAT.
- `estadoResumen` usa claves estables como `Creada`, `Enviando`, `Enviada`, `EnvioFallido`, `EnvioIncierto`, `Aceptada`, `EnProceso`, `Terminada`, `ErrorSat`, `Rechazada` y `Vencida`.
- `NuevaSolicitudViewModel` expone campos editables, `perfilesDisponibles`, `canSubmit`, `ocupado`, `errorMessage`, comando `submit()` y senal `submitted(id)`.
- La validacion del formulario es superficial: campos requeridos y fecha final mayor o igual a fecha inicial.
- `SolicitudDetailViewModel` carga por `id` y muestra metadata, estado local, estado SAT y paquetes simulados.
- El demo puede emitir errores de validacion; deduplicacion, limites SAT, codigos SAT y advertencias productivas quedan fuera.

## Criterios de aceptacion

- [x] `cmake -S . -B build` configura el proyecto correctamente cuando el entorno puede encontrar Qt 6.8; si hace falta, la tarea documenta `CMAKE_PREFIX_PATH`.
- [x] `cmake --build build` genera `satcfdi_app` como bundle macOS.
- [x] `ctest --test-dir build` se ejecuta sin fallos.
- [x] La app abre una ventana principal QML sin errores de carga.
- [x] La ventana muestra una lista de solicitudes simuladas.
- [x] Se puede navegar de la lista a nueva solicitud y al detalle.
- [x] El detalle muestra metadata simulada de la solicitud seleccionada.
- [x] Al enviar una nueva solicitud simulada valida, se abre el detalle por `id` y la solicitud aparece en la lista al regresar.
- [x] Con datos demo vacios, la lista muestra un estado vacio.
- [x] Con fechas invalidas o sin perfil, el formulario impide enviar y muestra error visible.
- [x] El detalle separa estado local, estado SAT y paquetes simulados.
- [x] Las filas de lista y badges de estado usan texto accesible y no dependen solo de color.
- [x] El flujo lista, nueva solicitud y detalle puede recorrerse con teclado.
- [x] Redimensionar la ventana no corta contenido esencial.
- [x] QML no accede directamente a SQLite, archivos, secretos ni SAT.
- [x] La estructura de targets y carpetas coincide con `qt-project-structure.md`.
- [x] Existen contratos compilables en `satcfdi_ports` para SAT, secretos, sistema operativo, paquetes, logs y repositorios.
- [x] Los contratos no contienen detalles SOAP inventados ni implementaciones concretas.
- [x] `satcfdi_ports` no declara metodos productivos que adelanten T003, T006 o T007.
- [x] `satcfdi_presentation_tests` carga el modulo QML y verifica roles/navegacion basica.
- [x] T002 no contiene `QSystemTrayIcon`, `setQuitOnLastWindowClosed(false)`, `LSUIElement`, instancia unica, `MacOSIntegration` ni `FakeOSIntegration`.
- [x] T002 no agrega dependencias Qt `Sql` ni `Network`.
- [x] No se introducen tablas, migraciones ni dependencias de integracion SAT en esta tarea.

## Verificacion

1. Configurar y compilar con CMake.
2. Ejecutar `ctest`.
3. Abrir el `.app` generado.
4. Recorrer lista, nueva solicitud y detalle.
5. Probar el recorrido con teclado.
6. Revisar imports QML y dependencias de los targets para confirmar las fronteras de capa.
7. Confirmar que la consola no muestra errores de carga QML.

## Definicion de terminado

- El bundle abre en macOS desde el build local.
- Las tres vistas son navegables con datos fake.
- El puente C++/QML esta funcionando.
- Las pruebas y comandos de compilacion documentados pasan.
- `T003` puede reemplazar los datos fake por persistencia sin modificar la estructura de navegacion.
- `T004` puede agregar menu bar/ciclo de vida sin retirar comportamiento oculto dentro de T002.

## Resultado

Completada el 2026-10-03.

- Shell Qt/QML por capas con 8 targets CMake y bundle `satcfdi_app.app`; prerrequisitos y comandos en `docs/development.md`.
- `SolicitudesService` y `PerfilesSatService` abstractos con `QFuture<Resultado<...>>`; implementaciones demo instanciadas solo en `AppCompositionRoot`.
- Puertos de `satcfdi_ports` sin metodos; modulo QML `SatCfdiDownloader` con lista, nueva solicitud y detalle.
- `ctest`: `satcfdi_tests` (unit) y `satcfdi_presentation_tests` (presentation) pasan; build sin warnings con `SATCFDI_WARNINGS_AS_ERRORS=ON`.
- Comprobacion manual en macOS real: ventana, teclado y redimensionado correctos.
- Limitaciones: compilado contra Qt 6.10.2 (APIs <= 6.8); bundle local requiere macOS 26+ con Qt de Homebrew; QtNetwork llega transitivo por `Qt6::Qml`.
- Para T003: conservar `crear()` como fachada de UI y descartar respuestas asincronas tardias en `NuevaSolicitudViewModel`.

## Riesgos y notas

- El shell no demuestra que la integracion SAT sea viable; eso corresponde a `T006`.
- El shell no completa el MVP y no debe confundirse con persistencia funcional.
- El menu bar se implementa despues en `T004` para mantener el incremento enfocado.
- Los contratos de puertos son provisionales donde el resultado de `T006` aun pueda cambiar la forma SAT.

## Referencias

- `docs/design/qt-project-structure.md`
- ADR 0011
- ADR 0012
- `T001-modelo-fisico-sqlite.md`
