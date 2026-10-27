#pragma once

// Icono plantilla del menu bar (T004, T014.4 D2, T020 D6). C++ portable sin
// AppKit: renderiza los SVG de `resources/branding/menu/` (propuesta A,
// carpeta en contorno con frente oblicuo), embebidos como recursos Qt, con
// QSvgRenderer sobre QImage para poder probar las variantes sin sesion
// grafica. El adaptador convierte las imagenes en un QIcon con isMask
// (NSImage template: macOS lo tine segun apariencia clara/oscura).

#include "ports/OSIntegration.h"

#include <QImage>
#include <QString>

namespace satcfdi::macos {

// Ruta del recurso Qt del SVG del estado para `lado` (18 -> menu-18*, 36 o
// mas -> menu-36*). Ej.: ":/menubar/menu-18-pausa.svg".
QString rutaSvgIconoMenuBar(OSIntegration::EstadoIcono estado, int lado);

// Renderiza el icono de `lado` x `lado` pixeles (rejilla logica de 18 pt; 18 y
// 36 para @1x/@2x). Monocromo negro sobre transparente.
// - Normal: carpeta en contorno con frente oblicuo.
// - Trabajando: insignia de arco abierto arriba a la derecha.
// - Pausado: insignia de dos barras.
// - Atencion: insignia de punto solido.
// Las insignias viven en x=12..17, y=1..6 (rejilla de 18) y no tocan el frente:
// la region y>=7 es identica en los cuatro estados.
// Devuelve una imagen nula si el recurso no carga.
QImage dibujarIconoMenuBar(OSIntegration::EstadoIcono estado, int lado);

} // namespace satcfdi::macos
