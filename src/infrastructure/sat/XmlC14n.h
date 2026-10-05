#pragma once

#include "domain/common/Resultado.h"

#include <QByteArray>
#include <QString>

namespace satcfdi::sat {

// Canonicalizacion XML (T006, D2) con libxml2 del SDK: C14N 1.0 inclusiva
// (http://www.w3.org/TR/2001/REC-xml-c14n-20010315) y exclusiva
// (http://www.w3.org/2001/10/xml-exc-c14n#), ambas SIN comentarios. Los
// headers no exponen tipos de libxml2.
//
// Seguridad del parser: sin red (XML_PARSE_NONET), sin sustitucion de
// entidades y se rechaza cualquier DOCTYPE (sin DTD no hay entidades
// externas ni expansion).

enum class VarianteC14n {
    Exclusiva,
    Inclusiva,
};

// URI del algoritmo para CanonicalizationMethod/Transform.
QString uriAlgoritmo(VarianteC14n variante);

struct ErrorXml {
    QString diagnostico; // tecnico, sin contenido del documento
};

// Nodo apice del conjunto a canonicalizar (subconjunto de documento XPath
// "descendant-or-self" del apice, como en una Reference de XML-DSig).
struct SelectorNodo {
    enum class Tipo {
        Documento, // documento completo (Reference URI="")
        PorId,     // elemento con un atributo de nombre local "Id" (con o sin ns) == valor
        PorNombre, // primer elemento, en orden de documento, con ese ns + nombre local
    };

    Tipo tipo = Tipo::Documento;
    QByteArray valor;          // PorId: valor del Id; PorNombre: nombre local
    QByteArray espacioNombres; // PorNombre: URI del namespace (vacio = sin namespace)
    // Transform enveloped-signature: excluye del conjunto todo elemento
    // ds:Signature (http://www.w3.org/2000/09/xmldsig#) descendiente del apice.
    bool excluirFirma = false;

    static SelectorNodo documento(bool excluirFirma = false)
    {
        SelectorNodo s;
        s.excluirFirma = excluirFirma;
        return s;
    }
    static SelectorNodo porId(QByteArray id)
    {
        SelectorNodo s;
        s.tipo = Tipo::PorId;
        s.valor = std::move(id);
        return s;
    }
    static SelectorNodo porNombre(QByteArray espacioNombres, QByteArray nombreLocal, bool excluirFirma = false)
    {
        SelectorNodo s;
        s.tipo = Tipo::PorNombre;
        s.espacioNombres = std::move(espacioNombres);
        s.valor = std::move(nombreLocal);
        s.excluirFirma = excluirFirma;
        return s;
    }
};

// Canonicaliza el subconjunto seleccionado de `xml` (UTF-8). Errores: XML mal
// formado, DOCTYPE presente, nodo no encontrado o Id duplicado, fallo de C14N.
Resultado<QByteArray, ErrorXml> canonicalizar(const QByteArray& xml, VarianteC14n variante,
                                              const SelectorNodo& selector);

// SHA-1 (OpenSSL) de `datos`, en bytes y en Base64.
QByteArray sha1(const QByteArray& datos);
QByteArray sha1Base64(const QByteArray& datos);

} // namespace satcfdi::sat
