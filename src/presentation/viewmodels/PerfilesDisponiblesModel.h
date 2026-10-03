#pragma once

#include "application/profiles/PerfilResumen.h"

#include <QAbstractListModel>
#include <QList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

// Modelo de solo lectura de perfiles SAT activos para el selector de
// NuevaSolicitudPage. Roles: id (UUID texto), rfc, razonSocial y etiqueta
// (texto visible "RFC - razon social"). No expone credenciales.
class PerfilesDisponiblesModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo expone NuevaSolicitudViewModel.perfilesDisponibles.")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Rol {
        IdRole = Qt::UserRole + 1,
        RfcRole,
        RazonSocialRole,
        EtiquetaRole,
    };
    Q_ENUM(Rol)

    explicit PerfilesDisponiblesModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_perfiles.size()); }

    // Fila del perfil con ese id (UUID texto) o -1.
    Q_INVOKABLE int filaDe(const QString& id) const;

    // Uso interno de NuevaSolicitudViewModel (no invocable desde QML).
    void reemplazar(QList<PerfilResumen> perfiles);
    bool contiene(const QString& id) const { return filaDe(id) >= 0; }

signals:
    void countChanged();

private:
    QList<PerfilResumen> m_perfiles;
};

} // namespace satcfdi
