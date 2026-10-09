#include "PerfilesSatListModel.h"

#include <QDateTime>

#include <utility>

namespace satcfdi {

QString PerfilesSatListModel::textoDe(PreparacionPerfil preparacion)
{
    switch (preparacion) {
    case PreparacionPerfil::Verificando:
        return tr("Verificando");
    case PreparacionPerfil::SinCredencial:
        return tr("Sin e.firma");
    case PreparacionPerfil::Lista:
        return tr("e.firma lista");
    case PreparacionPerfil::Vencida:
        return tr("e.firma vencida");
    case PreparacionPerfil::NoVigenteAun:
        return tr("e.firma aún no vigente");
    case PreparacionPerfil::MaterialFaltante:
        return tr("e.firma incompleta en el llavero");
    case PreparacionPerfil::MaterialDanado:
        return tr("e.firma dañada en el llavero");
    case PreparacionPerfil::EstadoNoDisponible:
        return tr("Estado no disponible");
    }
    return tr("Estado no disponible");
}

PerfilesSatListModel::PerfilesSatListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int PerfilesSatListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_perfiles.size());
}

QVariant PerfilesSatListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const PerfilConPreparacion& p = m_perfiles.at(index.row());
    switch (role) {
    case IdRole:
        return p.perfil.id.texto();
    case RfcRole:
        return p.perfil.rfc;
    case NombreRole:
    case Qt::DisplayRole:
        return p.perfil.nombre;
    case ActivoRole:
        return p.perfil.activo;
    case PreparacionRole:
        return claveEstable(p.preparacion);
    case ListoParaSolicitudesRole:
        return p.listoParaSolicitudes;
    case VerificandoRole:
        return p.preparacion == PreparacionPerfil::Verificando;
    case EstadoTextoRole:
        return textoDe(p.preparacion);
    case VigenteHastaRole:
        return p.vigenteHasta ? QVariant(*p.vigenteHasta) : QVariant::fromValue(nullptr);
    default:
        return {};
    }
}

QHash<int, QByteArray> PerfilesSatListModel::roleNames() const
{
    return {
        {IdRole, "id"},
        {RfcRole, "rfc"},
        {NombreRole, "nombre"},
        {ActivoRole, "activo"},
        {PreparacionRole, "preparacion"},
        {ListoParaSolicitudesRole, "listoParaSolicitudes"},
        {VerificandoRole, "verificando"},
        {EstadoTextoRole, "estadoTexto"},
        {VigenteHastaRole, "vigenteHasta"},
    };
}

int PerfilesSatListModel::filaDe(const QString& id) const
{
    for (qsizetype i = 0; i < m_perfiles.size(); ++i) {
        if (m_perfiles.at(i).perfil.id.texto() == id) {
            return int(i);
        }
    }
    return -1;
}

QString PerfilesSatListModel::idEn(int fila) const
{
    return (fila >= 0 && fila < m_perfiles.size()) ? m_perfiles.at(fila).perfil.id.texto() : QString();
}

void PerfilesSatListModel::reemplazar(QList<PerfilConPreparacion> perfiles)
{
    const int previo = count();
    beginResetModel();
    m_perfiles = std::move(perfiles);
    endResetModel();
    if (previo != count()) {
        emit countChanged();
    }
}

bool PerfilesSatListModel::actualizar(const PerfilConPreparacion& perfil)
{
    const int fila = filaDe(perfil.perfil.id.texto());
    if (fila < 0) {
        return false;
    }
    if (m_perfiles.at(fila) == perfil) {
        return true;
    }
    m_perfiles[fila] = perfil;
    const QModelIndex i = index(fila);
    emit dataChanged(i, i);
    return true;
}

std::optional<PerfilConPreparacion> PerfilesSatListModel::perfil(const QString& id) const
{
    const int fila = filaDe(id);
    if (fila < 0) {
        return std::nullopt;
    }
    return m_perfiles.at(fila);
}

} // namespace satcfdi
