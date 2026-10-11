#pragma once

#include "domain/cfdi/ResumenCfdi.h"

#include <QLatin1StringView>
#include <QString>
#include <QStringView>

#include <optional>

namespace satcfdi {

// Indice local de CFDI (T015.2, T015 D2/D3). Tipos y reglas puras; la
// persistencia vive en CfdiCatalogRepository (migracion 006).

// Estado de indexacion de un paquete Descargado.
//  - Pendiente: sin indice vigente (nunca indexado, version de parser distinta
//    o ZIP cambiado durante la lectura). Un paquete sin fila de indice se
//    reporta tambien como Pendiente.
//  - Indexado: CFDI del paquete en el indice, con la huella del ZIP leido.
//  - OrigenNoDisponible: el ZIP no existe o no se puede abrir; sin CFDI.
//  - Ilegible: el ZIP existe pero el lector lo rechazo; sin CFDI.
enum class EstadoIndicePaquete {
    Pendiente,
    Indexado,
    OrigenNoDisponible,
    Ilegible,
};

// Clave persistida ("Pendiente", "Indexado", ...).
QLatin1StringView claveEstadoIndice(EstadoIndicePaquete estado);
std::optional<EstadoIndicePaquete> estadoIndiceDesdeClave(QStringView clave);

// Clave persistida del estado de parseo ("Completo", "Parcial",
// "VersionNoSoportada", "Ilegible", "NoEsCfdi").
QLatin1StringView claveEstadoParseo(EstadoParseo estado);
std::optional<EstadoParseo> estadoParseoDesdeClave(QStringView clave);

// Huella de un ZIP (D2): tamano en bytes, fecha de modificacion (ms desde
// epoch, UTC) y SHA-256 en hexadecimal minusculo (64 caracteres).
struct HuellaZip {
    qint64 tamano = 0;
    qint64 modificadoMs = 0;
    QString sha256;

    // Revalidacion rapida (D2): tamano y fecha de modificacion iguales.
    bool mismosMetadatos(qint64 otroTamano, qint64 otroModificadoMs) const noexcept
    {
        return tamano == otroTamano && modificadoMs == otroModificadoMs;
    }

    friend bool operator==(const HuellaZip&, const HuellaZip&) = default;
};

// Texto de busqueda normalizado: minusculas (case folding) y sin marcas
// diacriticas (NFD sin marcas no espaciadas: "Peña" -> "pena"), con los
// espacios colapsados y recortados. Se aplica igual al texto indexado y al
// texto que escribe el usuario.
QString normalizarTextoBusqueda(QStringView texto);

// Texto de busqueda de un CFDI: RFC y nombre de emisor y receptor, UUID,
// serie y folio, normalizados y separados por un espacio. Contiene datos
// fiscales: solo va a la base local (D3), nunca a logs (D11).
QString textoBusquedaCfdi(const ResumenCfdi& resumen);

} // namespace satcfdi
