#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

// Mes de la lista lateral (o combo bajo ~760 px) de Facturas (T023.4, D9).
struct FilaMesFacturas {
    int anio = 0;
    int mes = 0;              // 1..12
    QString nombre;           // "septiembre"
    QString nombreCorto;      // "sep"
    QString estado;           // clave: SinDescargar | Preparando | Disponibles |
                              // DisponiblesParcialmente | RequiereAtencion | Futuro | Cargando
    QString estadoTexto;      // "Disponibles", "Disponibles parcialmente", ...
    QString tono;             // neutro | info | exito | aviso | error (no unico portador del estado)
    QString icono;            // nombre simbolico del icono del estado
    int conteo = 0;           // facturas del mes que coinciden con la busqueda/filtro (ambas pestanas)
    QString textoConteo;      // "214 facturas", "1 factura", "" si futuro o desconocido
    bool futuro = false;      // el mes aun no empieza: no seleccionable
    bool seleccionado = false;
    QString nombreAccesible;  // "septiembre 2026, Disponibles, 214 facturas"

    friend bool operator==(const FilaMesFacturas&, const FilaMesFacturas&) = default;
};

// Los 12 meses del anio elegido. Filas estables (siempre 12); reemplazar()
// solo emite dataChanged de las filas que cambiaron.
class MesesFacturasModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo expone FacturasViewModel.meses.")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Rol {
        AnioRole = Qt::UserRole + 1,
        MesRole,
        NombreRole,
        NombreCortoRole,
        EstadoRole,
        EstadoTextoRole,
        TonoRole,
        IconoRole,
        ConteoRole,
        TextoConteoRole,
        FuturoRole,
        SeleccionadoRole,
        NombreAccesibleRole,
    };
    Q_ENUM(Rol)

    explicit MesesFacturasModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_filas.size()); }
    const QList<FilaMesFacturas>& filas() const { return m_filas; }
    // Fila del mes 1..12, o -1.
    Q_INVOKABLE int filaDe(int mes) const;

    void reemplazar(QList<FilaMesFacturas> filas);

signals:
    void countChanged();

private:
    QList<FilaMesFacturas> m_filas;
};

} // namespace satcfdi
