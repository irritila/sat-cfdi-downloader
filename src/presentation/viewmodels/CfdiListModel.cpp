#include "CfdiListModel.h"

#include "FormatoFechas.h"

#include <QDate>

namespace satcfdi {

namespace {

QString textoTipo(TipoComprobante tipo)
{
    switch (tipo) {
    case TipoComprobante::Ingreso:
        return QObject::tr("Ingreso");
    case TipoComprobante::Egreso:
        return QObject::tr("Egreso");
    case TipoComprobante::Traslado:
        return QObject::tr("Traslado");
    case TipoComprobante::Nomina:
        return QObject::tr("Nómina");
    case TipoComprobante::Pago:
        return QObject::tr("Pago");
    case TipoComprobante::Desconocido:
        break;
    }
    return QObject::tr("Sin tipo");
}

QString claveEstado(EstadoParseo estado)
{
    switch (estado) {
    case EstadoParseo::Completo:
        return QStringLiteral("Completo");
    case EstadoParseo::Parcial:
        return QStringLiteral("Parcial");
    case EstadoParseo::VersionNoSoportada:
        return QStringLiteral("VersionNoSoportada");
    case EstadoParseo::NoEsCfdi: // no se indexa; por completitud
    case EstadoParseo::Ilegible:
        break;
    }
    return QStringLiteral("Ilegible");
}

QString textoEstado(EstadoParseo estado)
{
    switch (estado) {
    case EstadoParseo::Completo:
        return QObject::tr("Completo");
    case EstadoParseo::Parcial:
        return QObject::tr("Parcial");
    case EstadoParseo::VersionNoSoportada:
        return QObject::tr("Versión no soportada");
    case EstadoParseo::NoEsCfdi:
    case EstadoParseo::Ilegible:
        break;
    }
    return QObject::tr("Ilegible");
}

} // namespace

FilaCfdi FilaCfdi::desde(const CfdiListado& listado)
{
    const ResumenCfdi& r = listado.resumen;
    FilaCfdi f;
    f.paqueteId = listado.paqueteId;
    f.posicion = listado.posicion;
    f.fechaIso = r.fecha;
    // Fecha local del XML (sin zona): se muestra el dia tal cual, sin
    // conversion de zona (FormatoFechas, T013 D5).
    f.fecha = FormatoFechas::fecha(QDate::fromString(r.fecha.left(10), Qt::ISODate));
    f.tipo = QString(codigoTipoComprobante(r.tipo));
    f.tipoTexto = textoTipo(r.tipo);
    if (!r.serie.isEmpty() && !r.folio.isEmpty()) {
        f.serieFolio = r.serie + QLatin1Char('-') + r.folio;
    } else {
        f.serieFolio = r.serie + r.folio;
    }
    f.emisorRfc = r.emisor.rfc;
    f.emisorNombre = r.emisor.nombre;
    f.receptorRfc = r.receptor.rfc;
    f.receptorNombre = r.receptor.nombre;
    f.total = r.total ? r.total->texto().trimmed() : QString();
    f.moneda = r.moneda;
    f.metodoPago = r.metodoPago;
    f.uuid = r.uuid;
    f.uuidCorto = r.uuid.left(8);
    f.estadoParseo = claveEstado(r.estado);
    f.estadoParseoTexto = textoEstado(r.estado);

    const QString emisor = !f.emisorNombre.isEmpty() && !f.emisorRfc.isEmpty()
                               ? QStringLiteral("%1 %2").arg(f.emisorNombre, f.emisorRfc)
                               : (!f.emisorNombre.isEmpty() ? f.emisorNombre
                                  : !f.emisorRfc.isEmpty() ? f.emisorRfc
                                                           : QObject::tr("emisor desconocido"));
    const QString total = f.total.isEmpty()
                              ? QObject::tr("sin total")
                              : (f.moneda.isEmpty() ? f.total : QStringLiteral("%1 %2").arg(f.total, f.moneda));
    f.nombreAccesible = QStringLiteral("%1, %2, %3, %4")
                            .arg(f.tipoTexto, f.fecha.isEmpty() ? QObject::tr("sin fecha") : f.fecha, emisor, total);
    if (r.estado != EstadoParseo::Completo) {
        f.nombreAccesible += QStringLiteral(", ") + f.estadoParseoTexto.toLower();
    }
    return f;
}

CfdiListModel::CfdiListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int CfdiListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_filas.size());
}

