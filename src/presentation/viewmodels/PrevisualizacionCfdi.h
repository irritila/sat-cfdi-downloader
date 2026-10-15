#pragma once

#include "domain/common/Resultado.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QFuture>
#include <QObject>
#include <QString>

namespace satcfdi {

// PDF de un CFDI listo para mostrar (T015.4, T015 D7). `rutaArchivo` es
// absoluta (cache de PDF o temporal de la app); `nombreSugerido` no lleva RFC
// ni nombres ("CFDI-<serie><folio>-<UUID 8>.pdf").
struct PdfPrevisualizado {
    QString rutaArchivo;
    QString nombreSugerido;
};

// Motivo por el que no hay PDF o no se pudo guardar (espejo de
// ErrorPrevisualizacion::Tipo de application; el root traduce).
enum class MotivoPrevisualizacion {
    OrigenNoDisponible, // el ZIP falta o cambio y no se pudo releer
    VersionNoSoportada, // CFDI de una version que la app no representa
    Ilegible,           // XML ilegible
    Cancelada,          // la operacion se cancelo (p. ej. al cerrar la app)
    Fallo,              // cualquier otro error (render, disco, copia)
};

// Previsualizacion de CFDI que la presentacion solicita (T015.4), como
// ConsultaCatalogoCfdi: el composition root la implementa sobre
// PrevisualizacionCfdiService; las pruebas, con fakes. El trabajo pesado
// ocurre fuera del hilo grafico; los futures se continuan en el hilo grafico
// (then(contexto, ...)). Un future cancelado o fallido se trata como Fallo.
// QML nunca ve el XML, el ZIP ni la cache: solo la URL del PDF ya generado.
class PrevisualizacionCfdi : public QObject {
    Q_OBJECT

public:
    using ResultadoGenerar = Resultado<PdfPrevisualizado, MotivoPrevisualizacion>;
    using ResultadoGuardar = Resultado<bool, MotivoPrevisualizacion>;

    using QObject::QObject;
    ~PrevisualizacionCfdi() override = default;

    // Genera (o toma de la cache) el PDF del CFDI en esa posicion del paquete.
    virtual QFuture<ResultadoGenerar> generar(const SolicitudId& solicitud, const QString& paqueteId,
                                              int posicion) = 0;
    // Copia el PDF a `destinoAbsoluto` (atomica). El dialogo nativo ya
    // confirmo la sobrescritura, si aplica.
    virtual QFuture<ResultadoGuardar> guardarComo(const QString& rutaPdf, const QString& destinoAbsoluto) = 0;
};

} // namespace satcfdi
