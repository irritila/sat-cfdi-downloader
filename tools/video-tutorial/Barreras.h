#pragma once

// Barreras del guion (T017 D6): congelan el reloj de animaciones y esperan
// en tiempo real a una condicion de la UI, con timeout. `bombear` procesa
// eventos y dibuja sin grabar (false = la grabacion ya fallo por otra causa).
// Devuelven el error (sin la escena; la agrega quien llama) o vacio.

#include <QString>

#include <functional>

class QQuickItem;

namespace video {

using Bombear = std::function<bool()>;

// documentoListo del visor (`visorPdfCfdi`) y la primera pagina rasterizada.
// documentoError (vm en Error o PdfDocument.Error) o timeout -> error.
QString esperarDocumento(QQuickItem* raiz, int timeoutMs, const Bombear& bombear);

// La propiedad `count` del control `objetivo` (objectName o ruta) llega a
// `conteo`. Sirve tras teclear en una busqueda con debounce de tiempo real.
QString esperarConteo(QQuickItem* raiz, const QString& objetivo, int conteo, int timeoutMs, const Bombear& bombear);

} // namespace video