QVariant CfdiListModel::data(const QModelIndex& index, int role) const
{
    const FilaCfdi* f = filaEn(index.isValid() ? index.row() : -1);
    if (!f) {
        return {};
    }
    switch (role) {
    case PaqueteIdRole:
        return f->paqueteId;
    case PosicionRole:
        return f->posicion;
    case FechaRole:
        return f->fecha;
    case FechaIsoRole:
        return f->fechaIso;
    case TipoRole:
        return f->tipo;
    case TipoTextoRole:
        return f->tipoTexto;
    case SerieFolioRole:
        return f->serieFolio;
    case EmisorRfcRole:
        return f->emisorRfc;
    case EmisorNombreRole:
        return f->emisorNombre;
    case ReceptorRfcRole:
        return f->receptorRfc;
    case ReceptorNombreRole:
        return f->receptorNombre;
    case TotalRole:
        return f->total;
    case MonedaRole:
        return f->moneda;
    case MetodoPagoRole:
        return f->metodoPago;
    case UuidRole:
        return f->uuid;
    case UuidCortoRole:
        return f->uuidCorto;
    case EstadoParseoRole:
        return f->estadoParseo;
    case EstadoParseoTextoRole:
        return f->estadoParseoTexto;
    case Qt::DisplayRole:
    case NombreAccesibleRole:
        return f->nombreAccesible;
    default:
        return {};
    }
}

QHash<int, QByteArray> CfdiListModel::roleNames() const
{
    return {
        {PaqueteIdRole, "paqueteId"},
        {PosicionRole, "posicion"},
        {FechaRole, "fecha"},
        {FechaIsoRole, "fechaIso"},
        {TipoRole, "tipo"},
        {TipoTextoRole, "tipoTexto"},
        {SerieFolioRole, "serieFolio"},
        {EmisorRfcRole, "emisorRfc"},
        {EmisorNombreRole, "emisorNombre"},
        {ReceptorRfcRole, "receptorRfc"},
        {ReceptorNombreRole, "receptorNombre"},
        {TotalRole, "total"},
        {MonedaRole, "moneda"},
        {MetodoPagoRole, "metodoPago"},
        {UuidRole, "uuid"},
        {UuidCortoRole, "uuidCorto"},
        {EstadoParseoRole, "estadoParseo"},
        {EstadoParseoTextoRole, "estadoParseoTexto"},
        {NombreAccesibleRole, "nombreAccesible"},
    };
}

const FilaCfdi* CfdiListModel::filaEn(int fila) const
{
    return fila >= 0 && fila < m_filas.size() ? &m_filas.at(fila) : nullptr;
}

QString CfdiListModel::uuidEn(int fila) const
{
    const FilaCfdi* f = filaEn(fila);
    return f ? f->uuid : QString();
}

namespace {

bool mismaIdentidad(const FilaCfdi& a, const FilaCfdi& b)
{
    return a.posicion == b.posicion && a.paqueteId == b.paqueteId;
}

} // namespace

void CfdiListModel::reemplazar(QList<FilaCfdi> nuevas)
{
    const int anterior = count();
    // Prefijo y sufijo con la misma identidad; el tramo intermedio se quita y
    // se inserta. Sin beginResetModel: la vista conserva su estado.
    qsizetype inicio = 0;
    while (inicio < m_filas.size() && inicio < nuevas.size() && mismaIdentidad(m_filas.at(inicio), nuevas.at(inicio))) {
        ++inicio;
    }
    qsizetype finViejo = m_filas.size();
    qsizetype finNuevo = nuevas.size();
    while (finViejo > inicio && finNuevo > inicio
           && mismaIdentidad(m_filas.at(finViejo - 1), nuevas.at(finNuevo - 1))) {
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
    // Prefijo y sufijo: misma identidad, datos posiblemente cambiados.
    Q_ASSERT(m_filas.size() == nuevas.size());
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

void CfdiListModel::limpiar()
{
    reemplazar({});
}

} // namespace satcfdi
