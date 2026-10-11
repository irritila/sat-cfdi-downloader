#pragma once

// Icono plantilla del menu bar (T004, T014.4 D2). C++ portable sin AppKit:
// dibuja con QPainter sobre QImage para poder probar las variantes sin sesion
// grafica. El adaptador convierte las imagenes en un QIcon con isMask (NSImage
// template: macOS lo tine segun apariencia clara/oscura).

#include "ports/OSIntegration.h"

#include <QImage>

namespace satcfdi::macos {

// Dibuja el icono de `lado` x `lado` pixeles (rejilla logica de 18 pt; 18 y
// 36 para @1x/@2x). Monocromo negro sobre transparente.
// - Normal: documento con esquina doblada y flecha de descarga.
// - Pausado: el documento con insignia de pausa (dos barras) abajo a la derecha.
// - Trabajando: insignia con flecha circular (sincronizando).
// - Atencion: insignia con punto solido.
// Las insignias recortan (transparente) la esquina del documento para que se
// lean a 18 px.
QImage dibujarIconoMenuBar(OSIntegration::EstadoIcono estado, int lado);

} // namespace satcfdi::macos
