#include "FacturasListModel.h"

#include <QStringList>

#include <algorithm>

namespace satcfdi {

namespace {

bool mismaIdentidad(const FilaFactura& a, const FilaFactura& b)
{
    return a.cfdi.paqueteId == b.cfdi.paqueteId && a.cfdi.posicion == b.cfdi.posicion;
}

} // namespace

QString formatearTotalFactura(const QString& total)
{
    const QString t = total.trimmed();
    if (t.isEmpty()) {
        return {};
    }
    const bool negativo = t.startsWith(QLatin1Char('-'));
    const QString sinSigno = negativo || t.startsWith(QLatin1Char('+')) ? t.mid(1) : t;
    const qsizetype punto = sinSigno.indexOf(QLatin1Char('.'));
    const QString entero = punto < 0 ? sinSigno : sinSigno.left(punto);
    const QString decimales = punto < 0 ? QString() : sinSigno.mid(punto); // incluye el punto
    auto soloDigitos = [](const QString& s) {
        return !s.isEmpty() && std::all_of(s.cbegin(), s.cend(), [](QChar c) { return c.isDigit(); });
    };
    if (!soloDigitos(entero) || (!decimales.isEmpty() && decimales.size() > 1 && !soloDigitos(decimales.mid(1)))) {
        return t;
    }
    QString agrupado;
    for (qsizetype i = 0; i < entero.size(); ++i) {
        if (i > 0 && (entero.size() - i) % 3 == 0) {
            agrupado.append(QLatin1Char(','));
        }
        agrupado.append(entero.at(i));
    }
    return (negativo ? QStringLiteral("-$") : QStringLiteral("$")) + agrupado + decimales;
}

FacturasListModel::FacturasListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int FacturasListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_filas.size());
}

QVariant FacturasListModel::data(const QModelIndex& index, int role) const
{
    const FilaFactura* f = index.isValid() ? filaEn(index.row()) : nullptr;
    if (!f) {
        return {};
    }
    const FilaCfdi& c = f->cfdi;
    switch (role) {
    case PaqueteIdRole: return c.paqueteId;
    case PosicionRole: return c.posicion;
    case FechaRole: return c.fecha;
    case FechaIsoRole: return c.fechaIso;
    case TipoRole: return c.tipo;
    case TipoTextoRole: return c.tipoTexto;
    case SerieFolioRole: return c.serieFolio;
    case Qt::DisplayRole:
    case ContraparteRole: return f->contraparte;
    case ContraparteRfcRole: return f->contraparteRfc;
    case ContraparteNombreRole: return f->contraparteNombre;
    case TotalRole: return f->totalTexto;
    case TotalXmlRole: return c.total;
    case MonedaRole: return c.moneda;
    case MetodoPagoRole: return c.metodoPago;
    case DetalleTotalRole: return f->detalleTotal;
    case UuidRole: return c.uuid;
    case UuidCortoRole: return c.uuidCorto;
    case EstadoParseoRole: return c.estadoParseo;
    case EstadoParseoTextoRole: return c.estadoParseoTexto;
    case NombreAccesibleRole: return f->nombreAccesible;
    default: return {};
    }
}

QHash<int, QByteArray> FacturasListModel::roleNames() const
{
    return {{PaqueteIdRole, "paqueteId"},
            {PosicionRole, "posicion"},
            {FechaRole, "fecha"},
            {FechaIsoRole, "fechaIso"},
            {TipoRole, "tipo"},
            {TipoTextoRole, "tipoTexto"},
            {SerieFolioRole, "serieFolio"},
            {ContraparteRole, "contraparte"},
            {ContraparteRfcRole, "contraparteRfc"},
            {ContraparteNombreRole, "contraparteNombre"},
            {TotalRole, "total"},
            {TotalXmlRole, "totalXml"},
            {MonedaRole, "moneda"},
            {MetodoPagoRole, "metodoPago"},
            {DetalleTotalRole, "detalleTotal"},
            {UuidRole, "uuid"},
            {UuidCortoRole, "uuidCorto"},
            {EstadoParseoRole, "estadoParseo"},
            {EstadoParseoTextoRole, "estadoParseoTexto"},
            {NombreAccesibleRole, "nombreAccesible"}};
}

const FilaFactura* FacturasListModel::filaEn(int fila) const
{
    return fila >= 0 && fila < m_filas.size() ? &m_filas.at(fila) : nullptr;
}

QString FacturasListModel::uuidEn(int fila) const
{
    const FilaFactura* f = filaEn(fila);
    return f ? f->cfdi.uuid : QString();
}

void FacturasListModel::reemplazar(QList<FilaFactura> nuevas)
{
    // Mismo algoritmo que CfdiListModel::reemplazar: prefijo y sufijo con la
    // misma identidad; el tramo intermedio se quita y se inserta.
    const int anterior = count();
    qsizetype inicio = 0;
    while (inicio < m_filas.size() && inicio < nuevas.size() && mismaIdentidad(m_filas.at(inicio), nuevas.at(inicio))) {
        ++inicio;
    }
    qsizetype finViejo = m_filas.size();
    qsizetype finNuevo = nuevas.size();
    while (finViejo > inicio && finNuevo > inicio && mismaIdentidad(m_filas.at(finViejo - 1), nuevas.at(finNuevo - 1))) {
        --finViejo;
        --finNuevo;
    }
    if (finViejo > inicio) {
        beginRemoveRows({}, int(inicio), int(finViejo - 1));
        m_filas.remove(inicio, finViejo - inicio);
        endRemoveRows();
    }
    if (finNuevo > inicio) {
        beginInsertRows({}, int(inicio), int(finNuevo - 1));
        for (qsizetype i = inicio; i < finNuevo; ++i) {
            m_filas.insert(i, nuevas.at(i));
        }
        endInsertRows();
    }
    for (qsizetype i = 0; i < nuevas.size(); ++i) {
        if (i >= inicio && i < finNuevo) {
            continue;
        }
        if (!(m_filas.at(i) == nuevas.at(i))) {
            m_filas[i] = nuevas.at(i);
            const QModelIndex indice = index(int(i));
            emit dataChanged(indice, indice);
        }
    }
    if (count() != anterior) {
        emit countChanged();
    }
}

void FacturasListModel::limpiar()
{
    reemplazar({});
}

} // namespace satcfdi
