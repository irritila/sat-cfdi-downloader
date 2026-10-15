#pragma once

// LectorZipLibzip: implementacion de CfdiArchiveReader con libzip (API
// publica; QZipReader es privado). T015 D4/D11:
// - Abre con ZIP_RDONLY | ZIP_CHECKCONS; nunca escribe en disco.
// - El nombre interno de cada entrada solo se usa para validarlo; nunca como
//   ruta ni en logs.
// - Lectura acotada por entrada (zip_fread en trozos contando bytes reales).
// - Logs solo con causa y conteos.
// Sin estado compartido: cada llamada abre su propio zip_t (seguro desde un
// hilo de trabajo). Los tipos de libzip no salen de este target.

#include "infrastructure/cfdi/zip/LimitesLectorZip.h"
#include "ports/cfdi/CfdiArchiveReader.h"

namespace satcfdi {

class LectorZipLibzip final : public CfdiArchiveReader {
public:
    // Produccion: limites de T015 D4.
    LectorZipLibzip();
    // Solo pruebas: limites reducidos para ejercitarlos sin generar 1 GiB.
    explicit LectorZipLibzip(const LimitesLectorZip& limites);

    Resultado<ResumenLecturaZip, ErrorPaqueteZip> recorrer(
        const QString& rutaZip,
        const std::function<bool(const EntradaXmlCfdi&)>& alXml) override;

    // T015.4: abre el ZIP y descomprime solo la entrada `indice`.
    Resultado<std::optional<QByteArray>, ErrorPaqueteZip> leerEntrada(const QString& rutaZip, int indice) override;

    const LimitesLectorZip& limites() const noexcept { return m_limites; }

private:
    LimitesLectorZip m_limites;
};

} // namespace satcfdi
