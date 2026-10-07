#pragma once

#include "presentation/viewmodels/AccionesFinder.h"
#include "ports/OSIntegration.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QPromise>

#include <memory>

namespace satcfdi {

class AccesoPaquetesService;
class PaqueteSolicitudRepository;
class PersistenceDispatcher;
struct ResolucionRevelable;

// AccionesFinder (presentacion, T009.1) del composition root:
// 1. resuelve con AccesoPaquetesService (hilo del ejecutor; para un paquete,
//    antes mapea idPaqueteSat -> id local con PersistenceDispatcher);
// 2. con Disponible pide a OSIntegration mostrarEnFinder/abrirCarpetaEnFinder
//    con un peticionId nuevo;
// 3. completa el future con finderTerminado de ese peticionId.
// NoEncontrado/Error de la resolucion y NoEncontrado de Finder traen el texto
// D7; Fallido de Finder (o sin OSIntegration) "No se pudo abrir Finder".
// Solo lectura. Hilo grafico.
class AccesoFinder final : public QObject, public AccionesFinder {
    Q_OBJECT

public:
    AccesoFinder(AccesoPaquetesService& acceso, PersistenceDispatcher& dispatcher, PaqueteSolicitudRepository& paquetes,
                 QObject* parent = nullptr);
    ~AccesoFinder() override;

    // OSIntegration se conoce al iniciar el ciclo de vida (lo posee main).
    void setOS(OSIntegration* os);

    QFuture<ResultadoAccionFinder> mostrarPaquete(const SolicitudId& solicitud, const QString& idPaqueteSat) override;
    QFuture<ResultadoAccionFinder> abrirCarpetaSolicitud(const SolicitudId& solicitud) override;
    QFuture<ResultadoAccionFinder> abrirCarpetaPaquetes() override;

    static QString mensajeFinderFallido();

private:
    enum class Destino { Archivo, Carpeta };
    QFuture<ResultadoAccionFinder> revelar(QFuture<ResolucionRevelable> resolucion, Destino destino);
    void alTerminarFinder(const QString& peticionId, OSIntegration::ResultadoFinder resultado);

    AccesoPaquetesService& m_acceso;
    PersistenceDispatcher& m_dispatcher;
    PaqueteSolicitudRepository& m_paquetes;
    QPointer<OSIntegration> m_os;
    QMetaObject::Connection m_conexion;
    struct Pendiente {
        std::shared_ptr<QPromise<ResultadoAccionFinder>> promesa;
        Destino destino;
    };
    QHash<QString, Pendiente> m_pendientes;
};

} // namespace satcfdi
