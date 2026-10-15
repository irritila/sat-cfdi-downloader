#pragma once

// Puerto CfdiArchiveReader (T015.1, T015 D4/D10/D11): recorre los XML de un
// paquete ZIP del SAT sin escribir en disco. El nombre interno de cada entrada
// NUNCA sale del lector: solo su posicion (indice) y su contenido.

#include "domain/common/Resultado.h"

#include <QByteArray>
#include <QMap>
#include <QString>

#include <functional>
#include <optional>
#include <utility>

namespace satcfdi {

// XML aceptado. `indice` es la posicion de la entrada en el ZIP.
struct EntradaXmlCfdi {
    int indice = 0;
    QByteArray contenido;
};

// Motivo por el que una entrada se omite (el resto del ZIP continua).
enum class MotivoOmision {
    RutaInvalida,    // nombre con componente "..", absoluto o con '\'
    Enlace,          // atributos externos Unix de enlace simbolico
    Cifrada,         // metodo de cifrado distinto de ninguno
    NoXml,           // no termina en ".xml" (sin distinguir mayusculas)
    ExcedeTamano,    // mas del limite por entrada descomprimido (bytes reales)
    RazonCompresion, // razon descomprimido/comprimido excedida (y mas del umbral)
    ErrorLectura,    // libzip fallo al abrir o leer la entrada (incluye CRC)
};

struct ResumenLecturaZip {
    int xmlEntregados = 0;
    QMap<MotivoOmision, int> omitidas;
};

// Error por paquete: el llamador descarta todo lo recibido.
struct ErrorPaqueteZip {
    enum class Tipo { NoExiste, Ilegible } tipo;
    enum class Causa { NoExiste, Corrupto, Truncado, DemasiadasEntradas, ExcedeTotal } causa;
};

class CfdiArchiveReader {
public:
    virtual ~CfdiArchiveReader() = default;

    // Recorre las entradas en orden; entrega cada XML aceptado a `alXml`.
    // Si `alXml` devuelve false se cancela: no es error y se reporta lo leido.
    // Si devuelve error, el llamador DESCARTA todo lo recibido (sin
    // resultados parciales). Sincrono; se llama desde un hilo de trabajo
    // (nunca el grafico). No escribe en disco.
    virtual Resultado<ResumenLecturaZip, ErrorPaqueteZip> recorrer(
        const QString& rutaZip, const std::function<bool(const EntradaXmlCfdi&)>& alXml) = 0;

    // T015.4: lee SOLO la entrada en la posicion `indice` (la misma que
    // entrega recorrer) con las mismas reglas por entrada. Exito con nullopt
    // si no existe esa posicion o la entrada se omitiria (ruta invalida,
    // enlace, cifrada, no XML, limites, error de lectura). Error por paquete
    // como en recorrer. Sincrono; hilo de trabajo; no escribe en disco.
    // La implementacion por defecto recorre hasta la posicion (para fakes);
    // LectorZipLibzip la sobrescribe para descomprimir solo esa entrada.
    virtual Resultado<std::optional<QByteArray>, ErrorPaqueteZip> leerEntrada(const QString& rutaZip, int indice)
    {
        using R = Resultado<std::optional<QByteArray>, ErrorPaqueteZip>;
        std::optional<QByteArray> contenido;
        auto r = recorrer(rutaZip, [&contenido, indice](const EntradaXmlCfdi& e) {
            if (e.indice == indice) {
                contenido = e.contenido;
            }
            return e.indice < indice;
        });
        if (!r.esExito()) {
            return R::fallo(r.error());
        }
        return R::exito(std::move(contenido));
    }
};

} // namespace satcfdi
