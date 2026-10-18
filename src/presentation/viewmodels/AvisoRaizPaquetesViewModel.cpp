#include "AvisoRaizPaquetesViewModel.h"

namespace satcfdi {

namespace {

// Error con la raiz nueva en uso: la ruta anterior existe pero no es una
// carpeta (no se modifico) y la sesion usa la carpeta nueva.
bool anteriorNoEsCarpeta(const AvisoRaizPaquetesViewModel& vm)
{
    return vm.motivo() == AvisoRaizPaquetesViewModel::Motivo::Error && !vm.rutaEfectiva().isEmpty()
           && vm.rutaEfectiva() == vm.rutaNueva();
}

} // namespace

AvisoRaizPaquetesViewModel::AvisoRaizPaquetesViewModel(QObject* parent)
    : QObject(parent)
{
}

void AvisoRaizPaquetesViewModel::fijar(const Datos& datos)
{
    if (activo() || datos.motivo == Motivo::Ninguno) {
        return; // fijo durante la sesion
    }
    m_datos = datos;
    emit avisoChanged();
}

QString AvisoRaizPaquetesViewModel::variante() const
{
    switch (m_datos.motivo) {
    case Motivo::Conflicto:
    case Motivo::Error:
        return QStringLiteral("advertencia");
    case Motivo::RaizNoDisponible:
        return QStringLiteral("error");
    case Motivo::Ninguno:
        break;
    }
    return {};
}

QString AvisoRaizPaquetesViewModel::titulo() const
{
    switch (m_datos.motivo) {
    case Motivo::Conflicto:
        return tr("Los paquetes siguen en la carpeta anterior.");
    case Motivo::Error:
        return anteriorNoEsCarpeta(*this) ? tr("La carpeta de paquetes anterior no es válida.")
                                          : tr("No se pudo mover la carpeta de paquetes.");
    case Motivo::RaizNoDisponible:
        return tr("No se pudo crear la carpeta de paquetes.");
    case Motivo::Ninguno:
        break;
    }
    return {};
}

QString AvisoRaizPaquetesViewModel::descripcion() const
{
    switch (m_datos.motivo) {
    case Motivo::Conflicto:
        return tr("No se movió %1 a %2 porque la carpeta nueva ya tiene contenido. "
                  "Esta sesión usa %3. Revisa las dos carpetas y deja los paquetes solo en una; "
                  "el cambio se intentará de nuevo al reiniciar la aplicación.")
            .arg(m_datos.rutaLegada, m_datos.rutaNueva, m_datos.rutaEfectiva);
    case Motivo::Error:
        if (anteriorNoEsCarpeta(*this)) {
            return tr("La ruta anterior %1 no es una carpeta; no se modificó. Esta sesión usa %2.")
                .arg(m_datos.rutaLegada, m_datos.rutaEfectiva);
        }
        return tr("No se movió %1 a %2 y ningún archivo se copió ni se borró. "
                  "Esta sesión usa %3. El cambio se intentará de nuevo al reiniciar la aplicación.")
            .arg(m_datos.rutaLegada, m_datos.rutaNueva, m_datos.rutaEfectiva);
    case Motivo::RaizNoDisponible:
        return tr("%1 no existe o no es una carpeta, así que las descargas pueden fallar. "
                  "Esta sesión usa %2. Se intentará crearla de nuevo al reiniciar la aplicación.")
            .arg(m_datos.rutaNueva, m_datos.rutaEfectiva);
    case Motivo::Ninguno:
        break;
    }
    return {};
}

} // namespace satcfdi
