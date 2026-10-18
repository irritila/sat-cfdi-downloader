#include "Barreras.h"

#include "EntornoQml.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QQuickItem>

namespace video {

namespace {

// Image.Ready y PdfDocument.Error (enumeraciones de QtQuick / QtQuick.Pdf).
constexpr int kImagenLista = 1;
constexpr int kPdfError = 4;

} // namespace

QString esperarDocumento(QQuickItem* raiz, int timeoutMs, const Bombear& bombear)
{
    QElapsedTimer t;
    t.start();
    while (true) {
        if (!bombear()) {
            return QStringLiteral("interrumpida");
        }
        QQuickItem* visor = buscar(raiz, QStringLiteral("visorPdfCfdi"));
        if (visor != nullptr) {
            if (visor->property("conError").toBool() || visor->property("estadoDocumento").toInt() == kPdfError) {
                return QStringLiteral("documentoError del visor PDF");
            }
            QQuickItem* pagina = buscar(visor, QStringLiteral("imagenPaginaPdf_0"));
            if (visor->property("documentoListo").toBool() && pagina != nullptr
                && pagina->property("status").toInt() == kImagenLista) {
                return {};
            }
        }
        if (t.elapsed() > timeoutMs) {
            return QStringLiteral("timeout de %1 ms esperando documentoListo del visor PDF").arg(timeoutMs);
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

QString esperarConteo(QQuickItem* raiz, const QString& objetivo, int conteo, int timeoutMs, const Bombear& bombear)
{
    QElapsedTimer t;
    t.start();
    while (true) {
        if (!bombear()) {
            return QStringLiteral("interrumpida");
        }
        QQuickItem* item = buscarRuta(raiz, objetivo);
        const int actual = item != nullptr ? item->property("count").toInt() : -1;
        if (actual == conteo) {
            return {};
        }
        if (t.elapsed() > timeoutMs) {
            return QStringLiteral("timeout de %1 ms esperando %2 elementos en '%3' (hay %4)")
                .arg(timeoutMs)
                .arg(conteo)
                .arg(objetivo, item == nullptr ? QStringLiteral("ninguno visible") : QString::number(actual));
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

} // namespace video
