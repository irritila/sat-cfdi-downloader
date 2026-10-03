#include "EFirmaFormViewModel.h"

#include "application/profiles/CredencialesSatService.h"
#include "domain/perfiles/PerfilId.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QFileInfo>
#include <QFuture>

#include <utility>

namespace satcfdi {

namespace {

QString campoDe(OrigenErrorEFirma origen)
{
    switch (origen) {
    case OrigenErrorEFirma::Certificado:
        return QStringLiteral("certificado");
    case OrigenErrorEFirma::Llave:
        return QStringLiteral("llave");
    case OrigenErrorEFirma::Contrasena:
        return QStringLiteral("contrasena");
    case OrigenErrorEFirma::Ninguno:
        return {};
    }
    return {};
}

} // namespace

EFirmaFormViewModel::EFirmaFormViewModel(CredencialesSatService* credenciales, QObject* parent)
    : QObject(parent)
    , m_credenciales(credenciales)
{
    Q_ASSERT(credenciales != nullptr);
}

bool EFirmaFormViewModel::puedeEnviar() const
{
    return m_fase != Fase::Validando && !m_perfilId.isEmpty() && !m_nombreCertificado.isEmpty()
           && !m_nombreLlave.isEmpty();
}

QString EFirmaFormViewModel::rutaLocal(const QUrl& archivo)
{
    return archivo.isLocalFile() ? archivo.toLocalFile() : QString();
}

void EFirmaFormViewModel::restablecer()
{
    ++m_generacion;
    m_fase = Fase::Capturando;
    m_nombreCertificado.clear();
    m_nombreLlave.clear();
    m_rutaCertificado.clear();
    m_rutaLlave.clear();
    m_errorKey.clear();
    m_errorMessage.clear();
    m_campoConError.clear();
}

void EFirmaFormViewModel::iniciar(const QString& perfilId, const QString& perfilRfc, bool esReemplazo)
{
    restablecer();
    m_perfilId = PerfilId::desdeTexto(perfilId) ? perfilId : QString();
    m_perfilRfc = perfilRfc;
    m_esReemplazo = esReemplazo;
    emit cambio();
}

void EFirmaFormViewModel::seleccionarCertificado(const QUrl& archivo)
{
    if (m_fase == Fase::Validando) {
        return;
    }
    const QString ruta = rutaLocal(archivo);
    m_rutaCertificado = ruta;
    m_nombreCertificado = ruta.isEmpty() ? QString() : QFileInfo(ruta).fileName();
    if (m_campoConError == QStringLiteral("certificado")) {
        m_campoConError.clear();
    }
    emit cambio();
}

void EFirmaFormViewModel::seleccionarLlave(const QUrl& archivo)
{
    if (m_fase == Fase::Validando) {
        return;
    }
    const QString ruta = rutaLocal(archivo);
    m_rutaLlave = ruta;
    m_nombreLlave = ruta.isEmpty() ? QString() : QFileInfo(ruta).fileName();
    if (m_campoConError == QStringLiteral("llave")) {
        m_campoConError.clear();
    }
    emit cambio();
}

void EFirmaFormViewModel::fallarLocal(const QString& clave, const QString& mensaje, const QString& campo)
{
    m_fase = Fase::Error;
    m_errorKey = clave;
    m_errorMessage = mensaje;
    m_campoConError = campo;
    emit cambio();
}

bool EFirmaFormViewModel::enviar(const QUrl& certificado, const QUrl& llave, const QString& contrasena)
{
    // La contrasena entra al buffer sensible de inmediato; si no se envia,
    // el buffer se limpia al salir.
    BufferSecreto secreto = BufferSecreto::desdeTexto(contrasena);

    if (m_fase == Fase::Validando || !m_credenciales) {
        return false; // sin doble envio
    }
    const std::optional<PerfilId> id = PerfilId::desdeTexto(m_perfilId);
    if (!id) {
        fallarLocal(QStringLiteral("PerfilInexistente"), tr("Selecciona un perfil guardado."), QString());
        return false;
    }
    if (!certificado.isEmpty()) {
        m_rutaCertificado = rutaLocal(certificado);
        m_nombreCertificado = m_rutaCertificado.isEmpty() ? QString() : QFileInfo(m_rutaCertificado).fileName();
    }
    if (!llave.isEmpty()) {
        m_rutaLlave = rutaLocal(llave);
        m_nombreLlave = m_rutaLlave.isEmpty() ? QString() : QFileInfo(m_rutaLlave).fileName();
    }
    if (m_rutaCertificado.isEmpty()) {
        fallarLocal(QStringLiteral("CertificadoRequerido"), tr("Selecciona el archivo de certificado (.cer)."),
                    QStringLiteral("certificado"));
        return false;
    }
    if (m_rutaLlave.isEmpty()) {
        fallarLocal(QStringLiteral("LlaveRequerida"), tr("Selecciona el archivo de llave privada (.key)."),
                    QStringLiteral("llave"));
        return false;
    }
    if (secreto.vacio()) {
        fallarLocal(QStringLiteral("ContrasenaRequerida"), tr("Escribe la contrasena de la llave privada."),
                    QStringLiteral("contrasena"));
        return false;
    }

    EntradaEFirma entrada;
    entrada.rutaCertificado = std::exchange(m_rutaCertificado, QString());
    entrada.rutaLlavePrivada = std::exchange(m_rutaLlave, QString());
    entrada.contrasena = std::move(secreto);

    const quint64 generacion = ++m_generacion;
    const QString perfil = m_perfilId;
    m_fase = Fase::Validando;
    m_errorKey.clear();
    m_errorMessage.clear();
    m_campoConError.clear();

    QFuture<CredencialesSatService::ResultadoImportacion> futuro =
        m_esReemplazo ? m_credenciales->reemplazar(*id, std::move(entrada))
                      : m_credenciales->importar(*id, std::move(entrada));
    emit cambio();

    futuro.then(this, [this, generacion, perfil](CredencialesSatService::ResultadoImportacion r) {
        if (generacion != m_generacion) {
            return; // otra captura, otro perfil o cancelado
        }
        if (r.esExito()) {
            m_fase = Fase::Exito;
            emit cambio();
            emit operacionTerminada(perfil, true);
            return;
        }
        aplicarError(r.error());
        emit operacionTerminada(perfil, false);
    });
    return true;
}

void EFirmaFormViewModel::aplicarError(const ErrorCredencialSat& error)
{
    QString clave;
    QString mensaje;
    switch (error.tipo) {
    case ErrorCredencialSat::Tipo::PerfilInvalido:
        if (error.codigoPerfil == ErrorCredencialSat::CodigoPerfil::PerfilInactivo) {
            clave = QStringLiteral("PerfilInactivo");
            mensaje = tr("El perfil esta inactivo; no se puede registrar su e.firma.");
        } else {
            clave = QStringLiteral("PerfilInexistente");
            mensaje = tr("El perfil ya no existe.");
        }
        break;
    case ErrorCredencialSat::Tipo::CredencialExistente:
        clave = QStringLiteral("CredencialExistente");
        mensaje = tr("El perfil ya tiene una e.firma registrada; usa Reemplazar e.firma.");
        break;
    case ErrorCredencialSat::Tipo::Almacen:
        if (error.categoria) {
            clave = claveEstable(*error.categoria);
            mensaje = mensajeVisible(*error.categoria);
        } else {
            clave = QStringLiteral("Interno");
            mensaje = tr("No se pudo registrar la e.firma.");
        }
        break;
    case ErrorCredencialSat::Tipo::Persistencia:
        clave = QStringLiteral("Persistencia");
        mensaje = tr("No se pudo guardar la e.firma. Intenta de nuevo.");
        break;
    case ErrorCredencialSat::Tipo::HiloNoPermitido:
        clave = QStringLiteral("Interno");
        mensaje = tr("No se pudo registrar la e.firma.");
        break;
    }
    if (m_esReemplazo) {
        mensaje += QLatin1Char(' ') + tr("La e.firma anterior sigue registrada sin cambios.");
    }
    m_fase = Fase::Error;
    m_errorKey = clave;
    m_errorMessage = mensaje;
    m_campoConError = campoDe(error.origen);
    emit cambio();
}

void EFirmaFormViewModel::cancelar()
{
    if (m_fase == Fase::Validando) {
        return;
    }
    restablecer();
    emit cambio();
}

void EFirmaFormViewModel::descartarSeleccion()
{
    if (m_rutaCertificado.isEmpty() && m_rutaLlave.isEmpty() && m_nombreCertificado.isEmpty()
        && m_nombreLlave.isEmpty()) {
        return;
    }
    m_rutaCertificado.clear();
    m_rutaLlave.clear();
    m_nombreCertificado.clear();
    m_nombreLlave.clear();
    emit cambio();
}

void EFirmaFormViewModel::abandonar()
{
    restablecer(); // ++generacion: la respuesta en curso ya no aplica
    emit cambio();
}

} // namespace satcfdi
