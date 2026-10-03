#pragma once

#include "application/profiles/PerfilConPreparacion.h"

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace satcfdi {

// Lista de perfiles SAT no eliminados (activos e inactivos) para
// PerfilesSatPage (T005.1, contrato de presentacion). Roles EXACTOS:
//   id (UUID texto), rfc, nombre, activo, preparacion (clave estable de
//   PreparacionPerfil), listoParaSolicitudes (derivado por aplicacion; QML no
//   lo recalcula), verificando, estadoTexto (texto visible de la
//   preparacion, no depende del color) y vigenteHasta (QDateTime UTC o null).
// Sin rutas, contrasenas ni datos del certificado. La identidad es `id`,
// nunca el indice. Lo alimenta PerfilesSatViewModel (no invocable desde QML).
class PerfilesSatListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo expone PerfilesSatViewModel.perfiles.")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Rol {
        IdRole = Qt::UserRole + 1,
        RfcRole,
        NombreRole,
        ActivoRole,
        PreparacionRole,
        ListoParaSolicitudesRole,
        VerificandoRole,
        EstadoTextoRole,
        VigenteHastaRole,
    };
    Q_ENUM(Rol)

    // Texto visible por preparacion (fijo, sin datos variables).
    static QString textoDe(PreparacionPerfil preparacion);

    explicit PerfilesSatListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_perfiles.size()); }

    // Fila del perfil con ese id (UUID texto) o -1.
    Q_INVOKABLE int filaDe(const QString& id) const;
    // Id de la fila `fila` o "".
    Q_INVOKABLE QString idEn(int fila) const;

    // Uso interno de PerfilesSatViewModel.
    void reemplazar(QList<PerfilConPreparacion> perfiles);
    // Actualiza la fila del mismo id; false si ya no esta en la lista.
    bool actualizar(const PerfilConPreparacion& perfil);
    std::optional<PerfilConPreparacion> perfil(const QString& id) const;
    const QList<PerfilConPreparacion>& perfiles() const noexcept { return m_perfiles; }

signals:
    void countChanged();

private:
    QList<PerfilConPreparacion> m_perfiles;
};

} // namespace satcfdi
