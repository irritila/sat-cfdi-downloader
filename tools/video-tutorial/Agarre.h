#pragma once

// T022 D7 (C2): decision de si el press de un clic sigue agarrado antes del
// release. Funcion pura sobre el objetivo del `mover` previo y el
// `mouseGrabberItem()` de la ventana, para probarla sin grabar.
//
// - Grabber = objetivo, un descendiente (p. ej. el campo dentro de un
//   control) o un ancestro (p. ej. la fila que agarra el clic hecho sobre su
//   texto): conserva.
// - Grabber ajeno (otro control): no conserva.
// - Sin grabber: conserva solo si el objetivo sigue `pressed` o `down`
//   (controles que no toman el agarre exclusivo).

#include <QQuickItem>

namespace video {

inline bool presionado(const QQuickItem* item)
{
    for (const char* p : {"pressed", "down"}) {
        if (item->metaObject()->indexOfProperty(p) >= 0 && item->property(p).toBool()) {
            return true;
        }
    }
    return false;
}

inline bool conservaAgarre(const QQuickItem* objetivo, const QQuickItem* grabber)
{
    if (objetivo == nullptr) {
        return false;
    }
    if (grabber != nullptr) {
        return grabber == objetivo || objetivo->isAncestorOf(grabber) || grabber->isAncestorOf(objetivo);
    }
    return presionado(objetivo);
}

} // namespace video
