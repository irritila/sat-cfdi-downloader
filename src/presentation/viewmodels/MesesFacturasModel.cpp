#include "MesesFacturasModel.h"

namespace satcfdi {

MesesFacturasModel::MesesFacturasModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MesesFacturasModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_filas.size());
}

QVariant MesesFacturasModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_filas.size()) {
        return {};
    }
    const FilaMesFacturas& f = m_filas.at(index.row());
    switch (role) {
    case AnioRole: return f.anio;
    case MesRole: return f.mes;
    case Qt::DisplayRole:
    case NombreRole: return f.nombre;
    case NombreCortoRole: return f.nombreCorto;
    case EstadoRole: return f.estado;
    case EstadoTextoRole: return f.estadoTexto;
    case TonoRole: return f.tono;
    case IconoRole: return f.icono;
    case ConteoRole: return f.conteo;
    case TextoConteoRole: return f.textoConteo;
    case FuturoRole: return f.futuro;
    case SeleccionadoRole: return f.seleccionado;
    case NombreAccesibleRole: return f.nombreAccesible;
    default: return {};
    }
}

QHash<int, QByteArray> MesesFacturasModel::roleNames() const
{
    return {{AnioRole, "anio"},
            {MesRole, "mes"},
            {NombreRole, "nombre"},
            {NombreCortoRole, "nombreCorto"},
            {EstadoRole, "estado"},
            {EstadoTextoRole, "estadoTexto"},
            {TonoRole, "tono"},
            {IconoRole, "icono"},
            {ConteoRole, "conteo"},
            {TextoConteoRole, "textoConteo"},
            {FuturoRole, "futuro"},
            {SeleccionadoRole, "seleccionado"},
            {NombreAccesibleRole, "nombreAccesible"}};
}

int MesesFacturasModel::filaDe(int mes) const
{
    for (qsizetype i = 0; i < m_filas.size(); ++i) {
        if (m_filas.at(i).mes == mes) {
            return int(i);
        }
    }
    return -1;
}

void MesesFacturasModel::reemplazar(QList<FilaMesFacturas> filas)
{
    if (filas.size() != m_filas.size()) {
        beginResetModel();
        m_filas = std::move(filas);
        endResetModel();
        emit countChanged();
        return;
    }
    for (qsizetype i = 0; i < filas.size(); ++i) {
        if (!(m_filas.at(i) == filas.at(i))) {
            m_filas[i] = filas.at(i);
            const QModelIndex indice = index(int(i));
            emit dataChanged(indice, indice);
        }
    }
}

} // namespace satcfdi
