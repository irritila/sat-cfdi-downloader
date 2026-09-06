# ADR 0012: Organizar el proyecto Qt por capas y targets CMake

## Estado

Accepted

## Contexto

El MVP ya eligio Qt 6, QML / Qt Quick Controls, C++ y CMake como stack inicial. La arquitectura tambien separa presentacion, aplicacion, dominio, puertos e infraestructura.

Antes de inicializar el codigo, conviene fijar una estructura fisica que evite mezclar QML, reglas SAT, persistencia, archivos, secretos e integraciones macOS en los mismos modulos.

El proyecto sigue siendo personal y local-first. No necesita una estructura pensada para multiples productos, equipos, clientes o backend remoto.

## Decision

Organizar el codigo en un solo proyecto Qt/CMake con capas explicitas:

- `domain`: entidades, value objects, estados y reglas puras del MVP.
- `ports`: contratos C++ que separan aplicacion de SAT, secretos, sistema operativo, almacenamiento de paquetes y repositorios.
- `application`: casos de uso, worker, acciones manuales y coordinacion transaccional.
- `infrastructure`: adaptadores concretos para SQLite, SAT, archivos, macOS y secretos.
- `presentation`: QML, view models C++ y modelos expuestos a UI.
- `app`: punto de entrada, `QApplication`, composition root y configuracion del bundle.

El build se divide en targets CMake internos para mantener dependencias visibles. El ejecutable final `satcfdi_app` compone todos los modulos.

Reglas:

- QML no accede directamente a SQLite, SAT, archivos ni secretos.
- `domain` no depende de `application`, `infrastructure` ni `presentation`.
- `application` depende de `domain` y `ports`, pero no de adaptadores concretos.
- `infrastructure` implementa `ports`; no contiene flujos de UI.
- `presentation` llama casos de uso mediante view models; no implementa reglas SAT ni transiciones de estado.
- `app` es el unico lugar donde se conectan implementaciones concretas con servicios de aplicacion.
- El menu bar/system tray se implementa desde C++ con `QSystemTrayIcon`; se evita depender de tipos `Qt.labs` para una funcion central del MVP.

## Consecuencias

- La primera implementacion queda mas verbosa que un prototipo de un solo archivo, pero reduce acoplamiento temprano.
- Los tests pueden enfocarse en `domain` y `application` usando fakes de `ports`.
- La futura migracion a Windows puede reemplazar adaptadores sin cambiar casos de uso.
- Si el proyecto crece, se pueden separar targets internos sin mover conceptos de dominio.
- Para uso personal, no se introducen packages ni repositorios separados innecesarios.

## Referencias

- `docs/design/qt-project-structure.md`
- `docs/architecture.md`
- `docs/adrs/0011-qt-qml-application-stack.md`
- Qt CMake: https://doc.qt.io/qt-6/cmake-get-started.html
- Qt QML CMake: https://doc.qt.io/qt-6/cmake-build-qml-application.html
- Qt `qt_add_qml_module`: https://doc.qt.io/qt-6/qt-add-qml-module.html
- Qt `QSystemTrayIcon`: https://doc.qt.io/qt-6/qsystemtrayicon.html
