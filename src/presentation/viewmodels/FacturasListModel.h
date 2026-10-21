#pragma once

#include "CfdiListModel.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

// Fila de la lista de Facturas (T023.4, D20): el CFDI de la pestana activa
// con SOLO su contraparte (Receptor en Emitidas, Emisor en Recibidas).
// Contiene datos fiscales: nunca va a logs.
struct FilaFactura {
    FilaCfdi cfdi;              // datos del CFDI (FilaCfdi sigue sirviendo a Diagnostico)
    QString solicitudId;        // fuente canonica (para el PDF); no se muestra
    QString contraparteRfc;
    QString contraparteNombre;  // "" si falta (se muestra el RFC)
    QString contraparte;        // texto visible: nombre o, si falta, RFC
    QString totalTexto;         // "$1,200.10": miles con coma, decimales tal cual el XML; "" si falta
    QString detalleTotal;       // "MXN · PUE" (moneda · metodo de pago; omite los que falten)
    QString nombreAccesible;    // "tipo, fecha, contraparte, total"

    friend bool operator==(const FilaFactura&, const FilaFactura&) = default;
};

// Total del XML con separador de miles y signo de pesos, sin redondear:
// "1200.10" -> "$1,200.10", "-5" -> "-$5"; "" si `total` esta vacio. Si el
// texto no es un decimal simple se devuelve tal cual.
QString formatearTotalFactura(const QString& total);

// Lista de facturas de la pestana activa. Identidad de fila: (paqueteId,
// posicion). reemplazar() aplica cambios minimos sin reiniciar el modelo (la
// lista se llena durante la indexacion sin parpadeo).
class FacturasListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo expone FacturasViewModel.modelo.")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Rol {
        PaqueteIdRole = Qt::UserRole + 1,
        PosicionRole,
        FechaRole,
        FechaIsoRole,
        TipoRole,
        TipoTextoRole,
        SerieFolioRole,
        ContraparteRole,
        ContraparteRfcRole,
        ContraparteNombreRole,
        TotalRole,          // totalTexto (formateado)
        TotalXmlRole,       // texto exacto del XML
        MonedaRole,
        MetodoPagoRole,
        DetalleTotalRole,   // "MXN · PUE"
        UuidRole,
        UuidCortoRole,
        EstadoParseoRole,
        EstadoParseoTextoRole,
        NombreAccesibleRole,
    };
    Q_ENUM(Rol)

    explicit FacturasListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_filas.size()); }
    const QList<FilaFactura>& filas() const { return m_filas; }
    const FilaFactura* filaEn(int fila) const;
    // UUID completo de la fila (Cmd+C); "" si no hay.
    Q_INVOKABLE QString uuidEn(int fila) const;

    void reemplazar(QList<FilaFactura> nuevas);
    void limpiar();

signals:
    void countChanged();

private:
    QList<FilaFactura> m_filas;
};

} // namespace satcfdi
