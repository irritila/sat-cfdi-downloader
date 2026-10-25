#pragma once

// T026.2: CfdiPdfRenderer falso para las pruebas del servicio de exportacion.
// Falla (ErrorRenderPdf::Fallo) o entrega un PDF minimo segun `fallar`, cuenta
// instancias creadas por la fabrica, llamadas a renderizar() y el hilo de cada
// llamada. Thread-safe (el servicio lo usa en su hilo; la prueba inspecciona
// desde el grafico).

#include "application/cfdi/ExportacionFacturasService.h"
#include "ports/cfdi/CfdiPdfRenderer.h"

#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QThread>

#include <atomic>
#include <memory>

namespace fakes {

class FakeCfdiPdfRenderer final : public satcfdi::CfdiPdfRenderer {
public:
    struct Estado {
        std::atomic<bool> fallar{true};
        std::atomic<int> creados{0};
        std::atomic<int> llamadas{0};
        mutable QMutex mutex;
        QSet<QThread*> hilos;

        QSet<QThread*> hilosUsados() const
        {
            QMutexLocker l(&mutex);
            return hilos;
        }
    };

    explicit FakeCfdiPdfRenderer(std::shared_ptr<Estado> estado)
        : m_estado(std::move(estado))
    {
    }

    int version() const override { return 1; }

    satcfdi::Resultado<satcfdi::PdfRenderizado, satcfdi::ErrorRenderPdf>
    renderizar(const satcfdi::ResultadoParseoCfdi&) override
    {
        using R = satcfdi::Resultado<satcfdi::PdfRenderizado, satcfdi::ErrorRenderPdf>;
        ++m_estado->llamadas;
        {
            QMutexLocker l(&m_estado->mutex);
            m_estado->hilos.insert(QThread::currentThread());
        }
        if (m_estado->fallar.load()) {
            return R::fallo(satcfdi::ErrorRenderPdf{satcfdi::ErrorRenderPdf::Tipo::Fallo});
        }
        return R::exito(satcfdi::PdfRenderizado{QByteArray("%PDF-1.4\n% falso\n"), 1});
    }

    // Una instancia nueva por exportacion (D13).
    static satcfdi::FabricaRendererPdf fabrica(std::shared_ptr<Estado> estado)
    {
        return [estado]() -> std::unique_ptr<satcfdi::CfdiPdfRenderer> {
            ++estado->creados;
            return std::make_unique<FakeCfdiPdfRenderer>(estado);
        };
    }

private:
    std::shared_ptr<Estado> m_estado;
};

} // namespace fakes
