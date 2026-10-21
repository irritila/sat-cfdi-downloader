#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

// Fila del Historial (T023.5, D6/D14). Todos los textos vienen de C++ y no
// contienen "solicitud", "paquete" ni "verificacion"; sin IdSolicitud,
// IdPaquete, numero de paquetes ni codigos SAT.
struct FilaHistorial {
    QString clave;            // identidad estable de la fila (opaca para QML)
    int anio = 0;             // 0 si la fila no corresponde a un solo mes
    int mes = 0;              // 1..12; 0 si no corresponde a un solo mes
    QString titulo;           // "Septiembre 2026" o "1 ene 2026 – 15 mar 2026" (anterior multi-mes)
    QString fecha;            // "4 oct 2026, 09:12" (hora Centro)
    QString textoFecha;       // "Descarga del 4 oct 2026, 09:12"
    QString resultado;        // clave: Completa | EnPreparacion | RequiereAtencion |
                              // SinFacturas | NoCompletada | Eliminada | Anterior
    QString resultadoTexto;   // "Completa", "En preparación", "Requiere atención",
                              // "Sin facturas en el SAT", "Cubierta por otra descarga",
                              // "Eliminada", "Descarga anterior"
    QString tono;             // exito | progreso | error | neutro | aviso
    QString icono;            // nombre de assets/icons sin .svg
    QString textoConteos;     // "12 emitidas, 40 recibidas" | "El SAT no tenía facturas de este mes" |
                              // "Solo emitidas: 3" (anterior de una direccion) | ""
    QString detalle;          // "Parte de esta descarga se eliminó" | "Hecha con una versión anterior"
                              // (", con filtros") | "Una descarga posterior obtuvo estas facturas" |
                              // "Esta descarga se eliminó de este equipo" | ""
    bool eliminacionParcial = false;
    bool anterior = false;    // "Descarga anterior": solo diagnostico
    bool puedeVerFacturas = false;
    bool puedeReintentar = false;
    // Ver detalles tecnicos: disponible si hay alguna direccion con
    // diagnostico; `eligeDireccion` si hay ambas (QML pregunta Emitidas o
    // Recibidas antes de llamar verDetallesTecnicos).
    bool puedeVerDetalles = false;
    bool eligeDireccion = false;
    QString nombreAccesible;  // "Septiembre 2026, Completa, descarga del 4 oct 2026, 09:12, 12 emitidas, 40 recibidas"

    friend bool operator==(const FilaHistorial&, const FilaHistorial&) = default;
};

// Filas del historial, mas reciente primero. reemplazar() conserva las filas
// iguales (dataChanged / insercion / eliminacion por clave) para no perder el
// foco del teclado al recargar.
class HistorialListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo expone HistorialViewModel.modelo.")

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Rol {
        ClaveRole = Qt::UserRole + 1,
        AnioRole,
        MesRole,
        TituloRole,
        FechaRole,
        TextoFechaRole,
        ResultadoRole,
        ResultadoTextoRole,
        TonoRole,
        IconoRole,
        TextoConteosRole,
        DetalleRole,
        EliminacionParcialRole,
        AnteriorRole,
        PuedeVerFacturasRole,
        PuedeReintentarRole,
        PuedeVerDetallesRole,
        EligeDireccionRole,
        NombreAccesibleRole,
    };
    Q_ENUM(Rol)

    explicit HistorialListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_filas.size()); }
    const QList<FilaHistorial>& filas() const { return m_filas; }
    const FilaHistorial* filaEn(int fila) const;

    void reemplazar(QList<FilaHistorial> filas);

signals:
    void countChanged();

private:
    QList<FilaHistorial> m_filas;
};

} // namespace satcfdi
