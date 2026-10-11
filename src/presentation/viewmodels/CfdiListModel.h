#pragma once

#include "ports/repositories/CfdiCatalogRepository.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

// Fila del explorador ya lista para la UI (T015.3). Contiene datos fiscales
// (RFC, nombres, montos): nunca va a logs (T015 D11).
struct FilaCfdi {
    QString paqueteId;
    int posicion = 0;
    QString fecha;           // legible ("3 sep 2026"); "" si falta o no es valida
    QString fechaIso;        // texto del atributo Fecha
    QString tipo;            // clave de una letra ("I", "E", ...); "" desconocido
    QString tipoTexto;       // "Ingreso", "Egreso", ..., "Sin tipo"
    QString serieFolio;      // "A-123", "123" o ""
    QString emisorRfc;
    QString emisorNombre;
    QString receptorRfc;
    QString receptorNombre;
    QString total;           // texto exacto del XML; "" si falta
    QString moneda;
    QString metodoPago;
    QString uuid;            // completo (para copiar); "" si falta
    QString uuidCorto;       // primeros 8 caracteres
    QString estadoParseo;    // Completo | Parcial | VersionNoSoportada | Ilegible
    QString estadoParseoTexto;
    QString nombreAccesible; // "tipo, fecha, emisor, total"

    friend bool operator==(const FilaCfdi&, const FilaCfdi&) = default;

    static FilaCfdi desde(const CfdiListado& listado);
};

// Lista de CFDI del explorador (T015.3). La identidad de una fila es
// (paqueteId, posicion), nunca el indice. reemplazar() aplica la lista nueva
// con cambios minimos (filas quitadas, insertadas o con datos cambiados) y
// nunca reinicia el modelo: la vista conserva desplazamiento y seleccion al
// recargar (sin parpadeo).
class CfdiListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea ExploradorCfdiViewModel.")

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
        EmisorRfcRole,
        EmisorNombreRole,
        ReceptorRfcRole,
        ReceptorNombreRole,
        TotalRole,
        MonedaRole,
        MetodoPagoRole,
        UuidRole,
        UuidCortoRole,
        EstadoParseoRole,
        EstadoParseoTextoRole,
        NombreAccesibleRole,
    };
    Q_ENUM(Rol)

    explicit CfdiListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_filas.size()); }
    const QList<FilaCfdi>& filas() const { return m_filas; }
    // Fila `fila` o nullptr si no existe.
    const FilaCfdi* filaEn(int fila) const;
    // UUID completo de la fila (para copiar); "" si no hay.
    Q_INVOKABLE QString uuidEn(int fila) const;

    void reemplazar(QList<FilaCfdi> nuevas);
    void limpiar();

signals:
    void countChanged();

private:
    QList<FilaCfdi> m_filas;
};

} // namespace satcfdi
