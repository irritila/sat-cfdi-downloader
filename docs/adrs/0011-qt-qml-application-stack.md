# ADR 0011: Usar Qt y QML como stack de aplicacion

## Estado

Accepted

## Contexto

El MVP es una app desktop personal para macOS, con posible migracion futura a Windows. Necesita ventana principal, menu bar/system tray, cierre a segundo plano, inicio automatico, notificaciones, base local, carpeta de ZIPs y worker local.

Se evaluaron dos rutas principales:

- Qt + QML / Qt Quick.
- Go como lenguaje principal.

Go es fuerte para servicios locales, concurrencia, red y archivos, pero no trae una UI desktop oficial. En la practica obligaria a elegir otro framework encima, como Fyne o Wails.

Qt cubre de forma directa la superficie principal del MVP: UI desktop, QML, integracion C++/QML, system tray/menu bar, SQLite, red, timers, threads y distribucion desktop.

## Decision

Usar Qt como stack principal del MVP.

Stack inicial:

- Framework: Qt 6.
- UI: QML con Qt Quick Controls.
- Logica de aplicacion, dominio y adaptadores: C++ con Qt.
- Build: CMake.
- Base local: SQLite detras de repositorios. La implementacion inicial puede usar Qt SQL con driver SQLite.
- Worker local: `QTimer`, threads o tareas Qt, manteniendo las reglas definidas en `ADR 0007`.
- Menu bar/system tray: contrato `OSIntegration`, implementado inicialmente con Qt y adaptadores macOS cuando haga falta.
- Integraciones macOS especificas: adaptadores concretos para Login Item, notificaciones y Keychain, detras de `OSIntegration` y `SecretStore`.

No se usara Go en el MVP. Si en el futuro se justifica separar un motor de sincronizacion o CLI, se evaluara con un ADR nuevo.

## Consecuencias

- El proyecto mantiene un solo runtime principal.
- La UI queda declarativa y separada de la logica C++ mediante contratos/adaptadores.
- La futura migracion a Windows sigue siendo viable porque Qt soporta ambos entornos.
- La integracion SAT con SOAP, WS-Security y firma XML queda como riesgo tecnico a validar temprano. Si Qt no cubre suficiente para la firma XML requerida por SAT, se agregara una dependencia especifica mediante ADR o spike tecnico.
- La implementacion debe evitar que QML contenga reglas SAT, reglas de estado o acceso directo a persistencia.
- El diseno existente de puertos/adaptadores se conserva: cambiar Qt SQL, Keychain o integraciones del SO no debe cambiar los casos de uso.

## Referencias

- `docs/requirements.md`
- `docs/architecture.md`
- Qt Quick Controls: https://doc.qt.io/qt-6/qtquickcontrols-gettingstarted.html
- Qt QML y C++: https://doc.qt.io/qt-6/qtqml-cppintegration-overview.html
- Qt System Tray: https://doc.qt.io/qt-6/qsystemtrayicon.html
- Qt SQL SQLite: https://doc.qt.io/qt-6/sql-driver.html
