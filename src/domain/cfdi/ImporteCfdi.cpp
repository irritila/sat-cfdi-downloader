#include "domain/cfdi/ImporteCfdi.h"

#include <utility>

namespace satcfdi {

namespace {

bool esDigito(QChar c) noexcept
{
    return c >= u'0' && c <= u'9';
}

bool esEspacioXml(QChar c) noexcept
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r';
}

} // namespace

ImporteCfdi::ImporteCfdi(QString texto, QString entero, QString fraccion)
    : m_texto(std::move(texto))
    , m_entero(std::move(entero))
    , m_fraccion(std::move(fraccion))
{
}

std::optional<ImporteCfdi> ImporteCfdi::desdeTexto(QStringView texto)
{
    qsizetype inicio = 0;
    qsizetype fin = texto.size();
    while (inicio < fin && esEspacioXml(texto.at(inicio))) {
        ++inicio;
    }
    while (fin > inicio && esEspacioXml(texto.at(fin - 1))) {
        --fin;
    }
    const QStringView nucleo = texto.sliced(inicio, fin - inicio);
    if (nucleo.isEmpty()) {
        return std::nullopt;
    }

    const qsizetype punto = nucleo.indexOf(u'.');
    const QStringView entero = punto < 0 ? nucleo : nucleo.first(punto);
    const QStringView fraccion = punto < 0 ? QStringView() : nucleo.sliced(punto + 1);
    if (entero.isEmpty() && fraccion.isEmpty()) {
        return std::nullopt; // "."
    }
    if (entero.size() > kMaxDigitosEnteros || fraccion.size() > kMaxDigitosDecimales) {
        return std::nullopt;
    }
    for (const QChar c : entero) {
        if (!esDigito(c)) {
            return std::nullopt;
        }
    }
    for (const QChar c : fraccion) {
        if (!esDigito(c)) {
            return std::nullopt; // incluye un segundo punto
        }
    }

    qsizetype i = 0;
    while (i < entero.size() && entero.at(i) == u'0') {
        ++i;
    }
    qsizetype f = fraccion.size();
    while (f > 0 && fraccion.at(f - 1) == u'0') {
        --f;
    }
    return ImporteCfdi(texto.toString(), entero.sliced(i).toString(), fraccion.first(f).toString());
}

QString ImporteCfdi::normalizado() const
{
    QString resultado = m_entero.isEmpty() ? QStringLiteral("0") : m_entero;
    if (!m_fraccion.isEmpty()) {
        resultado += u'.';
        resultado += m_fraccion;
    }
    return resultado;
}

std::strong_ordering operator<=>(const ImporteCfdi& a, const ImporteCfdi& b) noexcept
{
    // Enteros sin ceros a la izquierda: mas digitos implica mayor valor.
    if (a.m_entero.size() != b.m_entero.size()) {
        return a.m_entero.size() <=> b.m_entero.size();
    }
    const int enteros = QStringView(a.m_entero).compare(QStringView(b.m_entero));
    if (enteros != 0) {
        return enteros <=> 0;
    }
    // Fracciones sin ceros a la derecha: comparar digito a digito; si una es
    // prefijo de la otra, la mas larga es mayor (su resto no es cero).
    const qsizetype comun = qMin(a.m_fraccion.size(), b.m_fraccion.size());
    const int fracciones = QStringView(a.m_fraccion).first(comun).compare(QStringView(b.m_fraccion).first(comun));
    if (fracciones != 0) {
        return fracciones <=> 0;
    }
    return a.m_fraccion.size() <=> b.m_fraccion.size();
}

} // namespace satcfdi
