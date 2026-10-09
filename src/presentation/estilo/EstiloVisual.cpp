#include "presentation/estilo/EstiloVisual.h"

#include <QQuickStyle>

namespace satcfdi::presentacion {

void fijarEstiloBasico()
{
    QQuickStyle::setStyle(QStringLiteral("Basic"));
}

} // namespace satcfdi::presentacion
