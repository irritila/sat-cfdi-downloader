# T024: Vocabulario y materiales del flujo centrado en facturas

## Estado

Completada (2026-10-27). Epica: se implementa en T024.1 a T024.4. Absorbe T021.

## Prioridad y tamano

Media. Grande: cuatro subtareas.

## Objetivo

Que la app, el manual, sus capturas y el video tutorial expliquen Archivo Fiscal desde las facturas por perfil y mes (T023), sin solicitudes, paquetes ni verificaciones fuera de Diagnostico.

## Contexto

T023 (D3, D4, D10) llevo el recorrido principal a Facturas por mes y dejo solicitudes, paquetes y verificaciones en `Ir > Diagnostico`; el barrido transversal quedo para T024. Hoy las notificaciones, el menu bar, Perfiles, e.firma, el visor PDF y el respaldo aun usan vocabulario del servicio web; el manual describe el flujo por solicitudes; la herramienta de capturas fotografia la UI vieja; y el guion del video esta roto desde T023 (T022 D6, D8). Sesion: `docs/meetings/T024-refinamiento/`.

## Decisiones

| ID | Decision | Estado | Fuente |
|---|---|---|---|
| D1 | T021 (manual y capturas al dia, defecto M5) se fusiona en T024. | Aprobada por usuario | 2026-10-26 (U1) |
| D2 | Video nuevo completo centrado en Facturas por mes, con voz nueva. Las llamadas de sintesis con costo las ejecuta el usuario (T017 D12), tras aprobar una muestra de 2 escenas. | Aprobada por usuario | 2026-10-26 (U2) |
| D3 | Glosario del recorrido normal: solicitud -> descarga; paquete -> archivo del SAT (o archivo descargado); verificacion -> preparando / consultando al SAT; Emitidos/Recibidos -> emitidas/recibidas. Diagnostico conserva el vocabulario tecnico. "Verificar" del respaldo no cambia. | Recomendacion tecnica | Ronda 01 |
| D4 | Notificaciones: una por direccion con textos nuevos ("Facturas descargadas", "No se pudieron descargar las facturas", "El SAT no acepto preparar las facturas", "Facturas no descargadas", "Sin facturas en el periodo"); "Solicitud terminada" con archivos no se notifica; las nuevas no llevan "Mostrar en Finder". Se mantienen claves de dedupe, la categoria y la ruta de Finder para las ya entregadas. Una notificacion por mes queda fuera. | Aprobada por usuario | 2026-10-26; ronda 01 (opcion A) |
| D5 | El menu bar quita "Abrir carpeta de paquetes" (sigue en Diagnostico). | Aprobada por usuario | 2026-10-26 |
| D6 | "Monitoreo" conserva su nombre. | Aprobada por usuario | 2026-10-26 |
| D7 | Video de 7 capitulos (perfil y e.firma con intro; descargar el mes; ver facturas y PDF; historial; exportar; respaldo; menu bar y notificaciones); tope ampliado a 8:00 (14400 cuadros), sustituye T017 D20 para este video. | Aprobada por usuario (tope); capitulos: recomendacion tecnica | 2026-10-26; ronda 02 |
| D8 | Solo en el manual (no en el video): reemplazo y vencimiento de e.firma, "Disponibles parcialmente", "Completar mes" y causas de atencion, Diagnostico, exportar varios meses o elegidas, cambio de contrasena y restaurar, atajos y mensajes. | Recomendacion tecnica | Ronda 02 |
| D9 | Capturas y video comparten un fake sintetico de Facturas (catalogo poblado y disponibilidad controlable), Historial, Exportacion y Respaldo; exportacion y respaldo del video se gobiernan por el guion. Ajuste en T024.2: `ExportacionFacturasViewModel` exige el `ExportacionFacturasService` concreto (con su hilo), asi que la exportacion usa el servicio real sobre puertos en memoria; el contenido es determinista y el momento de llegada se absorbe con la barrera `esperarExportacion`, que congela el reloj de cuadros y exige el estado terminal esperado (D16 se conserva). Respaldo si usa fakes sin hilos. Una interfaz para el servicio en `src/` queda como mejora fuera de T024. | Recomendacion tecnica | Rondas 01 y 02; T024.2 desarrollo |
| D10 | Dialogos nativos (FolderDialog, FileDialog, Imprimir, Vista Previa, portapapeles) nunca se pulsan con efecto en el video: `sinEfecto` + resultado inyectado + ilustracion con rotulo "Ilustracion" (T017 D14). | Recomendacion tecnica | Ronda 02 |
| D11 | Se borran `NuevaSolicitudPage`/`NuevaSolicitudViewModel` (sin ruta desde T023.5) y la guia duplicada de `SolicitudesPage`. | Recomendacion tecnica | Ronda 01; T023.5 |

## Subtareas

| Tarea | Resumen | Depende de |
|---|---|---|
| [T024.1](T024.1-vocabulario-y-notificaciones.md) | Vocabulario de la app (D3), notificaciones (D4), menu bar (D5), M5, limpieza D11, prueba de barrido de vocabulario, `docs/requirements.md` §7 | T023 |
| [T024.2](T024.2-fakes-sinteticos-materiales.md) | Fake sintetico comun (D9) y herramienta de capturas sobre la UI vigente | T024.1 |
| [T024.3](T024.3-manual-y-capturas.md) | Manual reescrito y capturas regeneradas (absorbe T021) | T024.1, T024.2 |
| [T024.4](T024.4-video-tutorial-nuevo.md) | Herramienta del video (D9, D10, atajos, ventana), guion de 7 capitulos, voz nueva, grabacion y manifiesto D16 | T024.1, T024.2 (integracion: T022, T025.6, T026.3) |

## Alcance

### Incluye

- Lo descrito en las subtareas.

### No incluye

- Una notificacion por mes (agrupar direcciones) ni cambiar como se calcula el icono de atencion del menu bar (se calcula por solicitudes; anotar como pregunta para core).
- Cambiar el formato del respaldo para contar facturas.
- La perdida de clics al cambiar de app durante la grabacion (T027): se mitiga grabando sin usar el equipo y con retomas por escena.

## Dependencias

- T023, T017, T022, T025, T026.

## Riesgos

- Grabacion larga sensible a T027: protocolo de grabacion en T024.4; la resistencia a cambios de app sigue abierta en T027.
- Pruebas que fijan textos (notificaciones, menu bar, Perfiles, respaldo, raiz de paquetes, quick tests).
- El guion se escribe despues de fijar los textos de T024.1; cambios posteriores obligan a resintetizar bloques de voz.

## Resultado

T024.1 (vocabulario y notificaciones), T024.2 (fakes sinteticos), T024.3 (manual y capturas) y T024.4 (video nuevo de 5:52 con voz `cedar`) completadas. Pendientes derivados: T027 (clic perdido al cambiar de app) y la diferencia de zona horaria entre Respaldo e Historial.

## Referencias

- `docs/tasks/T023-rediseno-ui-caso-de-uso.md`, `docs/tasks/T021-manual-capturas-al-dia.md`, `docs/tasks/T017-video-tutorial.md`, `docs/tasks/T022-intermitencia-pruebas-graficas-video.md`, `docs/meetings/T024-refinamiento/`.
