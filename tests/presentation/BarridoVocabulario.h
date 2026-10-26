#pragma once

// T024.1 (criterio 1): barrido de vocabulario del recorrido normal. Lo usan
// satcfdi_vocabulario_tests (textos de C++ y literales de las fuentes) y
// satcfdi_presentation_quicktests (arbol de items de las vistas, context
// property "barrido").
//
// - Normaliza (minusculas, sin acentos, espacios simples) y busca los
//   terminos del glosario D3 que no deben verse fuera de Diagnostico:
//   solicitud(es), paquete(s), verificacion(es) y verificando.
// - Excepciones: tests/presentation/vocabulario-excepciones.tsv (origen,
//   literal, motivo). Una excepcion solo aplica a su origen (nombre de
//   archivo; "*" = cualquiera) y solo quita su literal: otro termino
//   prohibido en el mismo texto sigue fallando.
// - Diagnostico queda excluido de forma explicita (archivosDiagnostico()):
//   el recorrido no entra en items creados por esos archivos y el barrido de
//   fuentes no los lee.

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QQuickItem;

namespace satcfdi::pruebas::vocabulario {

struct Excepcion {
    QString origen;  // nombre de archivo o "*"
    QString literal; // tal como aparece (se normaliza al comparar)
    QString motivo;
};

// Minusculas, sin marcas diacriticas y con espacios simples.
QString normalizar(const QString& texto);

// Terminos prohibidos (normalizados, p. ej. "paquetes") presentes en
// `texto` despues de quitar las excepciones que aplican a alguno de
// `origenes`.
QStringList terminosProhibidos(const QString& texto, const QStringList& origenes,
                               const QList<Excepcion>& excepciones);

// Lee el inventario. `error` recibe la primera fila mal formada (sin tres
// columnas no vacias, o con un literal que no contiene termino prohibido).
QList<Excepcion> cargarExcepciones(const QString& ruta, QString* error = nullptr);
QList<Excepcion> excepcionesPorOmision(); // SATCFDI_EXCEPCIONES_VOCABULARIO

// Archivos de Diagnostico (vocabulario tecnico permitido).
const QSet<QString>& archivosDiagnostico();

// Literales que pueden ser visibles de una fuente: todo literal de cadena
// fuera de comentarios (qsTr, tr, trn, textoMenu, QStringLiteral, literales
// directos en QML...) que contenga un espacio o empiece con mayuscula. En C++
// concatena literales adyacentes y omite lineas de preprocesador.
QStringList literalesQml(const QString& contenido);
QStringList literalesCpp(const QString& contenido);

} // namespace satcfdi::pruebas::vocabulario

namespace satcfdi::pruebas {

// Recorrido del arbol de una ventana o item (QML: context property
// "barrido"). Lee text, placeholderText, title, titulo, subtitulo,
// descripcion, texto y mensaje de cada objeto, y name/description de su
// Accessible adjunto, incluidos items ocultos y popups declarados (cerrados
// o abiertos). Los objetos adjuntos son hijos QObject de su item, asi que el
// `text` de ToolTip (QQuickToolTipAttached) se lee aunque el tooltip nunca se
// haya mostrado (p. ej. BotonAccion.textoAyuda); lo fija
// tst_barrido_vocabulario::test_detectaTooltipDinamico. Devuelve una linea por falla: "origen: \"texto\" -> termino".
class BarridoVocabulario : public QObject {
    Q_OBJECT
    Q_PROPERTY(int textosRevisados READ textosRevisados)

public:
    explicit BarridoVocabulario(QObject* parent = nullptr);

    // Recorre desde el contentItem de la ventana de `item` (incluye el
    // overlay de popups abiertos).
    Q_INVOKABLE QStringList revisarVentana(QQuickItem* item);
    // Recorre solo el subarbol de `raiz` (la raiz se revisa aunque la declare
    // Diagnostico).
    Q_INVOKABLE QStringList revisarArbol(QObject* raiz);
    // Un texto con un origen explicito (prueba negativa).
    Q_INVOKABLE QStringList revisarTexto(const QString& origen, const QString& texto) const;
    Q_INVOKABLE QString normalizar(const QString& texto) const;

    int textosRevisados() const { return m_revisados; }

private:
    void visitar(QObject* objeto, QStringList origenes, QSet<QObject*>& vistos, QStringList& fallas);
    void revisar(const QString& texto, const QStringList& origenes, QStringList& fallas);

    QList<vocabulario::Excepcion> m_excepciones;
    int m_revisados = 0;
    QObject* m_raizExplicita = nullptr;
};

} // namespace satcfdi::pruebas
