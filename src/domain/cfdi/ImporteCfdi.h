#pragma once

#include <QString>
#include <QStringView>

#include <compare>
#include <optional>

namespace satcfdi {

// Decimal exacto de un CFDI (importes, cantidades, tasas, tipos de cambio).
// T015 D5: nunca se convierte a double. Conserva el texto original del XML
// (`texto()`) para mostrarlo tal cual y compara por valor numerico exacto
// sobre los digitos: "100.50" == "100.5" y "0.1" != "0.10000000000000001".
//
// Gramatica aceptada (xs:decimal sin signo, como los tipos t_Importe del
// Anexo 20): digitos con punto decimal opcional, al menos un digito, sin signo,
// sin exponente, sin separadores de miles. Se ignoran espacios en los
// extremos (colapso de espacios de XSD) solo al validar; `texto()` los conserva.
// Limite: 40 digitos enteros y 40 decimales (muy por encima del Anexo 20).
class ImporteCfdi {
public:
    static constexpr int kMaxDigitosEnteros = 40;
    static constexpr int kMaxDigitosDecimales = 40;

    // nullopt si el texto no cumple la gramatica.
    static std::optional<ImporteCfdi> desdeTexto(QStringView texto);

    // Texto exacto tal como venia en el XML.
    const QString& texto() const noexcept { return m_texto; }

    // Forma canonica: sin ceros superfluos a la izquierda ni a la derecha
    // ("0010.500" -> "10.5", "0.00" -> "0"). Util como clave de orden/indice.
    QString normalizado() const;

    bool esCero() const noexcept { return m_entero.isEmpty() && m_fraccion.isEmpty(); }

    // Comparacion numerica exacta (no textual).
    friend bool operator==(const ImporteCfdi& a, const ImporteCfdi& b) noexcept
    {
        return a.m_entero == b.m_entero && a.m_fraccion == b.m_fraccion;
    }
    friend std::strong_ordering operator<=>(const ImporteCfdi& a, const ImporteCfdi& b) noexcept;

private:
    ImporteCfdi(QString texto, QString entero, QString fraccion);

    QString m_texto;
    QString m_entero;   // digitos enteros sin ceros a la izquierda ("" = 0)
    QString m_fraccion; // digitos decimales sin ceros a la derecha ("" = 0)
};

} // namespace satcfdi
