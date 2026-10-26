#pragma once

// Diagnostico de clics (T022 D2): traza opcional, apagada por omision, del
// estado de la ventana y del control objetivo alrededor de cada clic del
// guion. Solo observa: no bombea eventos, no renderiza ni consume cuadros, asi
// que activarla no cambia la linea de tiempo ni los cuadros del video.
//
// Se activa con SATCFDI_VIDEO_DIAGNOSTICO_CLIC=1 y escribe en
// <salida>/<nombre>.diag.log. Sin RFC, contrasenas ni otros secretos: solo
// objectName, clases, geometria, banderas y enums de estado de la UI.

#include <QElapsedTimer>
#include <QFile>
#include <QList>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <utility>

class QQuickItem;
class QQuickWindow;

namespace video {

class DiagnosticoClic final : public QObject {
public:
    // Estado semantico minimo: propiedades (enums, bool) de objetos de la UI.
    struct Semantico {
        QString etiqueta;
        QPointer<QObject> objeto;
        const char* propiedad = nullptr;
        // Si no esta vacio, el objeto es el item visible con ese objectName
        // en cada instantanea (paginas creadas por un Loader).
        QString objectName;
    };

    // `ruta` vacia: diagnostico apagado (ninguna otra llamada hace nada).
    DiagnosticoClic(const QString& ruta, QQuickWindow* ventana, QQuickItem* excluir);

    bool activo() const { return m_archivo.isOpen(); }
    QString ruta() const { return m_archivo.fileName(); }

    void setSemantico(QList<Semantico> semantico) { m_semantico = std::move(semantico); }
    // Contexto de las lineas siguientes (escena y objetivo del ultimo mover).
    // Se guarda aunque el diagnostico este apagado (estado()).
    void setContexto(const QString& escena, const QString& nombreObjetivo, QQuickItem* objetivo);
    // Linea libre (p. ej. resultado de la busqueda de un control).
    void nota(int cuadro, const QString& texto);
    // Instantanea completa del objetivo y la ventana con el cursor en `punto`.
    void instantanea(int cuadro, const QString& momento, QPointF punto);
    // Las mismas lineas sin escribirlas (tambien con el diagnostico apagado):
    // traza de un fallo (T022 D7).
    QStringList estado(const QString& momento, QPointF punto) const;
    QString nombreObjetivo() const { return m_nombreObjetivo; }
    // Registra los eventos que reciben la ventana y el objetivo hasta el
    // siguiente setContexto.
    void vigilar();

    // Cadena de items bajo `punto` respetando `z` y el orden de pintado, del
    // contentItem al item mas alto que contiene el punto (sin `excluir`).
    static QList<QQuickItem*> cadenaBajo(QQuickItem* raiz, QPointF punto, QQuickItem* excluir);
    static QString describir(const QQuickItem* item);

protected:
    bool eventFilter(QObject* objeto, QEvent* e) override;

private:
    void escribir(const QString& linea);
    void soltarVigilancia();
    QString semantico() const;

    QFile m_archivo;
    QElapsedTimer m_reloj;
    QQuickWindow* m_ventana = nullptr;
    QQuickItem* m_excluir = nullptr;
    QList<Semantico> m_semantico;
    QString m_escena;
    QString m_nombreObjetivo;
    QPointer<QQuickItem> m_objetivo;
    QPointer<QQuickItem> m_vigilado;
    bool m_vigilandoVentana = false;
    int m_cuadro = 0;
};

} // namespace video
