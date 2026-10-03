# Recuperacion de bridge y proveedores

Usa esta guia cuando el coordinador no puede obtener respuesta de un agente por
un bridge hacia Claude, Codex u otro proveedor. Su objetivo es recuperar la
sesion sin inventar respuestas, reenviar trabajo indefinidamente ni perder la
trazabilidad.

## Antes de asignar trabajo

1. Registra en `bitacora.md` el proveedor, modelo, rol, bridge y configuracion
   de sesion que el usuario haya elegido. Nunca registres tokens, API keys,
   cookies ni credenciales.
2. Comprueba cada bridge seleccionado con una solicitud mínima, por ejemplo
   `pong`. La prueba confirma transporte y autenticacion basica; no confirma
   que una tarea larga vaya a terminar.
3. Si la prueba falla, no asignes trabajo dependiente a ese proveedor. Registra
   el fallo sanitizado y sigue con especialistas o cortes independientes que ya
   esten disponibles.

## Diagnostico acotado

Clasifica la incidencia antes de recuperar:

| Señal | Tratamiento |
| --- | --- |
| Proceso o bridge inactivo | Comprueba el comando, ruta y estado documentados por el repositorio. Puede reiniciarse una vez si el usuario autorizó usar ese bridge. Repite `pong`. |
| Autenticación, cuenta, cuota o modelo no disponible | Registra proveedor y categoría; no cambies credenciales, modelo ni plan de cuenta. Pide al usuario resolverlo o elegir una sustitución. |
| Timeout o conexión transitoria | Revisa el evento sanitizado y reintenta una sola vez con el mismo rol, modelo y una instrucción más acotada. Conserva el identificador de sesión si el bridge lo soporta. |
| Error de entrada, formato o configuración local | Corrige únicamente el adaptador o payload afectado, registra el cambio y ejecuta una nueva comprobación mínima antes de reenviar el trabajo. |
| Respuesta vacía o truncada | Trátala como no recibida. Solicita continuación en la misma sesión una vez; no completes ni atribuyas la parte ausente al proveedor. |

No envíes más de un reintento de recuperación por trabajo sin nueva evidencia.
Si la misma causa reaparece, marca la intervención como bloqueada y evita
consumir más contexto o cuota.

## Continuidad sin una respuesta

- No atribuyas al proveedor una conclusión que no emitió.
- Mantén el trabajo independiente en marcha con los agentes disponibles.
- Si el coordinador plantea una alternativa propia, identifícala como propuesta
  del coordinador, no como revisión del especialista ausente.
- Sustituye un proveedor, modelo o especialista solo si el usuario lo eligió o
  si existe una equivalencia aprobada y registrada para esa sesión. Registra
  alcance, motivo y limitación de la sustitución.
- No cierres como revisado un criterio que dependía de la intervención ausente.
  Déjalo como pendiente, no verificado o bloqueado en `evidencia.md`.

## Registro mínimo

Para cada incidente anota en `bitacora.md`:

```text
<fecha UTC> - Coordinador -> bridge/<proveedor>/<rol>
Estado: timeout | autenticacion | proceso inactivo | respuesta vacia | configuracion
Evidencia sanitizada: <exit code, señal o categoría>
Accion: healthcheck | un reintento | trabajo independiente | espera de usuario
Resultado: recuperado | bloqueado | sustituido por <rol/modelo autorizado>
```

El log de bridge puede conservar la salida necesaria para diagnóstico, siempre
que no incluya secretos. `bitacora.md` debe contener una síntesis sanitizada y
la decisión de coordinación.
