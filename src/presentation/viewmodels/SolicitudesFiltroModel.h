#pragma once

#include "SolicitudesListModel.h"

#include <QPointer>
#include <QSortFilterProxyModel>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

// T014.1 D1 (SUG-02): filtro local sobre SolicitudesListModel. No cambia la
// consulta ni el orden de la fuente; solo oculta filas.
//
// - texto: busca (sin distinguir mayusculas) en el RFC y el nombre del perfil.
// - estado: clave de estadoResumen ("" = todos).
// - tipo: "Emitidos" | "Recibidos" ("" = todos).
// - mes: "yyyy-MM" ("" = todos). Una solicitud entra si su periodo
//   [fechaInicial, fechaFinal] se solapa con ese mes.
// - mesesDisponibles: meses ("yyyy-MM", del mas reciente al mas antiguo) que
//   tocan los periodos de la fuente, para el selector de mes.
//
// El estado de los filtros no vive aqui: SolicitudesPage enlaza estas
// propiedades a las de AppViewModel (se conservan durante la sesion).
class SolicitudesFiltroModel : public QSortFilterProxyModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(satcfdi::SolicitudesListModel* fuente READ fuente WRITE setFuente NOTIFY fuenteChanged)
    Q_PROPERTY(QString texto READ texto WRITE setTexto NOTIFY filtrosChanged)
    Q_PROPERTY(QString estado READ estado WRITE setEstado NOTIFY filtrosChanged)
    Q_PROPERTY(QString tipo READ tipo WRITE setTipo NOTIFY filtrosChanged)
    Q_PROPERTY(QString mes READ mes WRITE setMes NOTIFY filtrosChanged)
    Q_PROPERTY(bool hayFiltros READ hayFiltros NOTIFY filtrosChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QStringList mesesDisponibles READ mesesDisponibles NOTIFY mesesDisponiblesChanged)

public:
    explicit SolicitudesFiltroModel(QObject* parent = nullptr);

    SolicitudesListModel* fuente() const { return m_fuente; }
    void setFuente(SolicitudesListModel* fuente);

    QString texto() const { return m_texto; }
    void setTexto(const QString& valor);
    QString estado() const { return m_estado; }
    void setEstado(const QString& valor);
    QString tipo() const { return m_tipo; }
    void setTipo(const QString& valor);
    QString mes() const { return m_mes; }
    void setMes(const QString& valor);

    bool hayFiltros() const;
    int count() const { return rowCount(); }
    QStringList mesesDisponibles() const { return m_meses; }

    // Id (UUID texto) de la fila visible `fila`, o "".
    Q_INVOKABLE QString idEn(int fila) const;

signals:
    void fuenteChanged();
    void filtrosChanged();
    void countChanged();
    void mesesDisponiblesChanged();

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    // Cambia un filtro y vuelve a filtrar las filas.
    void cambiarFiltro(QString& campo, const QString& valor);
    void recalcularMeses();

    QPointer<SolicitudesListModel> m_fuente;
    QString m_texto;
    QString m_estado;
    QString m_tipo;
    QString m_mes;
    QStringList m_meses;
};

} // namespace satcfdi
