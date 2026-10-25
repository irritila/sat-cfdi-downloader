#pragma once

#include "ports/exportacion/EscritorZipExportacion.h"

#include <memory>
#include <utility>

namespace satcfdi {

// T026.1 (D12, C2): EscritorZipExportacion propio, en streaming, sobre
// ArchivoParcialExportacion::dispositivo(). libzip queda como lector y
// oraculo de pruebas, no como escritor.
//
// Subconjunto estricto de APPNOTE 6.3:
// - Metodo 8 (deflate raw, zlib nivel 6) para toda entrada; sin cifrado,
//   comentarios (de entrada ni de archivo) ni campos extra, salvo el ZIP64
//   (0x0001) obligatorio cuando aplica.
// - Bit 3 (descriptor de datos) siempre activo: el encabezado local lleva
//   CRC y tamanos en 0 y el descriptor (con firma 0x08074b50) los lleva
//   despues de los datos; el directorio central los repite.
// - `version made by` = Unix (3) | 2.0 (o 4.5 con ZIP64); atributos externos
//   (S_IFREG | 0600) << 16; atributos internos 0; disco 0.
// - Fecha y hora DOS de la exportacion en hora LOCAL del equipo (convencion
//   de los lectores ZIP sin campo extra de tiempo), con granularidad de 2 s
//   (los segundos impares se truncan) y rango 1980..2107 (fuera de rango se
//   satura). Sin campo extra de tiempo Unix.
// - ZIP64 solo cuando hace falta: por entrada si su tamano descomprimido o la
//   cota de compresion (deflateBound) llega a 0xFFFFFFFF (encabezado local con
//   tamanos 0xFFFFFFFF y extra ZIP64, descriptor con tamanos de 8 bytes) o si
//   su desplazamiento llega a 0xFFFFFFFF (extra ZIP64 en el directorio
//   central); y registros EOCD64 + localizador si hay >= 0xFFFF entradas o el
//   tamano/desplazamiento del directorio central llega a 0xFFFFFFFF.
// - Nombres: solo [A-Za-z0-9._-] y '/' como separador, sin '/' inicial
//   ni final, sin '\', sin componentes vacios, '.', '..', `._*` ni
//   `__MACOSX`, a lo mas 1024 bytes, sin duplicados (bit 11 UTF-8 apagado).
//
// Memoria: la entrada se comprime por trozos de 256 KiB de la vista del
// llamador (sin copiarla) hacia un bufer de salida de 64 KiB que se vacia al
// dispositivo; lo unico que crece con el ZIP es el directorio central en
// memoria (nombre + 28 bytes por entrada).
class EscritorZipStreaming final : public exportacion::EscritorZipExportacion {
public:
    struct Opciones {
        // Solo pruebas: umbral desde el que un tamano o desplazamiento se
        // codifica en ZIP64 (produccion: 0xFFFFFFFF). Permite ejercitar los
        // campos ZIP64 sin escribir 4 GiB.
        quint64 umbralZip64 = 0xFFFFFFFFULL;
        int nivelCompresion = 6;
    };

    EscritorZipStreaming();
    explicit EscritorZipStreaming(Opciones opciones);
    ~EscritorZipStreaming() override;

    EscritorZipStreaming(const EscritorZipStreaming&) = delete;
    EscritorZipStreaming& operator=(const EscritorZipStreaming&) = delete;

    // Pura, para pruebas: {hora DOS, fecha DOS} de `utc` en hora local.
    static std::pair<quint16, quint16> fechaHoraDos(const QDateTime& utc);
    // Fabrica de escritores nuevos con `opciones` (thread-safe: copia las
    // opciones por valor).
    static exportacion::FabricaEscritorZip fabrica();
    static exportacion::FabricaEscritorZip fabrica(Opciones opciones);

    // Pura, para pruebas: regla de nombres de entrada.
    static bool esNombreEntradaValido(const QString& nombre);

    Resultado<Exito, exportacion::ErrorZipExportacion> abrir(exportacion::ArchivoParcialExportacion& parcial,
                                                             QDateTime fechaExportacionUtc,
                                                             std::function<bool()> continuar = {}) override;
    Resultado<Exito, exportacion::ErrorZipExportacion> agregar(const QString& nombreEntrada,
                                                               QByteArrayView bytesValidados) override;
    Resultado<exportacion::ResumenZipExportacion, exportacion::ErrorZipExportacion> cerrar() override;
    void abortar() noexcept override;

    struct Estado;

private:
    std::unique_ptr<Estado> m_estado;
};

} // namespace satcfdi
