#include "BarridoVocabulario.h"

#include <QFile>
#include <QMetaProperty>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTextStream>

namespace satcfdi::pruebas::vocabulario {

namespace {

const QRegularExpression& terminos()
{
    static const QRegularExpression r(QStringLiteral("\\b(solicitud(?:es)?|paquetes?|verificacion(?:es)?|verificando)\\b"),
                                      QRegularExpression::UseUnicodePropertiesOption);
    return r;
}

bool aplica(const Excepcion& e, const QStringList& origenes)
{
    return e.origen == QLatin1String("*") || origenes.contains(e.origen);
}

// Lee un literal entre `comilla` desde `i` (en la comilla inicial). Deja `i`
// despues de la comilla final. Interpreta \n y quita la barra de los demas
// escapes.
QString leerLiteral(const QString& s, qsizetype& i, QChar comilla)
{
    QString r;
    ++i; // comilla inicial
    while (i < s.size() && s.at(i) != comilla) {
        if (s.at(i) == QLatin1Char('\\') && i + 1 < s.size()) {
            const QChar c = s.at(i + 1);
            r += c == QLatin1Char('n') ? QLatin1Char('\n') : c;
            i += 2;
            continue;
        }
        r += s.at(i++);
    }
    ++i; // comilla final
    return r;
}

// Todos los literales de cadena de una fuente C++ o QML/JS, fuera de
// comentarios y de lineas de preprocesador (#include). En C++ los literales
// adyacentes (solo separados por espacios) se concatenan; las comillas
// simples son caracteres. En QML/JS tambien cuentan '...' y `...`.
QStringList todosLosLiterales(const QString& s, bool cpp)
{
    QStringList r;
    QString actual;
    bool hayActual = false;
    bool inicioDeLinea = true;
    auto cerrar = [&] {
        if (hayActual) {
            r << actual;
            actual.clear();
            hayActual = false;
        }
    };
    qsizetype i = 0;
    while (i < s.size()) {
        const QChar c = s.at(i);
        const QChar sig = i + 1 < s.size() ? s.at(i + 1) : QChar();
        if (c == QLatin1Char('\n')) {
            inicioDeLinea = true;
            ++i;
            continue;
        }
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (cpp && inicioDeLinea && c == QLatin1Char('#')) {
            cerrar();
            while (i < s.size() && s.at(i) != QLatin1Char('\n')) {
                ++i;
            }
            continue;
        }
        inicioDeLinea = false;
        if (c == QLatin1Char('/') && sig == QLatin1Char('/')) {
            while (i < s.size() && s.at(i) != QLatin1Char('\n')) {
                ++i;
            }
            continue;
        }
        if (c == QLatin1Char('/') && sig == QLatin1Char('*')) {
            const qsizetype fin = s.indexOf(QStringLiteral("*/"), i + 2);
            i = fin < 0 ? s.size() : fin + 2;
            continue;
        }
        if (c == QLatin1Char('"')) {
            const QString literal = leerLiteral(s, i, c);
            if (!cpp) {
                cerrar();
            }
            actual += literal;
            hayActual = true;
            if (!cpp) {
                cerrar();
            }
            continue;
        }
        if (c == QLatin1Char('\'') || (!cpp && c == QLatin1Char('`'))) {
            cerrar();
            const QString literal = leerLiteral(s, i, c);
            if (!cpp) {
                r << literal;
            }
            continue;
        }
        cerrar();
        ++i;
    }
    cerrar();
    return r;
}

// Texto que puede ser visible: tiene un espacio o empieza con mayuscula. Las
// claves internas del proyecto (ids, roles, objectName, propiedades, claves
// de estado en minuscula, rutas de recursos) son una sola palabra en
// minuscula, camelCase o snake_case, y quedan fuera; "Verificando",
// "Paquetes" o "3 paquetes" quedan dentro. Una palabra suelta en minuscula
// que es un termino prohibido ("paquetes") tambien queda dentro: no se
// distingue de un texto visible y, si es clave o carpeta, va al inventario.
bool pareceVisible(const QString& literal)
{
    const QString t = literal.trimmed();
    if (t.isEmpty()) {
        return false;
    }
    if (t.contains(QLatin1Char(' ')) || t.at(0).isUpper()) {
        return true;
    }
    const QRegularExpressionMatch m = terminos().match(normalizar(t));
    return m.hasMatch() && m.capturedLength() == normalizar(t).size();
}

QStringList visibles(const QStringList& literales)
{
    QStringList r;
    for (const QString& l : literales) {
        if (pareceVisible(l)) {
            r << l;
        }
    }
    return r;
}

} // namespace

QString normalizar(const QString& texto)
{
    const QString d = texto.normalized(QString::NormalizationForm_D);
    QString r;
    r.reserve(d.size());
    for (const QChar c : d) {
        if (c.category() != QChar::Mark_NonSpacing) {
            r += c;
        }
    }
    return r.toLower().simplified();
}

QStringList terminosProhibidos(const QString& texto, const QStringList& origenes, const QList<Excepcion>& excepciones)
{
    QString n = normalizar(texto);
    for (const Excepcion& e : excepciones) {
        if (aplica(e, origenes)) {
            n.replace(normalizar(e.literal), QStringLiteral(" | "));
        }
    }
    QStringList r;
    auto it = terminos().globalMatch(n);
    while (it.hasNext()) {
        r << it.next().captured(1);
    }
    return r;
}

QList<Excepcion> cargarExcepciones(const QString& ruta, QString* error)
{
    QList<Excepcion> r;
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) {
            *error = QStringLiteral("no se pudo abrir ") + ruta;
        }
        return r;
    }
    QTextStream in(&f);
    in.setEncoding(QStringConverter::Utf8);
    int numero = 0;
    while (!in.atEnd()) {
        const QString linea = in.readLine();
        ++numero;
        if (linea.trimmed().isEmpty() || linea.startsWith(QLatin1Char('#'))) {
            continue;
        }
        const QStringList c = linea.split(QLatin1Char('\t'));
        const bool valida = c.size() == 3 && !c.at(0).trimmed().isEmpty() && !c.at(1).isEmpty()
                            && !c.at(2).trimmed().isEmpty()
                            && terminos().match(normalizar(c.at(1))).hasMatch();
        if (!valida) {
            if (error && error->isEmpty()) {
                *error = QStringLiteral("fila %1 invalida: %2").arg(numero).arg(linea);
            }
            continue;
        }
        r.append({c.at(0).trimmed(), c.at(1), c.at(2).trimmed()});
    }
    return r;
}

