# ADR 0019: Cobertura mensual guiada de CFDI

## Estado

Accepted

Modificado por ADR 0020: huecos de cobertura y causas superadas.

Enmendado por T023.6 (T023 D22, 2026-10-21): Rechazada con
`CodigoEstadoSolicitud` 5004 es un periodo sin facturas (ver "Cobertura y
estado mensual").

## Contexto

T023 cambia el recorrido principal de la aplicacion: el usuario consulta las
facturas de un perfil por mes y solicita obtener las faltantes, sin operar
directamente solicitudes, paquetes ni verificaciones SAT.

Para que este flujo no repita rangos ante SAT ni declare disponible un mes sin
respaldo local utilizable, se necesita una regla unica de cobertura, planeacion,
catalogo e historial. La regla debe permanecer fuera de QML, ser comprobable con
un reloj controlado y respetar los estados, eliminacion local y ejecutor serial
ya definidos.

## Decision

### Periodo, reloj y tramos

El flujo guiado opera exclusivamente sobre un mes natural y sobre las dos
direcciones: emitidos y recibidos. Fechas, meses, “hoy”, mes anterior y fechas
SAT se calculan con un reloj inyectable en `America/Mexico_City`. El instante se
captura al iniciar la accion. En enero, el mes inicial por omision es diciembre
del ano anterior.

Para el mes en curso:

- La primera descarga cubre desde el primer instante del mes hasta el instante
  de la accion.
- `Actualizar mes` cubre desde el segundo siguiente a la cobertura contigua
  hasta el instante de la accion.
- `Actualizar mes` solo se ofrece cuando la cobertura termina en un dia anterior
  a hoy. La UI comunica “Facturas disponibles hasta el <dia> de <mes>, <hora>”.

Para un mes cerrado con cobertura incompleta, `Completar mes` crea una sola
descarga desde el segundo siguiente a la cobertura contigua hasta el fin del
mes. Nunca repite un rango cubierto. Un mes cerrado completamente cubierto no
se actualiza.

### Cobertura y estado mensual

La cobertura se calcula por direccion. Solo acredita una solicitud canonica:
mismo perfil, direccion, `CFDI` y `Vigente`, sin filtros adicionales, con rango
dentro del mes, no eliminada y terminada con todos sus paquetes descargados,
ZIP presente y legible e indice completo; tambien acredita una solicitud
terminada cuyo conjunto vacio de paquetes haya sido persistido desde la
verificacion SAT.

Enmienda D22 (T023.6): una solicitud canonica con `EstadoSolicitud` 5
(`Rechazada`) y `CodigoEstadoSolicitud` 5004 ("la solicitud de descarga no
genero paquetes por falta de informacion", `docs/web-service.md` 6.2) acredita
su tramo como conjunto vacio, igual que una terminada sin paquetes confirmada,
y no es causa de atencion. Se deduce del par estado/codigo persistido, sin
migracion ni marca de conjunto vacio, de modo que las filas ya guardadas se
corrigen solas. Las demas causas de la solicitud (`5002`, `5005`, paquetes) se
conservan. `Rechazada` con otro codigo, o sin codigo, sigue siendo causa de
atencion. El `CodEstatus` 5004 de la operacion de verificacion (solicitud no
encontrada) es otro campo y sigue siendo falla (ADR 0018).

La cobertura es la union contigua de tramos desde el primer instante del mes.
Un tramo posterior a un hueco no la extiende. La frontera objetivo es el fin
del mes para meses cerrados, o cualquier instante de hoy para el mes en curso;
una cobertura que termina en un dia anterior a hoy no alcanza esa frontera.

El mes tiene exactamente estos estados, evaluados con esta precedencia:

1. `Requiere atencion`: `EnvioIncierto`, `EnvioFallido`, `ErrorSat`,
   `Rechazada` (salvo `CodigoEstadoSolicitud` 5004, enmienda D22), `Vencida`, codigos `5002` o `5005`, paquete `Error` o
   `Vencido`, ZIP faltante o ilegible, indice ilegible u origen no disponible,
   o indexacion pendiente sin trabajo programado.
2. `Preparando`: solicitud `Creada`, `Enviando`, `Enviada`, `Aceptada` o
   `EnProceso`, paquete descargandose o indexacion programada.
3. `Disponibles`: ambas direcciones cubren la frontera.
4. `Disponibles parcialmente`: existe alguna cobertura en al menos una
   direccion, sin trabajo ni error.
5. `Sin descargar`: no existe cobertura.

Las solicitudes con filtros o rangos no canonicos pueden aportar CFDI al
catalogo, pero no acreditan cobertura mensual.

### Planeacion de descarga guiada

La descarga guiada planifica ambas direcciones en una transaccion antes de
crear solicitudes. Por cada direccion:

1. Si la cobertura alcanza la frontera, no hace nada.
2. Si existe una solicitud canonica en curso, la reutiliza.
3. Si falta un tramo, lo calcula desde el fin de la cobertura contigua y solo
   entonces aplica `evaluarDuplicado`.

`RequiereConfirmacion` pide confirmacion explicita una vez por direccion;
cancelarla no afecta la otra direccion. `Bloqueado` reutiliza la solicitud
existente. La repeticion de la accion mientras exista trabajo en curso no crea
solicitudes nuevas.

Ante `5002` en una direccion, la otra se crea si esta libre; el mes queda en
`Requiere atencion` y no se ofrece recrear la direccion afectada. Ante `5005`,
la UI conduce al diagnostico. Solo una accion explicita del usuario crea
solicitudes.

### Catalogo e historial

El catalogo deduplica CFDI por UUID no vacio normalizado a mayusculas. La fuente
canonica se elige, en orden, entre solicitudes canonicas guiadas, la indexacion
mas antigua y el menor identificador de paquete. Si una fuente se elimina, se
elige la siguiente con el mismo orden. CFDI sin UUID no se deduplican. CFDI de
solicitudes o paquetes eliminados no aparecen ni cuentan.

Se persiste un lote de descarga guiada con el perfil identificado por
`perfil_sat_id`, mes, fecha y solicitudes vinculadas por direccion y tramo. El
historial se deriva de esos lotes. Si se elimina una solicitud vinculada, el
lote conserva mes y fecha, recalcula su resultado sin ella e indica que parte
de la descarga fue eliminada. Los lotes de un perfil eliminado se ocultan con
ese perfil y no se reasignan a uno recreado con el mismo RFC. Las solicitudes
anteriores a T023 aparecen como `Descarga anterior`, solo con diagnostico.

La migracion y el esquema fisico de este lote se definen en T023.2; este ADR no
fija nombres de tablas ni columnas definitivos.

### Frontera de capas

Las reglas de cobertura, clasificacion, planeacion, deduplicacion de catalogo y
lotes pertenecen a `domain` y `application`. La presentacion recibe DTOs con el
estado, cobertura, acciones disponibles, mensajes y destinos de navegacion.
QML solo representa esos DTOs e invoca comandos; no calcula cobertura, evalua
duplicados, inspecciona ZIPs, interpreta estados SAT ni accede a persistencia.

Esta decision no modifica ADR 0013: conserva sus operaciones SAT separadas para
emitidos y recibidos. No modifica ADR 0015: mantiene sus estados separados,
`dedup_key` y eliminacion logica. No modifica ADR 0018: las solicitudes creadas
por este flujo se encolan y ejecutan mediante su frontera `OperacionesSat` y
ejecutor serial.

## Consecuencias

- T023.2 implementara los DTOs, consultas, modelo persistido del lote y pruebas
  deterministas con reloj inyectable.
- T023.3 implementara la planeacion transaccional y las confirmaciones por
  direccion sin alterar el worker.
- T023.4 y T023.5 consumiran DTOs y conservaran el diagnostico como acceso
  secundario.
- Las pruebas deben cubrir huecos, tramos contiguos, ambos tipos de mes,
  precedencia de estados, eliminaciones, recreacion de perfil y seleccion de
  fuente canonica.

## Referencias

- `docs/tasks/T023-rediseno-ui-caso-de-uso.md` (D7, D11-D14, D17 y D19)
- `docs/tasks/T023.1-prototipo-y-adr-cobertura.md`
- ADR 0013, ADR 0015 y ADR 0018
- `docs/design/operational-rules.md`
