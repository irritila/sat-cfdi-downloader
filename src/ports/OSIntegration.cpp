#include "ports/OSIntegration.h"

namespace satcfdi {

// Destructor fuera de linea: ancla la vtable y el codigo MOC en satcfdi_ports.
OSIntegration::~OSIntegration() = default;

OSIntegration::ResultadoCopiaSecreta OSIntegration::copiarTextoSecreto(const QString& texto)
{
    Q_UNUSED(texto);
    return ResultadoCopiaSecreta::NoSoportado;
}

OSIntegration::ResultadoImpresion OSIntegration::imprimirTexto(const QString& titulo, const QString& texto)
{
    Q_UNUSED(titulo);
    Q_UNUSED(texto);
    return ResultadoImpresion::NoSoportado;
}

OSIntegration::ResultadoRelanzamiento OSIntegration::solicitarRelanzamiento()
{
    return ResultadoRelanzamiento::NoSoportado;
}

std::unique_ptr<OSIntegration::ActividadSistema> OSIntegration::iniciarActividadSistema(MotivoActividad motivo)
{
    Q_UNUSED(motivo);
    return std::make_unique<ActividadSistema>();
}

} // namespace satcfdi
