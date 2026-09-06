# ADR 0006: Proteger e.firma mediante contrato de secretos y logs sanitizados

## Estado

Accepted

## Contexto

La aplicacion necesita operar con e.firma: certificado, llave privada y contrasena. Ese material es sensible y no debe quedar expuesto en base local, logs ni archivos comunes.

Tambien pueden existir tokens SAT temporales y payloads SOAP con informacion sensible.

## Decision

Usar un contrato `SecretStore` como frontera de secretos.

Reglas:

- Al registrar e.firma, la app importa/copia `.cer` y `.key` al almacenamiento controlado por la aplicacion.
- `CredencialSat` guarda solo referencias no secretas.
- No se guardan contrasenas, llaves privadas ni tokens en texto plano.
- El material de firma se entrega a servicios solo para la operacion actual.
- El token SAT puede vivir solo en memoria. Cualquier persistencia futura de token requiere un ADR nuevo.
- Antes de persistir payloads SAT en `LogSolicitud`, deben pasar por `LogSanitizer`.

`ADR 0010` concreta la implementacion inicial del MVP: Keychain de macOS para secretos y token SAT solo en memoria.

## Consecuencias

- Los servicios dependen de contrato, no de una tecnologia concreta.
- La futura migracion a Windows puede implementar el mismo contrato con otro adaptador.
- La decision concreta de macOS queda separada del contrato para no bloquear una futura migracion a Windows.

## Referencias

- `docs/requirements.md`
- `docs/architecture.md`
