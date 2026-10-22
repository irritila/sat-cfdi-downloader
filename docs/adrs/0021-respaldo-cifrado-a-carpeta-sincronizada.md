# ADR 0021: Respaldo cifrado a carpeta sincronizada del usuario

## Estado

Accepted

Enmienda acotada a ADR 0004: agrega una copia cifrada de la informacion local fuera de las tres fronteras de persistencia, sin cambiarlas. No reemplaza ADR 0002: un backend propio sigue fuera de alcance.

## Contexto

Toda la informacion de la app vive en el equipo (ADR 0002, ADR 0004). T025 pide respaldarla en la nube del propio usuario (iCloud Drive, Dropbox, Google Drive, OneDrive u otro) y recuperarla si pierde o cambia de equipo. La base y los ZIP contienen RFC, nombres, montos, UUID y XML. La e.firma esta protegida por ADR 0006 y ADR 0010 y no debe salir del equipo.

Las alternativas evaluadas en el refinamiento de T025 fueron CloudKit (exige el programa de pago y el entitlement de iCloud), las APIs de cada proveedor (registro de la app, OAuth, tokens y revision por proveedor) y una carpeta que el cliente de escritorio del proveedor ya sincroniza. Para el formato se evaluaron un ZIP o tar dentro de un flujo cifrado y un contenedor propio. Un ZIP necesita volver al principio para el directorio central (texto plano en disco o un adaptador de E/S) y un tar no aporta integridad por entrada.

## Decision

### Mecanismo (T025 D1)

- La app escribe un archivo cifrado por respaldo en una carpeta que el usuario elige con el dialogo nativo, normalmente dentro de la carpeta sincronizada de su proveedor. Lo sube el cliente del proveedor. La app no habla con ninguna API de nube, no usa CloudKit ni OAuth y no guarda tokens de terceros.
- La UI dice "copiado a la carpeta", nunca "subido a la nube".
- Esto no es un backend del producto: no hay servidor propio, cuenta de la app ni sincronizacion entre usuarios o equipos. La app sigue funcionando sin red (RNF-002).

### Contenido (D3, D7)

- Lista blanca: copia consistente de SQLite, ZIP de paquetes `Descargado` y preferencias sin rutas. Nunca `credentials/`, Keychain, la contrasena SAT, logs de aplicacion ni temporales.
- La e.firma no sale del equipo; al restaurar se vuelve a registrar.
- Como defensa en profundidad, el contenedor rechaza toda ruta cuyo primer componente sea `credentials`.

### Contenedor AFB1 (D2, D6, D9, D12, D16, D17)

Formato propio en streaming, implementado con OpenSSL >= 3.2 en `infrastructure/crypto/ContenedorRespaldoOpenSsl` detras del puerto `ports/respaldo/ContenedorRespaldo.h`. El detalle binario esta en `ContenedorRespaldoFormato.h`.

- **Cabecera en claro, versionada** (204 bytes): magic `AFB1`, version, suite, id de instalacion (D17), id aleatorio del respaldo, fecha UTC en ms, parametros Argon2id y su sal, tamano de bloque, prefijo de nonce y las dos envolturas de la DEK.
  - Se puede leer sin clave, para la retencion y para derivar la KEK.
  - Solo queda autenticada completa (los 204 bytes, con ambas envolturas) al desenvolver la DEK y autenticar el bloque 0, cuya AAD incluye la cabecera. Es lo que hace `autenticarCabecera`.
  - Desenvolver la DEK por si solo autentica `cabecera[0..84)` y la envoltura de su rol.
- **Claves**:
  - KEK de contrasena: `Argon2id(contrasena, sal, m=64 MiB, t=3, p=1)`. La sal mide 16 B y es fija por contrasena configurada, para que T025.4 guarde la KEK en Keychain y el respaldo diario no pida la contrasena.
  - Al leer una cabecera se rechazan parametros por debajo de esos minimos o por encima de techos anti-DoS: 256 MiB, t=8, p=4. La derivacion nunca corre en el hilo grafico.
  - KEK de recuperacion: `HKDF-SHA256` de una clave de recuperacion de 32 B aleatorios. Se muestra como 56 caracteres Crockford Base32 en 14 grupos de 4, con 3 bytes de control para detectar errores de captura.
  - DEK aleatoria de 32 B por respaldo, envuelta con AES-256-GCM por cada KEK. La AAD de cada envoltura es `cabecera[0..84)` mas el rol.
  - Cambiar la contrasena conserva la clave de recuperacion (D16): las copias viejas abren con la contrasena vieja o con la clave, y las nuevas con la contrasena nueva o con la clave.
