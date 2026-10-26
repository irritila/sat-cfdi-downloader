#include "CatalogoMensajes.h"

#include "domain/solicitudes/EstadoResumen.h"

#include <QCoreApplication>

namespace satcfdi::catalogo {

namespace {
QString tr(const char* texto)
{
    return QCoreApplication::translate("CatalogoMensajes", texto);
}
// Elige singular o plural y sustituye %n (sin archivo de traduccion, %n del
// plural de Qt no distingue formas).
QString trn(const char* singular, const char* plural, int n)
{
    return QCoreApplication::translate("CatalogoMensajes", n == 1 ? singular : plural).replace(
        QStringLiteral("%n"), QString::number(n));
}
} // namespace

QString motivoCredencial(PreparacionPerfil preparacion, bool perfilActivo)
{
    if (!perfilActivo) {
        return tr("El perfil de esta solicitud está inactivo.");
    }
    switch (preparacion) {
    case PreparacionPerfil::Lista:
        return {};
    case PreparacionPerfil::Vencida:
        return tr("La e.firma de este perfil está vencida. Reemplázala para continuar.");
    case PreparacionPerfil::MaterialFaltante:
    case PreparacionPerfil::MaterialDanado:
        return tr("No se pudo leer la e.firma guardada. Vuelve a importarla.");
    case PreparacionPerfil::EstadoNoDisponible:
        return tr("El llavero de macOS está bloqueado. Desbloquea tu sesión e intenta de nuevo.");
    case PreparacionPerfil::SinCredencial:
        return tr("Este perfil no tiene e.firma registrada. Regístrala para continuar.");
    case PreparacionPerfil::NoVigenteAun:
        return tr("La e.firma de este perfil aún no está vigente.");
    case PreparacionPerfil::Verificando:
        return tr("Comprobando la e.firma del perfil…");
    }
    return {};
}

QString mensajeSolicitud(const SolicitudDetalle& d)
{
    const QString cod = d.codEstatusSolicitud.value_or(QString());
    // T009: la ultima falla ya trae su texto D10 (fase, codigo y causa
    // desglosados por la aplicacion): se usa tal cual, salvo en EnvioIncierto,
    // cuyo mensaje es fijo. Nunca se muestra el Mensaje SAT crudo.
    const QString desglosado =
        d.ultimoErrorDesglosado ? d.ultimoErrorDesglosado->mensaje.trimmed() : QString();
    if (!desglosado.isEmpty() && d.resumen.estadoLocal != EstadoLocal::EnvioIncierto) {
        return desglosado;
    }
    switch (d.resumen.estadoLocal) {
    case EstadoLocal::EnvioIncierto:
        return tr("No se sabe si el SAT registró la solicitud. No se reenviará automáticamente; revisa antes de "
                  "crear otra.");
    case EstadoLocal::EnvioFallido:
        if (cod == QLatin1String("5001")) {
            return tr("No estás autorizado para descargar estos CFDI (5001).");
        }
        if (cod == QLatin1String("5002")) {
            return tr("El SAT ya no acepta solicitudes con este mismo criterio.");
        }
        if (cod == QLatin1String("5003")) {
            return tr("La consulta supera el máximo de CFDI; usa un rango más corto.");
        }
        if (cod == QLatin1String("5005")) {
            return tr("El SAT ya tiene una solicitud activa con este criterio.");
        }
        if (cod == QLatin1String("5011")) {
            return tr("Se alcanzó el límite diario del SAT. Intenta mañana.");
        }
        if (!cod.isEmpty()) {
            return tr("El SAT rechazó la solicitud (código %1). No se registró en el SAT.").arg(cod);
        }
        return tr("El SAT rechazó la solicitud. No se registró en el SAT.");
    case EstadoLocal::Enviada: {
        const QString codigoEstado = d.codigoEstadoSolicitud.value_or(QString()).trimmed();
        if (esSinInformacionSat(d.resumen.estadoSat, d.codigoEstadoSolicitud)) {
            return tr("El SAT no tiene facturas en este periodo."); // T023.6 (D22)
        }
        if (codigoEstado == QLatin1String("5004")) {
            return tr("El SAT no encontró esta solicitud. La verificación automática se detuvo.");
        }
        if (codigoEstado == QLatin1String("5011")) {
            return tr("Se alcanzó el límite diario del SAT. Intenta mañana.");
        }
        if (d.ultimoError && !d.ultimoError->isEmpty()) {
            return tr("No se pudo consultar el estado; se reintentará automáticamente.");
        }
        return {};
    }
    case EstadoLocal::Creada:
        // Sin desglose: solo hay codigo de creacion si el SAT respondio.
        if (!cod.isEmpty()) {
            return tr("El SAT rechazó la solicitud (código %1). No se registró en el SAT.").arg(cod);
        }
        break;
    case EstadoLocal::Enviando:
        break;
    }
    return {};
}

bool esMaximoDescargas(const PaqueteResumen& p)
{
    return p.codigoDescargaSat && *p.codigoDescargaSat == QLatin1String("5008");
}

QString mensajePaquete(const PaqueteResumen& p)
{
    if (p.estadoDescarga == EstadoDescarga::Vencido
        || (p.codigoDescargaSat && *p.codigoDescargaSat == QLatin1String("5007"))) {
        return tr("El paquete ya no existe en el SAT (vencido). Crea una solicitud nueva para el mismo periodo.");
    }
    if (p.estadoDescarga == EstadoDescarga::Error) {
        return esMaximoDescargas(p) ? tr("El paquete alcanzó el máximo de descargas permitidas.")
                                    : tr("No se pudo descargar el paquete. Puedes reintentar.");
    }
    return {};
}

// ---- T025.6: respaldo y restauracion ----

namespace {

using EstadoRespaldo = respaldo::ResultadoRespaldo::Estado;
using CategoriaDestino = respaldo::ErrorDestino::Categoria;
using CategoriaContenedor = respaldo::ErrorRespaldo::Categoria;

QString causaDestino(CategoriaDestino c)
{
    switch (c) {
    case CategoriaDestino::CarpetaInexistente:
    case CategoriaDestino::NoEsCarpeta:
    case CategoriaDestino::CarpetaCambiada:
    case CategoriaDestino::VolumenNoDisponible:
    case CategoriaDestino::SinDescargar:
        return tr("La carpeta elegida no está disponible. Conéctala o espera a que esté descargada "
                  "e inténtalo de nuevo.");
    case CategoriaDestino::SinEscritura:
    case CategoriaDestino::DuenoDistinto:
    case CategoriaDestino::EnlaceIntermedio:
        return tr("No se puede escribir de forma segura en la carpeta elegida. Revisa sus permisos o "
                  "elige otra carpeta.");
    case CategoriaDestino::ExclusivoNoSoportado:
        return tr("El disco de la carpeta elegida no es compatible con el respaldo (por ejemplo, un "
                  "disco con formato exFAT). Elige una carpeta en otro disco.");
    case CategoriaDestino::EspacioInsuficiente:
        return tr("No hay espacio suficiente en la carpeta elegida para el respaldo.");
    case CategoriaDestino::Escritura:
    case CategoriaDestino::Sincronizacion:
    case CategoriaDestino::Conflicto:
    case CategoriaDestino::Publicacion:
    case CategoriaDestino::Lectura:
    case CategoriaDestino::NoRegular:
    case CategoriaDestino::Interno:
        break;
    }
    return tr("No se pudo copiar el respaldo a la carpeta elegida. Inténtalo de nuevo.");
}

QString causaContenedor(std::optional<CategoriaContenedor> c)
{
    if (c) {
        switch (*c) {
        case CategoriaContenedor::FuenteInvalida:
        case CategoriaContenedor::FuenteFaltante:
        case CategoriaContenedor::FuenteCambio:
            return tr("Tus datos cambiaron mientras se copiaban. No se guardó ningún respaldo; "
                      "inténtalo de nuevo.");
        case CategoriaContenedor::LimiteExcedido:
            return tr("Tus datos superan el tamaño que admite un respaldo.");
        default:
            break;
        }
    }
    return tr("No se pudo preparar la copia cifrada en este equipo. Revisa que la contraseña del "
              "respaldo esté configurada e inténtalo de nuevo.");
}

} // namespace

QString causaRespaldo(const ResultadoRespaldoPresentacion& r)
{
    switch (r.estado) {
    case EstadoRespaldo::Exito:
    case EstadoRespaldo::ExitoConAdvertencias:
        return {};
    case EstadoRespaldo::YaEnCurso:
        return tr("Ya hay un respaldo en curso. Espera a que termine.");
    case EstadoRespaldo::Cancelado:
        return tr("El respaldo se canceló. No se guardó ninguna copia.");
    case EstadoRespaldo::Detenido:
        return tr("El respaldo se detuvo porque la aplicación se está cerrando.");
    case EstadoRespaldo::ErrorDestino:
    case EstadoRespaldo::ErrorPublicacion:
        return causaDestino(r.errorDestino.value_or(CategoriaDestino::Interno));
    case EstadoRespaldo::ErrorFuente:
        return tr("Tus datos cambiaron mientras se copiaban. No se guardó ningún respaldo; "
                  "inténtalo de nuevo.");
    case EstadoRespaldo::ErrorCaptura:
        return tr("No se pudo preparar la copia de tus datos. Inténtalo de nuevo.");
    case EstadoRespaldo::ErrorContenedor:
        return causaContenedor(r.errorContenedor);
    }
    return tr("No se pudo crear el respaldo. Inténtalo de nuevo.");
}

MensajeVisible mensajeRespaldo(const ResultadoRespaldoPresentacion& r)
{
    if (r.exito()) {
        const bool conAvisos = r.estado == EstadoRespaldo::ExitoConAdvertencias || r.faltantes > 0
                               || r.omitidos > 0 || r.avisoDepuracion || r.avisoRegistro || r.copiasSinRegistro > 0
                               || r.avisoPersistencia;
        return {conAvisos ? QStringLiteral("advertencia") : QStringLiteral("exito"),
                conAvisos ? tr("Respaldo copiado a la carpeta, con avisos") : tr("Respaldo copiado a la carpeta"),
                tr("La copia cifrada quedó en la carpeta elegida. Tu servicio de nube, si la "
                   "sincroniza, se encarga de lo demás.")};
    }
    if (r.estado == EstadoRespaldo::YaEnCurso || r.estado == EstadoRespaldo::Cancelado
        || r.estado == EstadoRespaldo::Detenido) {
        return {QStringLiteral("neutro"), tr("No se creó el respaldo"), causaRespaldo(r)};
    }
    return {QStringLiteral("error"), tr("No se creó el respaldo"), causaRespaldo(r)};
}

QStringList advertenciasRespaldo(const ResultadoRespaldoPresentacion& r)
{
    QStringList avisos;
    if (!r.exito()) {
        if (r.avisoPersistencia) {
            avisos << tr("No se pudo guardar este resultado; al reabrir la aplicación no se mostrará.");
        }
        return avisos;
    }
    if (r.faltantes > 0) {
        avisos << trn("%n paquete ya no estaba en este equipo y no se incluyó.",
                      "%n paquetes ya no estaban en este equipo y no se incluyeron.", r.faltantes);
    }
    if (r.omitidos > 0) {
        avisos << trn("%n paquete no se pudo incluir.", "%n paquetes no se pudieron incluir.", r.omitidos);
    }
    if (r.avisoDepuracion || r.avisoRegistro) {
        avisos << tr("La copia es válida, pero algunas copias anteriores requieren revisión manual.");
    }
    if (r.avisoPersistencia) {
        avisos << tr("La copia se hizo, pero no se pudo guardar este resultado; al reabrir la aplicación no se "
                     "mostrará.");
    }
    return avisos;
}

QString avisoCopiasSinRegistro(int copias)
{
    if (copias <= 0) {
        return {};
    }
    return trn("Hay %n copia anterior que no está descargada en este equipo y Archivo Fiscal no puede "
               "confirmar que sea suya, así que no se borró. Si ya no la necesitas, bórrala a mano "
               "desde la carpeta.",
               "Hay %n copias anteriores que no están descargadas en este equipo y Archivo Fiscal no "
               "puede confirmar que sean suyas, así que no se borraron. Si ya no las necesitas, "
               "bórralas a mano desde la carpeta.",
               copias);
}

QString mensajeErrorGestor(respaldo::ErrorGestorRespaldo::Categoria c)
{
    using C = respaldo::ErrorGestorRespaldo::Categoria;
    switch (c) {
    case C::NoDisponible: return tr("El respaldo no está disponible en este equipo.");
    case C::FaltaCarpeta: return tr("Primero elige la carpeta del respaldo.");
    case C::FaltaContrasena: return tr("Primero crea la contraseña del respaldo.");
    case C::YaConfigurada: return tr("La contraseña del respaldo ya está configurada.");
    case C::Carpeta:
        return tr("No se pudo usar la carpeta elegida. Revisa que esté disponible o elige otra carpeta.");
    case C::Almacen:
        return tr("No se pudo acceder al llavero de macOS. Desbloquea tu sesión e inténtalo de nuevo.");
    case C::Kdf: return tr("No se pudo preparar el cifrado en este equipo.");
    case C::VerificacionIncorrecta:
        return tr("La contraseña anterior o la clave de recuperación no es correcta.");
    case C::Configuracion: return tr("No se pudo guardar la configuración del respaldo. Inténtalo de nuevo.");
    case C::Detenido: return tr("La aplicación se está cerrando.");
    case C::EnCurso: return tr("Espera a que termine el respaldo en curso.");
    }
    return tr("No se pudo completar la operación. Inténtalo de nuevo.");
}

QString mensajeErrorRestauracion(const respaldo::ErrorRestauracion& error)
{
    using C = respaldo::ErrorRestauracion::Categoria;
    const QString noVerificado = tr("El respaldo no se pudo verificar. Tus datos no se modificaron.");
    const QString masNuevo = tr("Este respaldo lo creó una versión más reciente de Archivo Fiscal. "
                                "Actualiza la aplicación para restaurarlo.");
    const QString origen = tr("No se pudo leer el archivo elegido. Si está en la nube, espera a que se "
                              "descargue e inténtalo de nuevo.");
    switch (error.categoria) {
    case C::EnCurso: return tr("Ya hay una restauración en curso.");
    case C::Detenido: return tr("La aplicación se está cerrando.");
    case C::NoPreparada: return tr("Vuelve a elegir el respaldo para verificarlo.");
    case C::RestauracionPendiente:
        return tr("Ya hay una restauración pendiente; se aplicará al abrir de nuevo la aplicación.");
    case C::Origen: return origen;
    case C::OrigenCambio:
    case C::StagingCambio:
        return tr("El respaldo cambió después de verificarlo. Vuelve a elegirlo para verificarlo otra vez.");
    case C::CredencialIncorrecta: return tr("La contraseña o la clave de recuperación no es correcta.");
    case C::ClaveRecuperacionMalFormada: return tr("La clave de recuperación no tiene un formato válido.");
    case C::Contenedor:
        if (error.contenedor) {
            switch (*error.contenedor) {
            case CategoriaContenedor::VersionNoSoportada: return masNuevo;
            case CategoriaContenedor::CredencialIncorrecta:
                return tr("La contraseña o la clave de recuperación no es correcta.");
            case CategoriaContenedor::ClaveRecuperacionMalFormada:
                return tr("La clave de recuperación no tiene un formato válido.");
            case CategoriaContenedor::Lectura: return origen;
            case CategoriaContenedor::KdfNoDisponible:
                return tr("No se pudo preparar el descifrado en este equipo.");
            case CategoriaContenedor::Cancelado: return tr("Se canceló la verificación del respaldo.");
            default: break;
            }
        }
        return noVerificado;
    case C::EsquemaMasNuevo: return masNuevo;
    case C::MetadatosInvalidos:
    case C::EsquemaInvalido:
    case C::Integridad:
    case C::RutaInesperada:
    case C::Base:
        return noVerificado;
    case C::EspacioInsuficiente:
        return tr("No hay espacio suficiente en el disco para preparar la restauración. Tus datos no se "
                  "modificaron.");
    case C::Cancelado: return tr("Se canceló la verificación del respaldo.");
    case C::Staging:
    case C::Destino:
    case C::Marcador:
    case C::Interno:
        break;
    }
    return tr("No se pudo preparar una restauración segura. Tus datos no se modificaron.");
}

MensajeVisible mensajeSolicitudRestauracion(const respaldo::ResultadoSolicitudRestauracion& r)
{
    using E = respaldo::ResultadoSolicitudRestauracion::Estado;
    switch (r.estado) {
    case E::Relanzando:
        return {QStringLiteral("progreso"), tr("Reiniciando para restaurar"),
                tr("La aplicación se cerrará y se abrirá de nuevo para aplicar la restauración.")};
    case E::SinRelanzamiento:
        return {QStringLiteral("advertencia"), tr("Abre de nuevo la aplicación"),
                tr("La aplicación se cerrará y no podrá abrirse sola. Ábrela de nuevo para aplicar la "
                   "restauración.")};
    case E::FalloTrasDetener:
        return {QStringLiteral("error"), tr("No se pudo solicitar la restauración"),
                tr("La aplicación se reiniciará sin cambiar tus datos. Puedes intentarlo de nuevo.")};
    case E::Rechazada:
        break;
    }
    return {QStringLiteral("error"), tr("No se pudo solicitar la restauración"),
            r.error ? mensajeErrorRestauracion(*r.error)
                    : tr("No se pudo preparar una restauración segura. Tus datos no se modificaron.")};
}

MensajeVisible mensajeDescarte(const ResultadoDescartePresentacion& r)
{
    using E = ResultadoDescartePresentacion::Estado;
    MensajeVisible m;
    switch (r.estado) {
    case E::Descartados:
        m = {QStringLiteral("exito"), tr("Datos anteriores descartados"),
             tr("Se borraron los datos guardados antes de la restauración.")};
        break;
    case E::SinDatosPrevios:
        m = {QStringLiteral("neutro"), tr("No hay datos anteriores"),
             tr("No hay datos anteriores a una restauración que descartar.")};
        break;
    case E::Incompleto:
        m = {QStringLiteral("advertencia"), tr("No se descartaron todos los datos anteriores"),
             tr("Algunos datos anteriores no se pudieron borrar. Las e.firmas anteriores se conservan en "
                "el llavero hasta terminar. Inténtalo de nuevo.")};
        break;
    case E::ErrorReconciliacion:
        m = {QStringLiteral("advertencia"), tr("Datos anteriores descartados, con avisos"),
             tr("Se borraron los datos anteriores, pero no se pudo terminar de limpiar las e.firmas "
                "anteriores del llavero de macOS. Tus perfiles actuales no se ven afectados.")};
        break;
    }
    if (r.carpetasAjenas > 0) {
        m.descripcion += QLatin1Char(' ')
                         + trn("Hay %n carpeta «pre-restauracion» que Archivo Fiscal no creó o que es un "
                               "enlace; no se tocó. Revísala a mano.",
                               "Hay %n carpetas «pre-restauracion» que Archivo Fiscal no creó o que son "
                               "enlaces; no se tocaron. Revísalas a mano.",
                               r.carpetasAjenas);
    }
    return m;
}

MensajeVisible mensajeArranque(ResultadoArranquePresentacion::Estado estado, const QString& fecha)
{
    using E = ResultadoArranquePresentacion::Estado;
    switch (estado) {
    case E::Aplicada:
        return {QStringLiteral("exito"), tr("Restauración aplicada"),
                (fecha.isEmpty() ? tr("Se restauró el respaldo.")
                                 : tr("Se restauró el respaldo del %1.").arg(fecha))
                    + QLatin1Char(' ')
                    + tr("Registra de nuevo la e.firma de cada perfil antes de descargar. Tus datos "
                         "anteriores se guardaron aparte hasta que los descartes.")};
    case E::Descartada:
        return {QStringLiteral("neutro"), tr("Restauración descartada"),
                tr("La restauración no se confirmó o el respaldo preparado cambió. Tus datos actuales no "
                   "se modificaron.")};
    case E::Revertida:
        return {QStringLiteral("advertencia"), tr("Restauración revertida"),
                tr("No se pudo completar la restauración y se deshizo. Tus datos actuales no se "
                   "modificaron.")};
    case E::Ninguno:
        break;
    }
    return {};
}

MensajeVisible avisoDatosPrevios()
{
    return {QStringLiteral("advertencia"), tr("Hay datos anteriores a una restauración"),
            tr("Se guardaron aparte por si los necesitas. Mientras existan, las e.firmas anteriores se "
               "conservan en el llavero de macOS. Puedes descartarlos desde Respaldo.")};
}

QString textoFase(respaldo::RespaldoService::Fase fase)
{
    using F = respaldo::RespaldoService::Fase;
    switch (fase) {
    case F::Captura: return tr("Preparando la copia de tus datos…");
    case F::Escritura: return tr("Cifrando y copiando a la carpeta…");
    case F::Publicacion: return tr("Terminando la copia…");
    case F::Depuracion: return tr("Depurando copias anteriores…");
    }
    return {};
}

QString textoFase(respaldo::RestauracionService::Fase fase)
{
    using F = respaldo::RestauracionService::Fase;
    switch (fase) {
    case F::Autenticando: return tr("Comprobando la contraseña…");
    case F::Extrayendo: return tr("Extrayendo y verificando el respaldo…");
    case F::Validando: return tr("Validando los datos…");
    case F::Sincronizando: return tr("Guardando en el disco…");
    }
    return {};
}

QString textoPendienteDiario(EstadoRespaldoPresentacion::PendienteDiario pendiente)
{
    using P = EstadoRespaldoPresentacion::PendienteDiario;
    switch (pendiente) {
    case P::FaltaCarpeta: return tr("El respaldo diario empezará cuando elijas una carpeta.");
    case P::FaltaContrasena: return tr("El respaldo diario empezará cuando crees una contraseña.");
    case P::Ninguno: break;
    }
    return {};
}

// ---- T026.3: exportacion de facturas ----

ResumenExportacion resumenExportacion(const ResultadoExportacion& resultado)
{
    ResumenExportacion r;
    r.estado = resultado.estado;
    if (resultado.error) {
        r.error = resultado.error->codigo;
        if (resultado.error->destino) {
            r.destino = resultado.error->destino->codigo;
        } else if (resultado.error->zip && resultado.error->zip->destino) {
            r.destino = *resultado.error->zip->destino; // escritura del ZIP (p. ej. disco lleno)
        }
    }
    r.exportadas = resultado.exportadas;
    r.pdfExportados = resultado.pdfExportados;
    r.sinPdf = resultado.sinPdf;
    r.omitidas = resultado.omitidas;
    r.lineasMes = resultado.lineasMes;
    r.advertenciaDurabilidad = resultado.advertenciaDurabilidad;
    return r;
}

QString avisoExportacion()
{
    return tr("El archivo que vas a crear contiene tus facturas completas y no está protegido con contraseña. "
              "Cualquier persona que lo abra podrá verlas. Si lo guardas en iCloud Drive, Dropbox o una carpeta "
              "compartida, se copiará fuera de este equipo. Guárdalo en un lugar de confianza y bórralo cuando ya "
              "no lo necesites.");
}

QString textoConteoPrevisto(int conteo)
{
    if (conteo <= 0) {
        return tr("No hay facturas para exportar");
    }
    return trn("Se exportará %n factura.", "Se exportarán %n facturas.", conteo);
}

QStringList avisosPlanExportacion(int omitidas, int lineasMes)
{
    QStringList avisos;
    if (omitidas > 0) {
        avisos << trn("%n factura no está disponible en este equipo y no se incluirá.",
                      "%n facturas no están disponibles en este equipo y no se incluirán.", omitidas);
    }
    if (lineasMes > 0) {
        avisos << trn("Hay %n mes con descargas pendientes o incompleto: pueden faltar facturas de ese mes.",
                      "Hay %n meses con descargas pendientes o incompletos: pueden faltar facturas de esos meses.",
                      lineasMes);
    }
    return avisos;
}

QString textoErrorRango(horizonte_facturas::ErrorRango error)
{
    using E = horizonte_facturas::ErrorRango;
    switch (error) {
    case E::Ninguno: return {};
    case E::Futuro: return tr("El rango no puede incluir meses que aún no empiezan.");
    case E::FueraDelHorizonte:
        return tr("El rango solo puede incluir meses de los últimos %1 años.").arg(horizonte_facturas::kAniosAtras);
    case E::Invertido: return tr("El mes inicial debe ser anterior o igual al mes final.");
    }
    return {};
}

QString textoErrorPlanExportacion(ErrorExportacion::Codigo codigo)
{
    using C = ErrorExportacion::Codigo;
    switch (codigo) {
    case C::Detenido:
        return tr("La exportación no está disponible mientras Archivo Fiscal se cierra o se restaura.");
    case C::EnCurso: return tr("Ya hay una exportación en curso.");
    default: break;
    }
    return tr("No se pudieron leer las facturas para exportar. Intenta de nuevo.");
}

QString textoErrorPublicarExportacion(ErrorExportacion::Codigo codigo)
{
    if (codigo == ErrorExportacion::Codigo::Detenido) {
        return textoErrorPlanExportacion(codigo);
    }
    return tr("El archivo preparado ya no se puede guardar. Cierra y vuelve a exportar.");
}

QString textoDestinoExistente()
{
    return tr("Ya existe un archivo con ese nombre en la carpeta. Escribe otro nombre.");
}

QString textoErrorDestinoCorregible(exportacion::ErrorDestinoExportacion::Codigo codigo)
{
    using C = exportacion::ErrorDestinoExportacion::Codigo;
    switch (codigo) {
    case C::DestinoExistente: return textoDestinoExistente();
    case C::CarpetaNoDisponible:
    case C::NoEsCarpeta: return tr("La carpeta elegida ya no está disponible. Elige otra.");
    case C::CarpetaNoPermitida:
        return tr("No se puede guardar la exportación dentro de los datos de Archivo Fiscal. Elige otra carpeta.");
    case C::SinPermiso: return tr("No hay permiso para guardar en esa carpeta. Elige otra.");
    case C::NombreInvalido: return tr("El nombre del archivo no es válido. Escribe otro nombre.");
    default: break;
    }
    return {};
}

QString textoFase(FaseExportacion fase)
{
    switch (fase) {
    case FaseExportacion::Preparando: return tr("Preparando la exportación…");
    case FaseExportacion::Xml: return tr("Agregando facturas…");
    case FaseExportacion::Pdf: return tr("Creando los PDF…");
    case FaseExportacion::Finalizando: return tr("Terminando el archivo…");
    }
    return {};
}

MensajeVisible mensajeExportacion(const ResumenExportacion& r)
{
    using E = ResultadoExportacion::Estado;
    switch (r.estado) {
    case E::Publicada:
        if (r.exportadas == 0) { // D22
            return {QStringLiteral("advertencia"), tr("No se exportó ninguna factura"),
                    tr("No se pudo leer ninguna de las facturas. El archivo ZIP solo contiene la lista de las "
                       "facturas omitidas.")};
        }
        return {r.advertenciaDurabilidad ? QStringLiteral("advertencia") : QStringLiteral("exito"),
                tr("Exportación terminada"),
                r.pdfExportados > 0
                    ? trn("Se exportó %n factura", "Se exportaron %n facturas", r.exportadas)
                          + trn(" con %n PDF.", " con %n PDF.", r.pdfExportados)
                    : trn("Se exportó %n factura.", "Se exportaron %n facturas.", r.exportadas)};
    case E::SinFacturas:
        return {QStringLiteral("neutro"), tr("No hay facturas para exportar"), tr("No se creó ningún archivo.")};
    case E::Cancelada:
        return {QStringLiteral("neutro"), tr("Exportación cancelada"), tr("No se creó ningún archivo.")};
    case E::Error:
        break;
    }
    QString descripcion;
    if ((r.error == ErrorExportacion::Codigo::Destino || r.error == ErrorExportacion::Codigo::Zip) && r.destino) {
        using C = exportacion::ErrorDestinoExportacion::Codigo;
        descripcion = *r.destino == C::EspacioInsuficiente
                          ? tr("No hay espacio suficiente en el disco. Libera espacio o elige otra carpeta.")
                          : textoErrorDestinoCorregible(*r.destino);
    } else if (r.error == ErrorExportacion::Codigo::EnCurso || r.error == ErrorExportacion::Codigo::Detenido) {
        descripcion = textoErrorPlanExportacion(*r.error);
    }
    if (descripcion.isEmpty()) {
        descripcion = tr("No se pudo crear el archivo. Intenta de nuevo.");
    }
    return {QStringLiteral("error"), tr("No se pudo exportar"), descripcion + QLatin1Char(' ')
                                                                    + tr("No se creó ningún archivo.")};
}

QStringList detallesExportacion(const ResumenExportacion& r)
{
    QStringList lineas;
    if (r.estado != ResultadoExportacion::Estado::Publicada) {
        return lineas;
    }
    if (r.sinPdf > 0) {
        lineas << trn("%n factura se exportó sin PDF.", "%n facturas se exportaron sin PDF.", r.sinPdf);
    }
    if (r.omitidas > 0) {
        lineas << trn("%n factura no se exportó.", "%n facturas no se exportaron.", r.omitidas);
    }
    if (r.lineasMes > 0) {
        lineas << trn("%n mes tiene descargas pendientes o está incompleto.",
                      "%n meses tienen descargas pendientes o están incompletos.", r.lineasMes);
    }
    if (r.sinPdf > 0 || r.omitidas > 0 || r.lineasMes > 0) {
        lineas << tr("El detalle está en omitidas.txt, dentro del archivo ZIP.");
    }
    return lineas;
}

QString advertenciaExportacion(const ResumenExportacion& r)
{
    if (r.estado != ResultadoExportacion::Estado::Publicada || !r.advertenciaDurabilidad) {
        return {};
    }
    return tr("El archivo se creó, pero no se pudo confirmar que quedara guardado en el disco. Ábrelo para "
              "comprobarlo antes de borrar nada.");
}

} // namespace satcfdi::catalogo
