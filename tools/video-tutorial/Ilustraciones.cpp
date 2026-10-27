#include "Ilustraciones.h"

#include "infrastructure/os/MenuBarDefinicion.h"
#include "infrastructure/os/macos/IconoMenuBar.h"
#include "infrastructure/os/macos/MacOSMapeos.h"

#include <QDir>
#include <QImage>

#ifndef SATCFDI_VIDEO_QML_DIR
#error "SATCFDI_VIDEO_QML_DIR debe definirse en CMake"
#endif
#ifndef SATCFDI_CAPTURAS_QML_DIR
#error "SATCFDI_CAPTURAS_QML_DIR debe definirse en CMake"
#endif

namespace video {

namespace {

constexpr int kEscala = 2;

// D6 (T012): entradas visibles del menu bar desde la definicion compartida.
QVariantList entradasMenu()
{
    menubar::EstadoMenu estado;
    estado.monitoreo.fase = OSIntegration::EstadoMonitoreo::Fase::ActivoEnEspera;
    // T024.4 (7.1): monitoreo activo sin pendientes (la entrada se oculta).
    estado.monitoreo.pendientes = 0;
    estado.preferenciaLoginItem = true;
    estado.loginItem = OSIntegration::LoginItemStatus::Enabled;
    estado.notificaciones = OSIntegration::NotificationStatus::Granted;
    QVariantList r;
    for (const menubar::Entrada& e : menubar::entradas(estado)) {
        if (!e.visible) {
            continue;
        }
        r.append(QVariantMap{{QStringLiteral("texto"), e.texto},
                             {QStringLiteral("habilitada"), e.habilitada},
                             {QStringLiteral("separador"), e.separador},
                             {QStringLiteral("marcada"), e.marcable && e.marcada},
                             {QStringLiteral("icono"), e.icono}});
    }
    return r;
}

// Icono real del menu bar (dibujarIconoMenuBar) a 2x en el temporal.
QVariantMap varianteIcono(const QString& dir, OSIntegration::EstadoIcono estado, const QString& clave)
{
    QVariantMap v{{QStringLiteral("texto"), macos::textoEstadoIcono(estado)}};
    const QList<QPair<QString, int>> tamanos{{QStringLiteral("imagen"), 18 * kEscala},
                                             {QStringLiteral("ampliada"), 48 * kEscala}};
    for (const auto& [propiedad, lado] : tamanos) {
        const QString ruta = QDir(dir).filePath(QStringLiteral("icono-%1-%2.png").arg(clave).arg(lado));
        if (!macos::dibujarIconoMenuBar(estado, lado).save(ruta, "png")) {
            return {};
        }
        v.insert(propiedad, QUrl::fromLocalFile(ruta));
    }
    return v;
}

} // namespace

QString prepararIlustracion(const Accion& a, const DemoVideo& demo, const QString& dirTemporal, QUrl& archivo,
                            QVariantMap& p)
{
    using E = OSIntegration::EstadoIcono;
    p = a.parametros;
    QString qml;
    if (a.ilustracion == QLatin1String("menuBar")) {
        qml = QStringLiteral("IlustracionMenuBar.qml");
        const QVariantMap normal = varianteIcono(dirTemporal, E::Normal, QStringLiteral("normal"));
        if (normal.isEmpty()) {
            return QStringLiteral("no se pudo dibujar el icono del menu bar");
        }
        p.insert(QStringLiteral("entradas"), entradasMenu());
        p.insert(QStringLiteral("icono"), normal.value(QStringLiteral("imagen")));
    } else if (a.ilustracion == QLatin1String("iconoMenuBar")) {
        qml = QStringLiteral("IlustracionIconoMenuBar.qml");
        QVariantList variantes;
        const QList<QPair<E, QString>> estados{{E::Normal, QStringLiteral("normal")},
                                               {E::Pausado, QStringLiteral("pausado")},
                                               {E::Trabajando, QStringLiteral("trabajando")},
                                               {E::Atencion, QStringLiteral("atencion")}};
        for (const auto& [estado, clave] : estados) {
            const QVariantMap v = varianteIcono(dirTemporal, estado, clave);
            if (v.isEmpty()) {
                return QStringLiteral("no se pudo dibujar el icono del menu bar");
            }
            variantes.append(v);
        }
        p.insert(QStringLiteral("variantes"), variantes);
    } else if (a.ilustracion == QLatin1String("notificacion")) {
        qml = QStringLiteral("IlustracionNotificacion.qml");
        QString error;
        const QVariantList ns = demo.notificaciones(a.parametros, error);
        if (!error.isEmpty()) {
            return error;
        }
        p.insert(QStringLiteral("notificaciones"), ns);
    } else if (a.ilustracion == QLatin1String("finder")) {
        qml = QStringLiteral("IlustracionFinder.qml");
    } else if (a.ilustracion == QLatin1String("selectorCarpeta")) {
        // T024.4 (03-motion 2a): ilustracion propia del video.
        archivo = QUrl::fromLocalFile(QStringLiteral(SATCFDI_VIDEO_QML_DIR "/IlustracionSelectorCarpeta.qml"));
        return {};
    } else {
        qml = QStringLiteral("IlustracionSelectorArchivos.qml");
    }
    archivo = QUrl::fromLocalFile(QStringLiteral(SATCFDI_CAPTURAS_QML_DIR "/") + qml);
    return {};
}

} // namespace video