QList<Excepcion> excepcionesPorOmision()
{
    QString error;
    const QList<Excepcion> r = cargarExcepciones(QStringLiteral(SATCFDI_EXCEPCIONES_VOCABULARIO), &error);
    if (!error.isEmpty()) {
        qWarning("inventario de excepciones de vocabulario: %s", qPrintable(error));
    }
    return r;
}

const QSet<QString>& archivosDiagnostico()
{
    static const QSet<QString> s{
        QStringLiteral("SolicitudesPage.qml"),     QStringLiteral("DetalleSolicitudPage.qml"),
        QStringLiteral("ExploradorCfdiPage.qml"),  QStringLiteral("FilaSolicitud.qml"),
        QStringLiteral("FilaPaquete.qml"),         QStringLiteral("Etiquetas.js"),
        QStringLiteral("SolicitudDetailViewModel.cpp"), QStringLiteral("SolicitudesListModel.cpp"),
        QStringLiteral("SolicitudesFiltroModel.cpp"),   QStringLiteral("ExploradorCfdiViewModel.cpp"),
        QStringLiteral("CfdiListModel.cpp"),
    };
    return s;
}

QStringList literalesQml(const QString& contenido)
{
    return visibles(todosLosLiterales(contenido, false));
}

QStringList literalesCpp(const QString& contenido)
{
    return visibles(todosLosLiterales(contenido, true));
}

} // namespace satcfdi::pruebas::vocabulario

