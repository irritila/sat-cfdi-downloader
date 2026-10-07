#pragma once

#include "application/operaciones/TiposOperacion.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QFuture>
#include <QString>

namespace satcfdi {

class OperacionExecutor;

// Fachada de acceso a paquetes descargados (T009.1 D1, D2, D4, D6, D7) que
// plataforma conecta a la UI y a OSIntegration. No depende de OSIntegration:
// solo devuelve la ruta absoluta validada en ese momento o el motivo.
//
// Hilo: se llama desde el hilo grafico; la lectura de SQLite y la resolucion
// en el filesystem corren en el hilo del OperacionExecutor. El future se
// completa en ese hilo: continuar con then(contextoGrafico, ...).
// Solo lectura: no cambia estados, SQLite ni LogSolicitud, no crea
// directorios ni abre el ZIP. NoEncontrado/Error traen el texto D7
// ("Archivo local no encontrado" / "Carpeta de paquetes no encontrada").
class AccesoPaquetesService {
public:
    virtual ~AccesoPaquetesService() = default;

    // "Mostrar en Finder" de un paquete Descargado (id local del paquete).
    virtual QFuture<ResolucionRevelable> resolverArchivoPaquete(const QString& paqueteId) = 0;
    // "Abrir carpeta de la solicitud" (requiere un paquete Descargado).
    virtual QFuture<ResolucionRevelable> resolverCarpetaSolicitud(const SolicitudId& solicitudId) = 0;
    // "Abrir carpeta de paquetes" (menu bar y lista).
    virtual QFuture<ResolucionRevelable> resolverRaizPaquetes() = 0;
};

// Implementacion sobre el OperacionExecutor (referencia no propietaria; el
// ejecutor vive mas que la fachada).
class AccesoPaquetesEjecutor final : public AccesoPaquetesService {
public:
    explicit AccesoPaquetesEjecutor(OperacionExecutor& ejecutor) : m_ejecutor(ejecutor) {}

    QFuture<ResolucionRevelable> resolverArchivoPaquete(const QString& paqueteId) override;
    QFuture<ResolucionRevelable> resolverCarpetaSolicitud(const SolicitudId& solicitudId) override;
    QFuture<ResolucionRevelable> resolverRaizPaquetes() override;

private:
    OperacionExecutor& m_ejecutor;
};

} // namespace satcfdi
