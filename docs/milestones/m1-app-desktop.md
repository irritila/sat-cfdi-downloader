# M1 - App escritorio distribuible

Estado: borrador para implementacion.

## Objetivo

Construir una primera version local de `SAT CFDI Downloader.app` usando Qt 6, QML / Qt Quick Controls y C++, alineada con el MVP personal definido en `docs/requirements.md`.

Este milestone no agrega alcance comercial, multiusuario ni backend remoto. La prioridad es dejar una app macOS ejecutable, mantenible y preparada para conectar el servicio SAT real.

## Alcance

- Proyecto Qt 6 con CMake.
- Ventana principal QML con navegacion base.
- Icono de menu bar/system tray.
- Ciclo de vida macOS: apertura manual con ventana, cierre a menu bar y salida explicita.
- Base local SQLite con migracion inicial.
- Repositorios para perfiles, solicitudes, paquetes, logs y configuracion.
- Carpeta local fija para paquetes ZIP.
- Worker local con pausa persistente, intervalo 10/30 min y estados definidos.
- Contratos internos iniciales: `SatGateway`, `SecretStore`, `OSIntegration`, `PackageStorage`, repositorios y `LogSanitizer`.
- Adaptadores iniciales con implementaciones reales o stubs controlados segun riesgo.

## Fuera de alcance de M1

- Extraccion, parseo o indexacion de XML.
- Solicitudes de metadata.
- Consulta de vigencia/cancelacion CFDI.
- Exportaciones contables.
- Backend remoto.
- Multiusuario, roles, clientes u organizaciones.
- Windows. La arquitectura queda preparada, pero el build inicial es macOS.

## Orden sugerido

1. Inicializar proyecto Qt 6 con CMake.
2. Crear estructura de modulos: `presentation`, `application`, `domain`, `ports`, `infrastructure`.
3. Implementar shell QML: lista de solicitudes, nueva solicitud, detalle y perfiles SAT con datos mock.
4. Implementar ciclo de vida de ventana y menu bar/system tray.
5. Crear migracion SQLite inicial y repositorios.
6. Implementar `ConfiguracionApp`, pausa persistente y autostart apagado por defecto.
7. Implementar worker local contra un `SatGateway` fake para validar estados sin tocar SAT.
8. Implementar carpeta local de paquetes ZIP y deteccion de existencia de archivo.
9. Implementar `LogSanitizer` y logs operativos basicos.
10. Hacer spike tecnico de autenticacion/firma SAT antes de conectar `SatGateway` real.
11. Implementar `MacOSSecretStore` con Keychain para e.firma.
12. Empaquetar `.app` local para prueba manual.

## Criterios de aceptacion

- La app compila y abre en macOS.
- Abrir manualmente muestra la ventana principal.
- Cerrar la ventana oculta la app y mantiene el proceso vivo desde menu bar.
- Salir desde menu bar termina el proceso.
- La app persiste configuracion local en SQLite.
- La app permite crear/listar solicitudes locales usando datos fake.
- El worker fake cambia estados y registra logs sin depender de SAT real.
- Los paquetes fake quedan registrados como `PaqueteSolicitud` y apuntan a una ruta local esperada.
- QML no accede directamente a SQLite, archivos, secretos ni SAT.
- Las decisiones relevantes quedan trazables a ADRs.

## Riesgos tecnicos tempranos

- Firma XML / WS-Security requerida por SAT.
- Integracion Keychain para importar y proteger e.firma.
- Login Item moderno de macOS y permisos del sistema.
- Notificaciones nativas si el usuario las rechaza.
- Empaquetado `.app` con Qt y recursos QML.

## Referencias

- `docs/requirements.md`
- `docs/architecture.md`
- `docs/adrs/0011-qt-qml-application-stack.md`
- `docs/adrs/0012-qt-layered-project-structure.md`
- `docs/design/qt-project-structure.md`
