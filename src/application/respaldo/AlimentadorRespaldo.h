#pragma once

// T025.2: alimenta un EscritorRespaldo (T025.1) con una CapturaRespaldo. Sin
// archivos propios: el escritor abre cada fuente por descriptor (raiz + ruta
// relativa, ADR 0021). Lo usa el servicio de respaldo (T025.3) y las pruebas
// de ida y vuelta.
//
// Reglas (D11):
// - Paquete que ya faltaba al congelar (sin huella): anotarFaltante() y el
//   respaldo termina "con advertencias".
// - Entrada con huella: agregarArchivo con esa huella. Si cambio
//   (FuenteCambio) o desaparecio despues de congelarse (FuenteFaltante, que se
//   informa como FuenteCambio "fuente.desaparecio"), falla.
// - FuenteInvalida, RutaInvalida, LimiteExcedido, Cancelado o cualquier otro
//   error: falla y el destino se descarta (no se publica).

#include "domain/common/Resultado.h"
#include "ports/respaldo/ContenedorRespaldo.h"
#include "ports/respaldo/FuenteRespaldo.h"

#include <QDateTime>
#include <QUuid>

namespace satcfdi::respaldo {

struct ResumenAlimentacion {
    int presentes = 0;
    int faltantes = 0; // paquetes anotados como faltantes (D11)
    int omitidos = 0;  // CapturaRespaldo::paquetesOmitidos

    bool conAdvertencias() const noexcept { return faltantes > 0 || omitidos > 0; }
};

// Datos del contenedor para esta captura: metadatos serializados (esquema y
// version de app), instalacion y fecha.
DatosNuevoRespaldo datosNuevoRespaldo(const CapturaRespaldo& captura, const QUuid& idInstalacion,
                                      const QDateTime& ahoraUtc);

// Agrega todas las entradas de `captura` a `escritor`, en orden. No llama a
// finalizar(): el llamador lo hace (y publica) solo si esto tiene exito.
Resultado<ResumenAlimentacion, ErrorRespaldo> alimentarEscritor(EscritorRespaldo& escritor,
                                                                const CapturaRespaldo& captura,
                                                                const Cancelacion& cancelacion = {});

} // namespace satcfdi::respaldo
