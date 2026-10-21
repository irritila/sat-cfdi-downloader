#include "HistorialListModel.h"

#include <QSet>

#include <utility>

namespace satcfdi {

HistorialListModel::HistorialListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int HistorialListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_filas.size());
}

QVariant HistorialListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_filas.size()) {
        return {};
    }
    const FilaHistorial& f = m_filas.at(index.row());
    switch (role) {
    case ClaveRole: return f.clave;
    case AnioRole: return f.anio;
    case MesRole: return f.mes;
    case Qt::DisplayRole:
    case TituloRole: return f.titulo;
    case FechaRole: return f.fecha;
    case TextoFechaRole: return f.textoFecha;
    case ResultadoRole: return f.resultado;
    case ResultadoTextoRole: return f.resultadoTexto;
    case TonoRole: return f.tono;
    case IconoRole: return f.icono;
    case TextoConteosRole: return f.textoConteos;
    case DetalleRole: return f.detalle;
    case EliminacionParcialRole: return f.eliminacionParcial;
    case AnteriorRole: return f.anterior;
    case PuedeVerFacturasRole: return f.puedeVerFacturas;
    case PuedeReintentarRole: return f.puedeReintentar;
    case PuedeVerDetallesRole: return f.puedeVerDetalles;
    case EligeDireccionRole: return f.eligeDireccion;
    case Qt::AccessibleTextRole:
    case NombreAccesibleRole: return f.nombreAccesible;
    default: return {};
    }
}

QHash<int, QByteArray> HistorialListModel::roleNames() const
{
    return {{ClaveRole, "clave"},
            {AnioRole, "anio"},
            {MesRole, "mes"},
            {TituloRole, "titulo"},
            {FechaRole, "fecha"},
            {TextoFechaRole, "textoFecha"},
            {ResultadoRole, "resultado"},
            {ResultadoTextoRole, "resultadoTexto"},
            {TonoRole, "tono"},
            {IconoRole, "icono"},
            {TextoConteosRole, "textoConteos"},
            {DetalleRole, "detalle"},
            {EliminacionParcialRole, "eliminacionParcial"},
            {AnteriorRole, "anterior"},
            {PuedeVerFacturasRole, "puedeVerFacturas"},
            {PuedeReintentarRole, "puedeReintentar"},
            {PuedeVerDetallesRole, "puedeVerDetalles"},
            {EligeDireccionRole, "eligeDireccion"},
            {NombreAccesibleRole, "nombreAccesible"}};
}

const FilaHistorial* HistorialListModel::filaEn(int fila) const
{
    return fila >= 0 && fila < m_filas.size() ? &m_filas.at(fila) : nullptr;
}

void HistorialListModel::reemplazar(QList<FilaHistorial> filas)
{
    const qsizetype antes = m_filas.size();
    QSet<QString> nuevas;
    for (const FilaHistorial& f : std::as_const(filas)) {
        nuevas.insert(f.clave);
    }
    // 1. Quita las filas que ya no estan.
    for (qsizetype i = m_filas.size() - 1; i >= 0; --i) {
        if (!nuevas.contains(m_filas.at(i).clave)) {
            beginRemoveRows(QModelIndex(), int(i), int(i));
            m_filas.removeAt(i);
            endRemoveRows();
        }
    }
    // 2. Recorre el orden nuevo: conserva, mueve o inserta.
    for (qsizetype i = 0; i < filas.size(); ++i) {
        const FilaHistorial& nueva = filas.at(i);
        qsizetype actual = -1;
        for (qsizetype j = i; j < m_filas.size(); ++j) {
            if (m_filas.at(j).clave == nueva.clave) {
                actual = j;
                break;
            }
        }
        if (actual < 0) {
            beginInsertRows(QModelIndex(), int(i), int(i));
            m_filas.insert(i, nueva);
            endInsertRows();
            continue;
        }
        if (actual != i) {
            beginMoveRows(QModelIndex(), int(actual), int(actual), QModelIndex(), int(i));
            m_filas.move(actual, i);
            endMoveRows();
        }
        if (!(m_filas.at(i) == nueva)) {
            m_filas[i] = nueva;
            const QModelIndex indice = index(int(i));
            emit dataChanged(indice, indice);
        }
    }
    if (m_filas.size() != antes) {
        emit countChanged();
    }
}

} // namespace satcfdi
