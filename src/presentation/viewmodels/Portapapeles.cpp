#include "Portapapeles.h"

#include <QClipboard>
#include <QGuiApplication>

namespace satcfdi {

Portapapeles::Portapapeles(QObject* parent)
    : QObject(parent)
{
}

bool Portapapeles::copiar(const QString& texto) const
{
    QClipboard* portapapeles = QGuiApplication::clipboard();
    if (texto.isEmpty() || !portapapeles) {
        return false;
    }
    portapapeles->setText(texto);
    return true;
}

} // namespace satcfdi
