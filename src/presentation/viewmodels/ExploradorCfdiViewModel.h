#pragma once

#include "ports/repositories/CfdiCatalogRepository.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace satcfdi {

class AccionesFinder;
class CfdiListModel;
class ConsultaCatalogoCfdi;

// Explorador de CFDI de una solicitud (T015.3, T015 D1). Lista los CFDI ya
// indexados con busqueda y filtro por tipo, y el estado del indice.
//
// Estados (`estado`): Ninguno (sin solicitud), Cargando (primera carga o
// Reintentar), Error (no se pudo leer la lista o el resumen; reemplaza la
// lista) y ConDatos. `vista` resume lo que la pagina muestra, con esta
// precedencia: "error" > "cargando" > "lista" (hay filas) >
// "sinCoincidencias" (lista vacia con filtros aplicados) > "indexando" (lista
// vacia mientras se indexa; sin estado vacio) > "sinFacturas".
//
// Lineas de estado (`lineasEstado`, sobre la lista, en este orden y solo si
// su conteo es mayor que cero): indexando N paquetes, N paquetes no
// disponibles (ZIP faltante), N paquetes ilegibles y N CFDI ilegibles o no
// soportados. Cada una: {clave, conteo, texto, accionCarpeta}; con
// accionCarpeta la pagina ofrece "Abrir carpeta de la solicitud" (T009.1).
//
// Recarga: abrir(id) revalida las huellas (revisarSolicitud) y carga lista y
// resumen. indiceCambiado(id) de la solicitud abierta recarga ambos sin
// volver a Cargando ni vaciar la lista (sin parpadeo; `actualizando`);
// actividadCambiada() recarga solo el resumen. La busqueda se aplica con un
// retardo corto (`intervaloBusqueda`, 250 ms) y el filtro de tipo al
// instante; tokens de generacion: la ultima respuesta gana.
//
// previsualizar(fila) solo expone la intencion (T015.4 la atiende).
class ExploradorCfdiViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea PresentacionViewModels.")

    Q_PROPERTY(QString solicitudId READ solicitudId NOTIFY solicitudIdChanged)
    Q_PROPERTY(satcfdi::CfdiListModel* modelo READ modelo CONSTANT)
    Q_PROPERTY(Estado estado READ estado NOTIFY estadoChanged)
    Q_PROPERTY(QString vista READ vista NOTIFY vistaChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY estadoChanged)
    Q_PROPERTY(bool actualizando READ actualizando NOTIFY actualizandoChanged)
    Q_PROPERTY(QString texto READ texto WRITE setTexto NOTIFY filtrosChanged)
    Q_PROPERTY(QString tipo READ tipo WRITE setTipo NOTIFY filtrosChanged)
    Q_PROPERTY(bool hayFiltros READ hayFiltros NOTIFY filtrosChanged)
    Q_PROPERTY(int totalCfdi READ totalCfdi NOTIFY resumenChanged)
    Q_PROPERTY(bool indexando READ indexando NOTIFY resumenChanged)
    Q_PROPERTY(QVariantList lineasEstado READ lineasEstado NOTIFY resumenChanged)
    Q_PROPERTY(bool puedeAbrirCarpeta READ puedeAbrirCarpeta NOTIFY mensajeFinderChanged)
    Q_PROPERTY(QString mensajeFinder READ mensajeFinder NOTIFY mensajeFinderChanged)

public:
    enum class Estado {
        Ninguno,
        Cargando,
        Error,
        ConDatos,
    };
    Q_ENUM(Estado)

    static constexpr int kIntervaloBusquedaMs = 250;

    explicit ExploradorCfdiViewModel(QObject* parent = nullptr);

    // No propietarios; nullptr = sin consulta (lista vacia) o sin Finder.
    void setConsulta(ConsultaCatalogoCfdi* consulta);
    void setAccionesFinder(AccionesFinder* acciones);
    // Retardo de la busqueda (pruebas: 0).
    void setIntervaloBusqueda(int ms) { m_retardo.setInterval(ms); }
    int intervaloBusqueda() const { return m_retardo.interval(); }

    QString solicitudId() const;
    CfdiListModel* modelo() const { return m_modelo; }
    Estado estado() const;
    QString vista() const;
    QString errorMessage() const;
    bool actualizando() const { return m_genListaPendiente != 0 || m_genResumenPendiente != 0; }
    QString texto() const { return m_texto; }
    void setTexto(const QString& texto);
    QString tipo() const { return m_tipo; }
    void setTipo(const QString& tipo);
    bool hayFiltros() const;
    int totalCfdi() const { return m_resumen ? m_resumen->cfdiTotal : 0; }
    bool indexando() const { return m_resumen && m_resumen->indexando; }
    QVariantList lineasEstado() const;
    bool puedeAbrirCarpeta() const { return m_finder != nullptr && m_solicitud.has_value(); }
    QString mensajeFinder() const { return m_mensajeFinder; }

    // Abre (o reabre) el explorador de la solicitud. Otra solicitud vacia la
    // lista y los filtros. false si el id no es canonico.
    Q_INVOKABLE bool abrir(const QString& id);
    // "Reintentar": vuelve a Cargando y consulta lista y resumen.
    Q_INVOKABLE void recargar();
    Q_INVOKABLE void limpiarFiltros();
    Q_INVOKABLE void abrirCarpetaSolicitud();
    // Intencion de previsualizar el CFDI de la fila (T015.4). false si la
    // fila no existe.
    Q_INVOKABLE bool previsualizar(int fila);
    // Al salir de la pagina: descarta respuestas en curso y deja de recargar
    // con las senales hasta el siguiente abrir().
    void desactivar();

signals:
    void solicitudIdChanged();
    void estadoChanged();
    void vistaChanged();
    void actualizandoChanged();
    void filtrosChanged();
    void resumenChanged();
    void mensajeFinderChanged();
    void previsualizacionSolicitada(const QString& solicitudId, const QString& paqueteId, int posicion);

private:
    FiltroListaCfdi filtroActual() const;
    void consultarLista();
    void consultarResumen();
    void refrescar();
    void alCambiarIndice(const SolicitudId& id);
    void alCambiarActividad();
    // Ejecuta `cambio` y emite las senales de estado, vista y actualizando que
    // correspondan.
    template <typename F>
    void notificar(F&& cambio);
    void setMensajeFinder(const QString& mensaje);

    int m_profundidad = 0;
    QPointer<ConsultaCatalogoCfdi> m_consulta;
    AccionesFinder* m_finder = nullptr;
    CfdiListModel* m_modelo;
    QTimer m_retardo;
    std::optional<SolicitudId> m_solicitud;
    bool m_activo = false;
    QString m_texto;
    QString m_tipo;

    // Lista y resumen se consultan por separado (la actividad del indexador
    // solo recarga el resumen). *Pendiente != 0: respuesta esperada con ese
    // token; las demas se descartan.
    quint64 m_generacion = 0;
    quint64 m_genListaPendiente = 0;
    quint64 m_genResumenPendiente = 0;
    bool m_listaConocida = false;
    bool m_errorLista = false;
    bool m_errorResumen = false;
    // Filtro con el que se obtuvo la lista visible (para "sinCoincidencias").
    bool m_listaConFiltros = false;
    std::optional<FiltroListaCfdi> m_filtroSolicitado;
    std::optional<ResumenIndiceSolicitud> m_resumen;

    QString m_mensajeFinder;
    quint64 m_genFinder = 0;
};

} // namespace satcfdi
