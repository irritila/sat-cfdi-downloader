# ADR 0002: Mantener el MVP como app personal local-first para macOS

## Estado

Accepted

## Contexto

El producto esta pensado para uso personal: un unico usuario local actuando como contador. No se esta disenando para clientes ficticios, clientes potenciales, SaaS, portal web multiusuario ni uso comercial.

El MVP necesita crear solicitudes de descarga masiva, monitorear su estado, descargar paquetes ZIP y mostrar detalle operativo local.

## Decision

Construir el MVP como una aplicacion desktop para macOS, local-first, sin backend remoto.

La aplicacion tendra:

- UI desktop macOS.
- Base de datos local.
- Carpeta local para paquetes ZIP.
- Worker local dentro del proceso de la app.
- Icono en menu bar.
- Inicio automatico opcional con la sesion de macOS.

No se agregaran usuarios, roles, organizaciones, permisos, backend remoto ni integraciones pensadas para escenarios comerciales que aun no existen.

## Consecuencias

- Se reduce la complejidad inicial.
- La seguridad se concentra en el equipo local y en la proteccion de e.firma.
- El usuario es responsable del respaldo local de base y paquetes.
- Una version comercial, multiusuario o web requeriria decisiones nuevas y probablemente otra arquitectura.

## Referencias

- `README.md`
- `docs/requirements.md`
- `docs/architecture.md`

