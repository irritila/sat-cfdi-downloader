# T019: Renombrar la aplicacion a "Archivo Fiscal"

## Estado

Pendiente (sin refinar).

## Objetivo

Que la aplicacion se llame "Archivo Fiscal" en todo lo visible y en su identidad de macOS, sin perder los datos ni las credenciales del usuario.

## Contexto

El usuario decidio el nombre durante T017 (D18): "SAT CFDI Downloader" suena raro. El guion del video ya usa el nombre nuevo, pero la app, el bundle, las ilustraciones de menu bar y las carpetas locales siguen con el anterior.

## Alcance

### Incluye

- Nombre visible en ventanas, menu bar, notificaciones, dialogos y manual.
- Bundle (nombre, identificador si se decide cambiarlo), carpeta de datos y paquetes, servicio del Keychain, con migracion de lo existente.
- Ilustraciones del video (menu bar, notificaciones) y regrabar el tutorial.

### No incluye

- Logo e icono (T020).

## Dependencias

- T017 completada. Coordinar con T011 (empaquetado y firma) y T020.

## Trabajo esperado

- Por definir en el refinamiento: decision sobre identificador de bundle, rutas y servicio de Keychain, y estrategia de migracion.

## Criterios de aceptacion

- [ ] Por definir en el refinamiento (incluir migracion de datos y credenciales existentes sin perdida).

## Verificacion

Por definir.

## Resultado

Pendiente.

## Referencias

- `docs/tasks/T017-video-tutorial.md` (D18), `docs/tasks/T020-logo.md`.
