# ADR 0003: Ejecutar UI, menu bar y worker en un solo proceso de app macOS

## Estado

Accepted

## Contexto

La aplicacion debe poder monitorear solicitudes y descargar paquetes aunque la ventana principal este cerrada u oculta. Tambien debe mostrar un icono en el menu bar y poder iniciar automaticamente con la sesion de macOS cuando el usuario lo habilite.

Para uso personal, un daemon independiente o servicio remoto agregaria complejidad innecesaria.

## Decision

Ejecutar UI, menu bar y worker local dentro de un solo proceso: `SAT CFDI Downloader.app`.

Reglas de ciclo de vida:

- Si el usuario abre manualmente la app, se muestra la ventana principal.
- Si la app inicia por Login Item, inicia solo con icono de menu bar y worker.
- Cerrar la ventana principal oculta la ventana, pero mantiene vivo el proceso.
- Salir requiere accion explicita desde menu bar o comando equivalente de macOS.
- El worker solo existe mientras el proceso de la app esta ejecutandose.

La integracion con macOS se modela por contrato `OSIntegration`, con `MacOSIntegration` como adaptador inicial.

## Consecuencias

- No hay daemon independiente.
- Si el usuario sale explicitamente de la app, el worker se detiene.
- El inicio automatico depende del mecanismo normal de macOS.
- `ADR 0009` decide la activacion macOS por contexto: foreground al abrir manualmente y background/menu bar al iniciar por Login Item.

## Referencias

- `docs/requirements.md`
- `docs/architecture.md`