- **Bloques (marco explicito)**:
  - Flujo logico de registros (metadatos, entradas, faltantes, manifiesto) en bloques con AES-256-GCM bajo la DEK.
  - Cada bloque en disco es `largo u32 | final u8 | ciphertext | tag`. Un bloque no final mide exactamente 1 MiB; el final mide de 1 B a 1 MiB, es el unico con `final=1` y tras el debe venir el fin de archivo real.
  - Nonce: el prefijo aleatorio de 4 B mas el indice u64 del bloque, unico porque la DEK es nueva en cada respaldo.
  - AAD: cabecera completa, indice, largo y marca de final. Asi un bloque alterado, reordenado o repetido, un marco cambiado o un archivo truncado no autentican.
- **Lectura de dispositivos**: el lector nunca decide el final con `atEnd()`. Completa lecturas parciales o en cero con `waitForReadyRead()` y solo acepta el fin cuando `read()` devuelve 0, `waitForReadyRead()` falla y no quedan bytes. Un dispositivo secuencial que no implementa la espera falla cerrado (contenedor truncado), nunca acepta un contenedor incompleto.
- **Entradas**: ruta relativa, tamano, datos y SHA-256 justo despues de los datos. El manifiesto final repite ruta, estado (presente o faltante, D11), tamano y SHA-256 de cada entrada, y debe coincidir byte a byte con lo visto.
- **Limites (C2)**, que escritor y lector aplican por igual con `LimiteExcedido`:
  - 100 000 entradas por respaldo, incluidos los faltantes.
  - 16 MiB de filas de manifiesto.
  - 200 000 carpetas distintas.
  - Con rutas reales (`paquetes/<RFC>/<yyyy-mm>/<UUID>/<id>.zip`, unos 95 B) caben las 100 000 entradas.
