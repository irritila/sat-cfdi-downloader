#include "FranjaMesViewModel.h"

#include <QStringList>

namespace satcfdi {

FranjaMesViewModel::FranjaMesViewModel(QObject* parent)
    : QObject(parent)
{
}

QString FranjaMesViewModel::nombreAccesible() const
{
    QStringList partes;
    for (const QString& p : {m_d.estadoTexto, m_d.titular, m_d.descripcion, m_d.hasta.isEmpty() ? QString() : m_d.hasta,
                             m_d.causa}) {
        if (!p.isEmpty() && !partes.contains(p)) {
            partes.append(p);
        }
    }
    return partes.join(QStringLiteral(". "));
}

void FranjaMesViewModel::fijar(const Datos& datos)
{
    if (m_d == datos) {
        return;
    }
    m_d = datos;
    emit cambio();
}

} // namespace satcfdi
