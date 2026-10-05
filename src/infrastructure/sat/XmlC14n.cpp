#include "infrastructure/sat/XmlC14n.h"

#include <libxml/c14n.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xmlIO.h>

#include <openssl/evp.h>

#include <cstring>
#include <memory>

namespace satcfdi::sat {

namespace {

constexpr const char* kNsXmlDsig = "http://www.w3.org/2000/09/xmldsig#";

struct LiberarDoc {
    void operator()(xmlDoc* d) const noexcept { xmlFreeDoc(d); }
};
using DocPtr = std::unique_ptr<xmlDoc, LiberarDoc>;

struct LiberarBuffer {
    void operator()(xmlOutputBuffer* b) const noexcept { xmlOutputBufferClose(b); }
};
using BufferPtr = std::unique_ptr<xmlOutputBuffer, LiberarBuffer>;

bool esElemento(const xmlNode* n, const char* ns, const char* local)
{
    if (n == nullptr || n->type != XML_ELEMENT_NODE || n->name == nullptr) {
        return false;
    }
    if (std::strcmp(reinterpret_cast<const char*>(n->name), local) != 0) {
        return false;
    }
    const char* uri = (n->ns != nullptr && n->ns->href != nullptr) ? reinterpret_cast<const char*>(n->ns->href) : "";
    return std::strcmp(uri, ns) == 0;
}

// Busca en orden de documento. `cuenta` permite detectar Ids duplicados.
void buscar(xmlNode* n, const SelectorNodo& s, xmlNode*& encontrado, int& cuenta)
{
    for (; n != nullptr; n = n->next) {
        if (n->type != XML_ELEMENT_NODE) {
            continue;
        }
        if (s.tipo == SelectorNodo::Tipo::PorId) {
            for (xmlAttr* a = n->properties; a != nullptr; a = a->next) {
                if (a->name != nullptr && std::strcmp(reinterpret_cast<const char*>(a->name), "Id") == 0) {
                    xmlChar* v = xmlNodeGetContent(reinterpret_cast<xmlNode*>(a));
                    const bool igual = v != nullptr && s.valor == reinterpret_cast<const char*>(v);
                    xmlFree(v);
                    if (igual) {
                        ++cuenta;
                        if (encontrado == nullptr) {
                            encontrado = n;
                        }
                    }
                }
            }
        } else if (encontrado == nullptr
                   && esElemento(n, s.espacioNombres.constData(), s.valor.constData())) {
            encontrado = n;
            cuenta = 1;
            return;
        }
        buscar(n->children, s, encontrado, cuenta);
        if (s.tipo == SelectorNodo::Tipo::PorNombre && encontrado != nullptr) {
            return;
        }
    }
}

struct Visibilidad {
    xmlNode* apice = nullptr; // nullptr = documento completo
    bool excluirFirma = false;
};

// Callback de libxml2: un nodo es visible si su elemento (o el elemento
// portador, para atributos y namespaces) desciende del apice y no de una
// ds:Signature excluida. xmlNs y xmlNode comparten la posicion de `type`.
int esVisible(void* datos, xmlNodePtr nodo, xmlNodePtr padre)
{
    const auto* v = static_cast<const Visibilidad*>(datos);
    if (nodo == nullptr) {
        return 0;
    }
    xmlNode* base = nodo;
    if (nodo->type == XML_NAMESPACE_DECL || nodo->type == XML_ATTRIBUTE_NODE) {
        base = padre;
    }
    if (base == nullptr) {
        return 0;
    }
    bool dentro = v->apice == nullptr;
    for (xmlNode* x = base; x != nullptr && x->type != XML_DOCUMENT_NODE; x = x->parent) {
        if (v->excluirFirma && esElemento(x, kNsXmlDsig, "Signature")) {
            // El propio elemento Signature y todo su contenido quedan fuera.
            return 0;
        }
        if (x == v->apice) {
            dentro = true;
            break;
        }
    }
    return dentro ? 1 : 0;
}

} // namespace

QString uriAlgoritmo(VarianteC14n variante)
{
    return variante == VarianteC14n::Exclusiva ? QStringLiteral("http://www.w3.org/2001/10/xml-exc-c14n#")
                                               : QStringLiteral("http://www.w3.org/TR/2001/REC-xml-c14n-20010315");
}

Resultado<QByteArray, ErrorXml> canonicalizar(const QByteArray& xml, VarianteC14n variante,
                                              const SelectorNodo& selector)
{
    using R = Resultado<QByteArray, ErrorXml>;
    xmlInitParser();
    if (xml.size() > 64 * 1024 * 1024) {
        return R::fallo({QStringLiteral("xml.tamano")});
    }
    DocPtr doc(xmlReadMemory(xml.constData(), static_cast<int>(xml.size()), nullptr, "UTF-8",
                             XML_PARSE_NONET | XML_PARSE_NOWARNING | XML_PARSE_NOERROR));
    if (!doc) {
        return R::fallo({QStringLiteral("xml.mal_formado")});
    }
    if (doc->intSubset != nullptr || doc->extSubset != nullptr) {
        return R::fallo({QStringLiteral("xml.doctype_no_permitido")});
    }

    Visibilidad vis;
    vis.excluirFirma = selector.excluirFirma;
    if (selector.tipo != SelectorNodo::Tipo::Documento) {
        xmlNode* encontrado = nullptr;
        int cuenta = 0;
        buscar(xmlDocGetRootElement(doc.get()), selector, encontrado, cuenta);
        if (encontrado == nullptr) {
            return R::fallo({QStringLiteral("xml.nodo_no_encontrado")});
        }
        if (cuenta > 1) {
            return R::fallo({QStringLiteral("xml.id_duplicado")});
        }
        vis.apice = encontrado;
    }

    BufferPtr salida(xmlAllocOutputBuffer(nullptr));
    if (!salida) {
        return R::fallo({QStringLiteral("xml.buffer")});
    }
    const int modo = variante == VarianteC14n::Exclusiva ? XML_C14N_EXCLUSIVE_1_0 : XML_C14N_1_0;
    const int escrito = xmlC14NExecute(doc.get(), &esVisible, &vis, modo, nullptr, 0, salida.get());
    if (escrito < 0) {
        return R::fallo({QStringLiteral("xml.c14n")});
    }
    xmlOutputBufferFlush(salida.get());
    const xmlChar* contenido = xmlOutputBufferGetContent(salida.get());
    const size_t tamano = xmlOutputBufferGetSize(salida.get());
    return R::exito(QByteArray(reinterpret_cast<const char*>(contenido), static_cast<qsizetype>(tamano)));
}

QByteArray sha1(const QByteArray& datos)
{
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int largo = 0;
    if (EVP_Digest(datos.constData(), static_cast<size_t>(datos.size()), md, &largo, EVP_sha1(), nullptr) != 1) {
        return {};
    }
    return QByteArray(reinterpret_cast<const char*>(md), static_cast<qsizetype>(largo));
}

QByteArray sha1Base64(const QByteArray& datos)
{
    return sha1(datos).toBase64();
}

} // namespace satcfdi::sat
