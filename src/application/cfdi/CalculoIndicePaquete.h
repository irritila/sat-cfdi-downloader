#pragma once

#include "domain/cfdi/ResultadoParseoCfdi.h"
#include "ports/repositories/CfdiCatalogRepository.h"

#include <QByteArrayView>
#include <QDateTime>

#include <functional>

namespace satcfdi {

class ArchivosZipCfdi;
class CfdiArchiveReader;

// Parser de CFDI inyectable (produccion: parsearCfdi de satcfdi_cfdi_xml).
// Debe ser puro y reentrante (se llama desde el hilo del indexador).
using ParserCfdiFn = std::function<ResultadoParseoCfdi(QByteArrayView)>;

// Dependencias del calculo, todas usadas en el hilo del indexador.
struct EntornoCalculoIndice {
    CfdiArchiveReader& lector;
    ArchivosZipCfdi& archivos;
    ParserCfdiFn parser;
    int versionParser = 1;
};

// Decision para un paquete (T015.2, T015 D2). Solo describe lo que se debe
// escribir; la escritura la hace el indexador en UNA transaccion.
struct CalculoPaquete {
    enum class Tipo {
        SinCambio,        // indice vigente: no se escribe nada
        ActualizarHuella, // mismo SHA-256, otro tamano/mtime: solo metadatos
        Reemplazar,       // borrar lo derivado y escribir `indice`
        Cancelado,        // se cancelo a mitad: no se escribe nada (queda como estaba)
    };
    Tipo tipo = Tipo::SinCambio;
    IndicePaquete indice; // con Reemplazar
    HuellaZip huella;     // con ActualizarHuella
};

// Regla por paquete (hilo del indexador; bloqueante: stat, SHA-256, lectura
// del ZIP y parseo):
//  1. Si el ZIP no existe o su ruta no es valida: OrigenNoDisponible sin CFDI
//     (SinCambio si ya lo estaba con la version vigente).
//  2. Indice vigente (Indexado o Ilegible, con huella y version vigente):
//     tamano y mtime iguales -> SinCambio; distintos -> SHA-256: igual ->
//     ActualizarHuella; distinto -> reindexar.
//  3. Sin fila, Pendiente, OrigenNoDisponible o version distinta: indexar.
//  Indexar: SHA-256, recorrer el ZIP y parsear cada XML. Error del lector ->
//  Ilegible (u OrigenNoDisponible si no existe) sin CFDI. Si el ZIP cambio
//  durante la lectura -> Pendiente sin CFDI.
// `continuar` se consulta entre trozos del SHA-256 y entre entradas del ZIP;
// si devuelve false el resultado es Cancelado (sin escribir).
CalculoPaquete calcularIndicePaquete(const PaqueteIndexable& paquete, EntornoCalculoIndice& entorno,
                                     const std::function<bool()>& continuar, const QDateTime& ahoraUtc);

} // namespace satcfdi
