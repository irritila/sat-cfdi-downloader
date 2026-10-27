#include "BarreraExportacion.h"

#include "presentation/viewmodels/ExportacionFacturasViewModel.h"
#include "presentation/viewmodels/RespaldoViewModel.h"

#include <QCoreApplication>
#include <QElapsedTimer>

namespace video {

QString esperarExportacion(satcfdi::ExportacionFacturasViewModel* vm, const QString& esperado, int timeoutMs,
                           const std::function<bool()>& bombear)
{
    using Estado = satcfdi::ExportacionFacturasViewModel::Estado;
    if (!resultadosExportacion().contains(esperado)) {
        return QStringLiteral("resultado esperado desconocido \"%1\"").arg(esperado);
    }
    if (vm == nullptr || !vm->disponible()) {
        return QStringLiteral("la exportacion no esta disponible");
    }
    if (vm->estado() == Estado::Inactivo) {
        return QStringLiteral("no hay una exportacion abierta");
    }
    QElapsedTimer t;
    t.start();
    while (vm->estado() == Estado::Planeando || vm->estado() == Estado::Exportando) {
        if (!bombear()) {
            return QStringLiteral("interrumpida");
        }
        if (t.elapsed() > timeoutMs) {
            return QStringLiteral("timeout de %1 ms esperando el resultado de la exportacion").arg(timeoutMs);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    switch (vm->estado()) {
    case Estado::Dialogo:
        if (!vm->errorPlan().isEmpty()) {
            return QStringLiteral("la exportacion volvio al dialogo por un error del plan: %1").arg(vm->errorPlan());
        }
        if (!vm->errorDestino().isEmpty()) {
            return QStringLiteral("la exportacion volvio al dialogo por un error del destino: %1")
                .arg(vm->errorDestino());
        }
        return QStringLiteral("la exportacion sigue en el dialogo (no se inicio)");
    case Estado::Inactivo:
        return QStringLiteral("la exportacion se cerro sin resultado");
    case Estado::EsperandoNombre:
        return esperado == QLatin1String("esperandoNombre")
                   ? QString()
                   : QStringLiteral("la exportacion espera otro nombre de archivo; se esperaba \"%1\"").arg(esperado);
    case Estado::Resultado:
        if (vm->varianteResultado() == esperado) {
            return {};
        }
        return QStringLiteral("resultado \"%1\" (%2); se esperaba \"%3\"")
            .arg(vm->varianteResultado(), vm->tituloResultado(), esperado);
    case Estado::Planeando:
    case Estado::Exportando:
        break;
    }
    return QStringLiteral("estado de exportacion inesperado");
}

QString esperarRespaldo(satcfdi::RespaldoViewModel* vm, const QString& esperado, int timeoutMs,
                        const std::function<bool()>& bombear)
{
    const QString variante = esperado == QLatin1String("conAvisos") ? QStringLiteral("advertencia")
                             : esperado == QLatin1String("error")   ? QStringLiteral("error")
                                                                    : QStringLiteral("exito");
    if (vm == nullptr || !vm->disponible()) {
        return QStringLiteral("el respaldo no esta disponible");
    }
    if (!vm->respaldando() && !vm->hayResultado()) {
        return QStringLiteral("no hay un respaldo en curso ni un resultado (falta pulsar \"Respaldar ahora\")");
    }
    QElapsedTimer t;
    t.start();
    while (vm->respaldando() || !vm->hayResultado()) {
        if (!bombear()) {
            return QStringLiteral("interrumpida");
        }
        if (t.elapsed() > timeoutMs) {
            return QStringLiteral("timeout de %1 ms esperando el resultado del respaldo (falta resultadoRespaldo)")
                .arg(timeoutMs);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    if (vm->varianteResultado() != variante) {
        return QStringLiteral("resultado de respaldo \"%1\" (%2); se esperaba \"%3\"")
            .arg(vm->varianteResultado(), vm->tituloResultado(), variante);
    }
    return {};
}

} // namespace video