namespace satcfdi::pruebas {

namespace {

const char* const kPropiedades[] = {"text",       "placeholderText", "title", "titulo", "subtitulo",
                                    "descripcion", "texto",          "mensaje"};

QString archivoDe(QObject* objeto)
{
    const QQmlContext* contexto = qmlContext(objeto);
    return contexto ? contexto->baseUrl().fileName() : QString();
}

QString propiedadTexto(const QObject* objeto, const char* nombre)
{
    const int i = objeto->metaObject()->indexOfProperty(nombre);
    if (i < 0) {
        return {};
    }
    const QMetaProperty p = objeto->metaObject()->property(i);
    if (p.metaType().id() != QMetaType::QString) {
        return {};
    }
    return p.read(objeto).toString();
}

} // namespace

BarridoVocabulario::BarridoVocabulario(QObject* parent)
    : QObject(parent)
    , m_excepciones(vocabulario::excepcionesPorOmision())
{
}

QStringList BarridoVocabulario::revisarVentana(QQuickItem* item)
{
    if (!item || !item->window()) {
        return {QStringLiteral("sin ventana")};
    }
    return revisarArbol(item->window()->contentItem());
}

QStringList BarridoVocabulario::revisarArbol(QObject* raiz)
{
    QStringList fallas;
    QSet<QObject*> vistos;
    // La raiz explicita se revisa aunque la declare un archivo de Diagnostico
    // (p. ej. el visor PDF dentro del explorador): solo se excluye su contenido
    // de Diagnostico.
    m_raizExplicita = raiz;
    visitar(raiz, {}, vistos, fallas);
    m_raizExplicita = nullptr;
    fallas.removeDuplicates();
    return fallas;
}

QStringList BarridoVocabulario::revisarTexto(const QString& origen, const QString& texto) const
{
    QStringList fallas;
    for (const QString& t : vocabulario::terminosProhibidos(texto, {origen}, m_excepciones)) {
        fallas << QStringLiteral("%1: \"%2\" -> %3").arg(origen, texto, t);
    }
    return fallas;
}

QString BarridoVocabulario::normalizar(const QString& texto) const
{
    return vocabulario::normalizar(texto);
}

void BarridoVocabulario::revisar(const QString& texto, const QStringList& origenes, QStringList& fallas)
{
    if (texto.isEmpty()) {
        return;
    }
    ++m_revisados;
    for (const QString& t : vocabulario::terminosProhibidos(texto, origenes, m_excepciones)) {
        fallas << QStringLiteral("%1: \"%2\" -> %3").arg(origenes.join(QLatin1Char('>')), texto, t);
    }
}

void BarridoVocabulario::visitar(QObject* objeto, QStringList origenes, QSet<QObject*>& vistos, QStringList& fallas)
{
    if (!objeto || vistos.contains(objeto)) {
        return;
    }
    vistos.insert(objeto);
    const QString archivo = archivoDe(objeto);
    if (objeto != m_raizExplicita && vocabulario::archivosDiagnostico().contains(archivo)) {
        return; // Diagnostico: excluido de forma explicita.
    }
    if (!archivo.isEmpty() && (origenes.isEmpty() || origenes.constLast() != archivo)) {
        origenes << archivo;
    }
    if (qstrcmp(objeto->metaObject()->className(), "QQuickAccessibleAttached") == 0) {
        revisar(propiedadTexto(objeto, "name"), origenes, fallas);
        revisar(propiedadTexto(objeto, "description"), origenes, fallas);
    } else {
        for (const char* nombre : kPropiedades) {
            revisar(propiedadTexto(objeto, nombre), origenes, fallas);
        }
    }
    if (auto* item = qobject_cast<QQuickItem*>(objeto)) {
        const QList<QQuickItem*> hijos = item->childItems();
        for (QQuickItem* hijo : hijos) {
            visitar(hijo, origenes, vistos, fallas);
        }
    }
    const QObjectList hijos = objeto->children();
    for (QObject* hijo : hijos) {
        visitar(hijo, origenes, vistos, fallas);
    }
}

} // namespace satcfdi::pruebas
