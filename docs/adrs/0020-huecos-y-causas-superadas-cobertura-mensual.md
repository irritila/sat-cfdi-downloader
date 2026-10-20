# ADR 0020: Huecos y causas superadas en la cobertura mensual

## Estado

Accepted

Modifica ADR 0019 en la evidencia de cobertura, el estado `Disponibles parcialmente`, la planeacion de `Completar mes` y `Actualizar mes`, y la vigencia de las causas de atencion.

## Contexto

Al implementar T023.2 se detectaron dos efectos de la lectura literal de ADR 0019:

- Si la unica cobertura acreditada de una direccion empieza despues del primer instante del mes, el mes se clasifica `Sin descargar` y la planeacion, que parte del fin de la cobertura contigua, volveria a pedir un rango ya cubierto, contradiciendo "nunca repite un rango cubierto".
- Una solicitud canonica fallida antigua mantiene el mes en `Requiere atencion` aunque solicitudes posteriores acrediten su mismo tramo, hasta que el usuario la elimine.

## Decision

### Huecos

- Por direccion se unen todos los tramos acreditados dentro del mes. La cobertura contigua desde el primer instante sigue siendo la medida de completitud ("disponibles hasta").
- La planeacion de la descarga guiada pide el complemento de esa union dentro de `[inicio del mes, frontera]`, en componentes maximos: una solicitud por cada hueco inicial, interno o final. Nunca incluye segundos ya acreditados.
- Una solicitud canonica en curso reserva su tramo: se reutiliza para el hueco que intersecta y no se crea otra solapada; los demas huecos libres se planifican.
- Con al menos un tramo acreditado en alguna direccion y sin trabajo ni error activos, el mes es `Disponibles parcialmente`, aunque ese tramo no empiece el dia 1. `Sin descargar` significa cero tramos acreditados en ambas direcciones. La precedencia de `Requiere atencion` y `Preparando` no cambia.
- `Completar mes` y `Actualizar mes` pueden crear una o mas solicitudes por direccion, una por hueco maximo.

### Causas superadas

- Una causa de una solicitud canonica deja de estar activa si solicitudes canonicas acreditadas posteriores (por `creada_en`) de la misma direccion cubren integramente su tramo. Se conserva en el historial y en el diagnostico, pero no fuerza `Requiere atencion`, no aparece en las causas activas ni bloquea la descarga guiada.
- Persisten todas las causas cuyo tramo no quede cubierto integramente por acreditacion posterior, incluidas `5002`, `5005` y errores de paquete, ZIP o indice. Una cobertura parcial, de otra direccion o anterior no supera la causa.

## Consecuencias

- T023.2 ajusta `evaluarCoberturaMensual` (union de tramos, huecos por direccion, causas activas frente a superadas) y sus pruebas.
- T023.3 planifica por huecos y prueba que el plan produce exclusivamente esos tramos.
- T023 D17 cambia "una sola descarga" por "una descarga por hueco".

## Referencias

- ADR 0019.
- `docs/meetings/T023.2-desarrollo/respuestas/03-arquitectura-riesgos.md`.
- `docs/tasks/T023-rediseno-ui-caso-de-uso.md`, T023.2 y T023.3.
