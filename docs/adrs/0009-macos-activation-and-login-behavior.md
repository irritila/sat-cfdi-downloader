# ADR 0009: Usar activacion macOS por contexto

## Estado

Accepted

## Contexto

La app necesita dos comportamientos macOS que parecen opuestos:

- Si inicia automaticamente con la sesion del usuario, debe aparecer solo como icono de menu bar y mantener el worker activo.
- Si el usuario abre la app manualmente, debe mostrar la ventana principal aunque el proceso ya exista.

Apple distingue entre apps regulares, que aparecen en Dock y pueden tener UI completa, y apps agente (`LSUIElement`), que no aparecen en Dock pero pueden presentar UI. AppKit tambien permite modificar la politica de activacion en runtime.

Para este MVP personal no conviene introducir un helper, daemon o LaunchAgent propio. La app debe seguir siendo un solo proceso local.

## Decision

Usar un solo bundle `SAT CFDI Downloader.app` con activacion por contexto, encapsulada detras de `OSIntegration`.

Reglas:

- En apertura manual, la app entra en modo foreground y muestra la ventana principal.
- En inicio automatico por Login Item, la app entra en modo background: crea el icono de menu bar, inicializa el worker y no abre la ventana principal.
- Cerrar la ventana oculta la UI y conserva el icono de menu bar y el worker.
- Salir requiere accion explicita desde menu bar o comando equivalente de macOS.
- El MVP no usa un daemon, LaunchAgent independiente ni helper separado.
- El MVP no se modela como una app `LSUIElement` permanente. Si la implementacion necesita ocultar Dock/App Switcher en el arranque automatico, debe hacerlo como detalle de activacion runtime dentro de `MacOSIntegration`.
- El Login Item se configura mediante la integracion normal de macOS. `OSIntegration` debe representar estados como habilitado, pendiente de aprobacion, rechazado o no disponible.
- Si el usuario rechaza notificaciones, el flujo principal sigue funcionando; la UI/menu bar debe poder mostrar que las notificaciones estan deshabilitadas.

## Consecuencias

- Se conserva una experiencia de app desktop cuando el usuario la abre manualmente.
- El inicio automatico cumple el objetivo de monitoreo silencioso para uso personal.
- `OSIntegration` absorbe detalles propios de macOS como `SMAppService`, politica de activacion y permisos de notificaciones.
- La futura migracion a Windows puede implementar el mismo contrato sin cambiar los servicios de aplicacion.
- La implementacion debe probar explicitamente los escenarios de apertura manual, inicio automatico, cierre de ventana, re-apertura desde menu bar y salida explicita.

## Referencias

- `docs/requirements.md`
- `docs/architecture.md`
- Apple: `LSUIElement`: https://developer.apple.com/documentation/bundleresources/information-property-list/lsuielement
- Apple: `NSApplication.ActivationPolicy`: https://developer.apple.com/documentation/appkit/nsapplication/activationpolicy-swift.enum/regular
- Apple: `setActivationPolicy(_:)`: https://developer.apple.com/documentation/appkit/nsapplication/setactivationpolicy%28_%3A%29
- Apple: `SMAppService`: https://developer.apple.com/documentation/servicemanagement/smappservice
