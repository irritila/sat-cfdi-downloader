# T002: Shell Qt/QML ejecutable

## Estado

Pendiente

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
- Pruebas basicas de compilacion y carga de QML.

### No incluye

- Persistencia SQLite real.
- Repositorios productivos.
- Credenciales SAT o Keychain.
- Llamadas SAT, worker o ejecutor serial productivo.
- Menu bar, autostart y notificaciones; pertenecen a `T004`.
- Formularios completos de perfiles y credenciales.

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
7. Agregar una prueba automatizada minima para confirmar que el proyecto compila y registra sus tests.

## Restricciones tecnicas

- QML solo maneja layout, navegacion y estado visual.
- Los view models C++ exponen propiedades, comandos y modelos a QML.
- Ningun archivo QML importa repositorios, SQL, SAT, filesystem o Keychain.
- El composition root es el unico punto que conecta implementaciones concretas.
- El shell debe poder reemplazar los datos fake por repositorios sin redisenar las vistas.
- Los contratos iniciales son fronteras compilables; los DTOs SAT pueden ajustarse despues de `T006` sin quebrar los targets.

## Criterios de aceptacion

- [ ] `cmake -S . -B build` configura el proyecto correctamente.
- [ ] `cmake --build build` genera `satcfdi_app` como bundle macOS.
- [ ] `ctest --test-dir build` se ejecuta sin fallos.
- [ ] La app abre una ventana principal QML sin errores de carga.
- [ ] La ventana muestra una lista de solicitudes simuladas.
- [ ] Se puede navegar de la lista a nueva solicitud y al detalle.
- [ ] El detalle muestra metadata simulada de la solicitud seleccionada.
- [ ] QML no accede directamente a SQLite, archivos, secretos ni SAT.
- [ ] La estructura de targets y carpetas coincide con `qt-project-structure.md`.
- [ ] Existen contratos compilables en `satcfdi_ports` para SAT, secretos, sistema operativo, paquetes, logs y repositorios.
- [ ] Los contratos no contienen detalles SOAP inventados ni implementaciones concretas.
- [ ] No se introducen tablas, migraciones ni dependencias de integracion SAT en esta tarea.

## Verificacion

1. Configurar y compilar con CMake.
2. Ejecutar `ctest`.
3. Abrir el `.app` generado.
4. Recorrer lista, nueva solicitud y detalle.
5. Revisar imports QML y dependencias de los targets para confirmar las fronteras de capa.

## Definicion de terminado

- El bundle abre en macOS desde el build local.
- Las tres vistas son navegables con datos fake.
- El puente C++/QML esta funcionando.
- Las pruebas y comandos de compilacion documentados pasan.
- `T003` puede reemplazar los datos fake por persistencia sin modificar la estructura de navegacion.

## Resultado

Pendiente.

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
