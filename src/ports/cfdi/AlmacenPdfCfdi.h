#pragma once

// Puerto AlmacenPdfCfdi (T015.4, T015 D7): cache regenerable de PDF de CFDI en
// disco y copia "Guardar PDF...". Separa la orquestacion (application) del
// filesystem. Implementacion: infrastructure/cfdi/pdf/AlmacenPdfCfdiFs.
//
// Hilo: el hilo de trabajo de PrevisualizacionCfdiService (nunca el grafico).
// Sin logs con rutas personales ni datos del CFDI (D11).

#include "domain/common/Resultado.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace satcfdi {

// Clave de cache (D7): UUID del timbre + SHA-256 del XML + version del
// generador. El SHA-256 del XML hace que un XML distinto nunca reciba un PDF
// viejo aunque conserve el UUID.
struct ClavePdfCfdi {
    QString uuid;      // normalizado en mayusculas; vacio si el CFDI no lo trae
    QString sha256Xml; // 64 hex minusculas
    int versionGenerador = 1;

    friend bool operator==(const ClavePdfCfdi&, const ClavePdfCfdi&) = default;
};

struct PdfAlmacenado {
    QString rutaAbsoluta;
    bool enTemporal = false; // la cache no se pudo escribir: se uso el temporal de la app
};

struct ErrorAlmacenPdf {
    enum class Tipo {
        Escritura,     // ni la cache ni el temporal se pudieron escribir / destino no escribible
        OrigenInvalido, // guardarComo: el PDF no existe, es un enlace o esta fuera de la cache/temporal
        DestinoInvalido, // guardarComo: ruta vacia, relativa o es un directorio
    } tipo = Tipo::Escritura;
};

class AlmacenPdfCfdi {
public:
    virtual ~AlmacenPdfCfdi() = default;

    // PDF ya generado para `clave` (en la cache o en el temporal). Un acierto
    // marca el archivo como usado recientemente (expulsion LRU).
    virtual std::optional<QString> buscar(const ClavePdfCfdi& clave) = 0;

    // Escribe el PDF de forma atomica (temporal + rename) en la cache; si la
    // cache no se puede escribir, en el temporal de la app. Despues de cada
    // escritura expulsa los menos usados hasta respetar el limite (nunca el
    // recien escrito).
    virtual Resultado<PdfAlmacenado, ErrorAlmacenPdf> guardar(const ClavePdfCfdi& clave, const QByteArray& pdf) = 0;

    // Copia atomica (temporal en el directorio destino + rename) de un PDF de
    // la cache o del temporal a `destinoAbsoluto`. Reemplaza un destino
    // existente (el dialogo nativo ya pidio confirmacion). Si falla, el
    // destino queda como estaba.
    virtual Resultado<bool, ErrorAlmacenPdf> copiar(const QString& rutaPdf, const QString& destinoAbsoluto) = 0;

    // Directorios propios (para la lista permitida de "Abrir en Vista Previa").
    virtual QString directorioCache() const = 0;
    virtual QString directorioTemporal() const = 0;
};

} // namespace satcfdi
