#include "SolicitudesFiltroModel.h"

#include "SolicitudesListModel.h"

#include <QDate>
#include <QSet>

#include <algorithm>
#include <functional>

namespace satcfdi {

namespace {

using Rol = SolicitudesListModel::Rol;

// Primer y ultimo dia de "yyyy-MM"; fechas nulas si no es valido.
std::pair<QDate, QDate> limitesDeMes(const QString& mes)
{
    const QDate primero = QDate::fromString(mes + QStringLiteral("-01"), Qt::ISODate);
    if (!primero.isValid()) {
        return {};
    }
    return {primero, primero.addMonths(1).addDays(-1)};
}

} // namespace

SolicitudesFiltroModel::SolicitudesFiltroModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    // count y meses siguen a las filas visibles y a la fuente.
    for (const auto senal : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved}) {
        connect(this, senal, this, &SolicitudesFiltroModel::countChanged);
    }
    connect(this, &QAbstractItemModel::modelReset, this, &SolicitudesFiltroModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &SolicitudesFiltroModel::countChanged);
}

void SolicitudesFiltroModel::setFuente(SolicitudesListModel* fuente)
{
    if (m_fuente == fuente) {
        return;
    }
    if (m_fuente) {
        disconnect(m_fuente, nullptr, this, nullptr);
    }
    m_fuente = fuente;
    setSourceModel(fuente);
    if (fuente) {
        connect(fuente, &QAbstractItemModel::modelReset, this, &SolicitudesFiltroModel::recalcularMeses);
        connect(fuente, &QAbstractItemModel::rowsInserted, this, &SolicitudesFiltroModel::recalcularMeses);
        connect(fuente, &QAbstractItemModel::rowsRemoved, this, &SolicitudesFiltroModel::recalcularMeses);
        connect(fuente, &QAbstractItemModel::dataChanged, this, &SolicitudesFiltroModel::recalcularMeses);
    }
    recalcularMeses();
    emit fuenteChanged();
    emit countChanged();
}

void SolicitudesFiltroModel::cambiarFiltro(QString& campo, const QString& valor)
{
    if (campo == valor) {
        return;
    }
    beginFilterChange();
    campo = valor;
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
    emit filtrosChanged();
    emit countChanged();
}

void SolicitudesFiltroModel::setTexto(const QString& valor) { cambiarFiltro(m_texto, valor); }
void SolicitudesFiltroModel::setEstado(const QString& valor) { cambiarFiltro(m_estado, valor); }
void SolicitudesFiltroModel::setTipo(const QString& valor) { cambiarFiltro(m_tipo, valor); }
void SolicitudesFiltroModel::setMes(const QString& valor) { cambiarFiltro(m_mes, valor); }

bool SolicitudesFiltroModel::hayFiltros() const
{
    return !m_texto.trimmed().isEmpty() || !m_estado.isEmpty() || !m_tipo.isEmpty() || !m_mes.isEmpty();
}

QString SolicitudesFiltroModel::idEn(int fila) const
{
    const QModelIndex i = index(fila, 0);
    return i.isValid() ? i.data(Rol::IdRole).toString() : QString();
}

bool SolicitudesFiltroModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QAbstractItemModel* m = sourceModel();
    if (!m) {
        return false;
    }
    const QModelIndex i = m->index(sourceRow, 0, sourceParent);

    const QString buscado = m_texto.trimmed();
    if (!buscado.isEmpty()) {
        const QString rfc = i.data(Rol::PerfilRfcRole).toString();
        const QString nombre = i.data(Rol::PerfilNombreRole).toString();
        if (!rfc.contains(buscado, Qt::CaseInsensitive) && !nombre.contains(buscado, Qt::CaseInsensitive)) {
            return false;
        }
    }
    if (!m_estado.isEmpty() && i.data(Rol::EstadoResumenRole).toString() != m_estado) {
        return false;
    }
    if (!m_tipo.isEmpty() && i.data(Rol::TipoDescargaRole).toString() != m_tipo) {
        return false;
    }
    if (!m_mes.isEmpty()) {
        const auto [primero, ultimo] = limitesDeMes(m_mes);
        const QDate inicial = QDate::fromString(i.data(Rol::FechaInicialRole).toString(), Qt::ISODate);
        const QDate final_ = QDate::fromString(i.data(Rol::FechaFinalRole).toString(), Qt::ISODate);
        // Solape de [inicial, final] con [primero, ultimo].
        if (!primero.isValid() || !inicial.isValid() || !final_.isValid() || inicial > ultimo
            || final_ < primero) {
            return false;
        }
    }
    return true;
}

void SolicitudesFiltroModel::recalcularMeses()
{
    QSet<QString> meses;
    if (const QAbstractItemModel* m = sourceModel()) {
        for (int fila = 0; fila < m->rowCount(); ++fila) {
            const QModelIndex i = m->index(fila, 0);
            QDate inicial = QDate::fromString(i.data(Rol::FechaInicialRole).toString(), Qt::ISODate);
            const QDate final_ = QDate::fromString(i.data(Rol::FechaFinalRole).toString(), Qt::ISODate);
            if (!inicial.isValid() || !final_.isValid() || final_ < inicial) {
                continue;
            }
            // Cada mes que toca el periodo (con tope para periodos anomalos).
            QDate mes(inicial.year(), inicial.month(), 1);
            for (int n = 0; mes <= final_ && n < 240; ++n, mes = mes.addMonths(1)) {
                meses.insert(mes.toString(QStringLiteral("yyyy-MM")));
            }
        }
    }
    QStringList lista(meses.cbegin(), meses.cend());
    std::sort(lista.begin(), lista.end(), std::greater<>());
    if (lista != m_meses) {
        m_meses = lista;
        emit mesesDisponiblesChanged();
    }
}

} // namespace satcfdi
