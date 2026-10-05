#include "application/operaciones/TiposOperacion.h"
#include "application/operaciones/WorkerLocal.h"

namespace satcfdi {

QString claveEstable(TipoOperacion tipo)
{
    switch (tipo) {
    case TipoOperacion::Envio: return QStringLiteral("Envio");
    case TipoOperacion::Verificacion: return QStringLiteral("Verificacion");
    case TipoOperacion::Descarga: return QStringLiteral("Descarga");
    case TipoOperacion::VencimientoEstimado: return QStringLiteral("VencimientoEstimado");
    case TipoOperacion::Recuperacion: return QStringLiteral("Recuperacion");
    case TipoOperacion::RegistroIntencion: return QStringLiteral("RegistroIntencion");
    }
    return {};
}

QString claveEstable(ExistenciaArchivo existencia)
{
    switch (existencia) {
    case ExistenciaArchivo::Presente: return QStringLiteral("Presente");
    case ExistenciaArchivo::NoEncontrado: return QStringLiteral("NoEncontrado");
    case ExistenciaArchivo::ErrorComprobacion: return QStringLiteral("ErrorComprobacion");
    }
    return {};
}

QString claveEstable(EstadoWorker estado)
{
    switch (estado) {
    case EstadoWorker::Pausado: return QStringLiteral("Pausado");
    case EstadoWorker::ActivoEnEspera: return QStringLiteral("ActivoEnEspera");
    case EstadoWorker::Ejecutando: return QStringLiteral("Ejecutando");
    case EstadoWorker::Deteniendo: return QStringLiteral("Deteniendo");
    case EstadoWorker::Detenido: return QStringLiteral("Detenido");
    }
    return {};
}

} // namespace satcfdi
