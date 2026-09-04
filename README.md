# SAT CFDI Downloader

Este repositorio contiene una aplicacion de escritorio local-first para gestionar descargas masivas de CFDI. La descarga usa el servicio web que el SAT pone al servicio del contribuyente.

El producto esta pensado por ahora para uso personal: un unico usuario actuando como contador, con capacidad de administrar perfiles SAT de contribuyentes/RFC desde su propio equipo.

## Decision de producto actual

Este proyecto no se esta disenando como producto comercial, SaaS, portal multiusuario ni herramienta para clientes ficticios o potenciales. Las decisiones deben favorecer el uso personal y local antes que escenarios comerciales que todavia no existen.

Decisiones actuales:

- Usuario objetivo: un unico usuario local, actuando como contador.
- Plataforma: aplicacion desktop.
- Backend remoto: fuera de alcance por ahora.
- Operacion: multiples contribuyentes/RFC o perfiles SAT administrados por el usuario.
- Autenticacion SAT: e.firma, certificado, llave privada y contrasena manejados de forma segura en el equipo local. No se asume CSD para descarga masiva salvo confirmacion oficial.
- Flujo principal: crear solicitudes de descarga masiva, consultar estado y descargar paquetes disponibles.
- Filtros de busqueda: solo los filtros aceptados por el servicio web del SAT.
- Persistencia: solicitudes, paquetes, XML, metadatos y logs guardados localmente.
- Escala inicial esperada: alrededor de 10,000 CFDI.
- Procesamiento posterior: consulta local de CFDI descargados; indexacion, exportacion, validacion o conciliacion se decidiran despues solo si hacen falta para el uso personal.

## Arquitectura objetivo inicial

- UI desktop.
- Base de datos local.
- Carpeta local para paquetes y XML.
- Worker local para solicitudes, consultas de estado y descargas.
- Logs locales.
- Sin backend remoto por ahora.

## Requerimiento general

El sistema debe permitir a un usuario unico, actuando como contador, gestionar perfiles SAT de contribuyentes, almacenar sus credenciales de forma segura en el equipo local, crear solicitudes de descarga masiva de CFDI conforme a los filtros soportados por el servicio web del SAT, consultar periodicamente el estado de las solicitudes mediante un worker local, descargar los paquetes disponibles, extraer los XML, guardar paquetes/XML/metadatos/logs en almacenamiento local y permitir consultar los CFDI descargados desde una interfaz de escritorio.

## Milestones

- [#1](docs/milestones/m1-app-desktop.md) App escritorio distribuible.

## Documentos del proyecto

* [Requerimientos](docs/requirements.md)

## Otras referencias

* [Documentacion oficial del servicio web proporcionada por el SAT](docs/web-service.md)
* https://github.com/phpcfdi/sat-ws-descarga-masiva
    - Repositorio publico que implementa la descarga masiva con php.