- **Rutas**:
  - Relativas, componentes no vacios, sin `/` inicial ni final, sin `//` y sin componentes que empiecen por `.` o `~` (cubre `.` y `..`).
  - Sin `\`, `:`, NUL ni controles. UTF-8 valido en NFC, de hasta 1024 bytes y 255 por componente, como en ADR 0004.
  - Se rechazan duplicados y choques entre archivo y carpeta sin distinguir mayusculas.
  - Solo existe la clase "archivo regular": un registro de enlace u otra clase se rechaza.
- **Escritor (C3)**:
  - Cada fuente llega como raiz absoluta mas ruta relativa y se abre componente a componente: la raiz con `O_DIRECTORY|O_NOFOLLOW`, cada intermedio con `openat(O_DIRECTORY|O_NOFOLLOW)` y el archivo con `openat(O_NOFOLLOW|O_NONBLOCK)`, exigiendo archivo regular.
  - Un enlace en la raiz, en un ancestro o en el archivo nunca se sigue. Un FIFO o un dispositivo tampoco se leen.
  - Si el tamano, el mtime o el inodo cambian durante la lectura (comprobado con `fstatat` sobre el padre), o la fuente desaparece, el respaldo falla (D11).
  - Los errores previos a escribir (ruta, fuente, limite) no invalidan el escritor. Cualquier error posterior lo deja inutilizable y el destino se descarta.
- **Lector**:
  - `verificar()` recorre y autentica todo sin escribir.
  - `extraer()` hace lo mismo y escribe en un staging privado validado por descriptor: abierto con `O_DIRECTORY|O_NOFOLLOW`; `fstat` con dueno igual al euid y modo exacto 0700; vacio al enumerarlo sobre el descriptor.
  - Escribe con `openat`, `O_NOFOLLOW` y `O_EXCL`, carpetas 0700 y archivos 0600, y comprueba el SHA-256 de cada archivo.
  - Las entradas se escriben antes de autenticar el manifiesto y el fin. El staging no es confiable mientras `extraer()` no devuelva exito, y T025.5 nunca lo promueve sin ese exito.
  - Ante cualquier error lo vacia recorriendo cada carpeta por su descriptor y borrando con `unlinkat` sobre el descriptor del padre.
  - Las entradas leidas se entregan una a una (`alLeerEntrada`) y solo son validas si la llamada termina con exito.
- **Errores**: categorias tipadas con diagnosticos ASCII estables, sin rutas, nombres, RFC, contrasenas ni contenido. Una contrasena o clave incorrecta, o una cabecera alterada (indistinguible sin la clave), da siempre `CredencialIncorrecta`.
- **Memoria (C2)**: buffers de unos 2.5 MiB (dos bloques de 1 MiB y un chunk de 256 KiB), mas el manifiesto en memoria (hasta 16 MiB) y el indice de rutas (unos 24-32 B por archivo o carpeta, hasta unos 9 MiB). La cota del pipeline queda en unos 28 MiB, sin contar Argon2id.
  - Medido con malloc en uso, despues del KDF: un archivo de 3 GiB anade 3.3 MiB al escribir y 2.3 MiB al leer; 100 000 entradas reales anaden 22.6 MiB.
  - Argon2id reserva 64 MiB durante la derivacion. El allocator de macOS retiene esa memoria ya liberada en la huella del proceso.
  - Los buffers de texto plano y las claves se limpian con `OPENSSL_cleanse` o `BufferSecreto`.
- **OpenSSL**: CMake exige `OpenSSL 3.2` y cada derivacion comprueba `EVP_KDF_fetch("ARGON2ID")`. La app expone `comprobarDisponible()` para el arranque (D12). La distribucion debe empaquetar libcrypto >= 3.2.

### KEK guardada y clave de recuperacion

- La KEK de contrasena que T025.4 guarda en Keychain (ThisDeviceOnly, no sincronizable) abre todos los respaldos hechos con esa contrasena y su sal, pasados y futuros, sin pedir la contrasena. Quien obtenga esa KEK tiene el mismo alcance que quien conoce la contrasena. Por eso nunca sale del equipo ni se escribe en logs.
- La app no guarda la clave de recuperacion: solo su KEK derivada, y solo si T025.4 lo requiere. T025.6 la muestra una vez para imprimirla o copiarla y no persiste su forma formateada (ni en preferencias, ni en logs, ni en el portapapeles mas alla de la accion explicita del usuario).

### Responsabilidades fuera del contenedor

- T025.2: captura consistente y lista de entradas.
- T025.3: `.partial`, `F_FULLFSYNC`, rename exclusivo y retencion de 5 (D17). La retencion autentica cada cabecera con la KEK disponible, deduplica por `idRespaldo` y nunca borra por un id repetido: una copia truncada o duplicada conserva una cabecera autentica.
- T025.4: KEK en Keychain y respaldo diario.
- T025.5: staging, marcador y restauracion (ADR de restauracion propio). Muestra la fecha y el id del respaldo autenticados antes de restaurar.
- T025.6: UI.

## Consecuencias

- Una contrasena olvidada sin su clave de recuperacion hace irrecuperable el respaldo (D2). La app no puede ayudar.
- El id de instalacion, la fecha, el tamano aproximado y los parametros KDF son visibles sin clave en la carpeta sincronizada. El nombre de archivo es opaco (D8).
- No se detecta que alguien con acceso a la carpeta sustituya un respaldo por otro anterior pero autentico de la misma contrasena: cada copia es valida por si sola. T025.5 mitiga mostrando la fecha y el id autenticados para que el usuario confirme cual restaura.
- Un cambio incompatible de formato exige una version nueva de cabecera. Los lectores rechazan versiones desconocidas con `VersionNoSoportada`.
- Leer el inventario requiere recorrer el archivo completo. Es aceptable porque restaurar exige verificarlo todo antes (D5).
- Un respaldo con mas de 100 000 entradas o 16 MiB de manifiesto no se puede crear con esta version. Subir los limites es un cambio de formato que exige revisar la cota de memoria.

## Verificacion

`satcfdi_respaldo_tests` (label `respaldo`) cubre:

- Ida y vuelta con extraccion.
- Texto en claro buscado en un archivo real en disco (RFC, home, `<cfdi:`, una contrasena SAT sintetica bajo `credentials/`, que se rechaza) y ausencia de mensajes Qt durante el flujo.
- Alteracion de cualquier bloque, marco o cabecera, y de la envoltura del otro rol.
- Truncado, incluida una copia truncada con cabecera autentica y la cabecera de una copia sobre los bloques de otra. Reordenado, duplicado y bloque final exactamente lleno.
- Un dispositivo secuencial que entrega de 1 a 7 bytes y 0 intermitentes.
- Contrasena y clave incorrectas con error generico, clave de recuperacion, cambio de contrasena (D16) y parametros KDF en el techo y por encima.
- Rutas hostiles en el escritor y en el lector.
- Limites de entradas, de manifiesto y de carpetas.
- Fuentes enlazadas (archivo, ancestro y raiz), FIFO, cambiantes o borradas.
- Staging no vacio, enlazado, relativo, inexistente o no 0700, y vaciado ante una falla tardia.
- Cancelacion.
- Vector Argon2id contrastado con el CLI de OpenSSL.
- Memoria adicional <= 32 MiB con 3 GiB (`SATCFDI_RESPALDO_PRUEBA_3GB=1`) y con 100 000 entradas (`SATCFDI_RESPALDO_PRUEBA_100K=1`).
