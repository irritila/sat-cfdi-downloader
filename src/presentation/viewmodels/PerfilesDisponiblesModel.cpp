#include "PerfilesDisponiblesModel.h"

#include <utility>

namespace satcfdi {

PerfilesDisponiblesModel::PerfilesDisponiblesModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PerfilesDisponiblesModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_perfiles.size());
}

QVariant PerfilesDisponiblesModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const PerfilResumen& p = m_perfiles.at(index.row());
    switch (role) {
    case IdRole:
        return p.id.texto();
    case RfcRole:
        return p.rfc;
    case NombreRole:
        return p.nombre;
    case EtiquetaRole:
    case Qt::DisplayRole:
        return p.nombre.isEmpty() ? p.rfc
                                       : QStringLiteral("%1 — %2").arg(p.rfc, p.nombre);
    default:
        return {};
    }
}

QHash<int, QByteArray> PerfilesDisponiblesModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {RfcRole, "rfc"},
        {NombreRole, "nombre"},
        {EtiquetaRole, "etiqueta"},
    };
}

int PerfilesDisponiblesModel::filaDe(const QString& id) const
{
    for (qsizetype i = 0; i < m_perfiles.size(); ++i) {
        if (m_perfiles.at(i).id.texto() == id) {
            return int(i);
        }
    }
    return -1;
}

void PerfilesDisponiblesModel::reemplazar(QList<PerfilResumen> perfiles)
{
    const int previo = count();
    beginResetModel();
    m_perfiles = std::move(perfiles);
    endResetModel();
    if (previo != count()) {
        emit countChanged();
    }
}

} // namespace satcfdi
