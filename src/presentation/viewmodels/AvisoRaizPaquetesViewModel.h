#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

// T019 D8: aviso de la carpeta de paquetes en la ventana principal, cuando la
// migracion de la carpeta anterior a ~/Archivo-Fiscal/paquetes no se pudo
// completar (conflicto o error) o la raiz nueva no esta disponible.
//
// - Lo fija el composition root una sola vez, antes de cargar Main.qml, a
//   partir del AvisoRaizPaquetes de app_core (presentation no depende de
//   app_core: recibe sus propios Datos). Llamadas posteriores a fijar() se
//   ignoran: el aviso no cambia ni se descarta durante la sesion.
// - Sin aviso (Ninguno), `activo` es false y los textos estan vacios.
// - Las rutas llegan ya relativas al home (`~/...`); este view model no toca
//   el sistema de archivos ni compone rutas absolutas.
// - No reutiliza AppViewModel::mensajeFinder (resultado de una accion de
//   Finder): este aviso describe el arranque y dura toda la sesion.
class AvisoRaizPaquetesViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea el composition root.")

    Q_PROPERTY(bool activo READ activo NOTIFY avisoChanged)
    Q_PROPERTY(Motivo motivo READ motivo NOTIFY avisoChanged)
    // Variante de AvisoEnLinea: "advertencia" (se usa la carpeta anterior) o
    // "error" (la carpeta de esta sesion no esta disponible).
    Q_PROPERTY(QString variante READ variante NOTIFY avisoChanged)
    Q_PROPERTY(QString titulo READ titulo NOTIFY avisoChanged)
    Q_PROPERTY(QString descripcion READ descripcion NOTIFY avisoChanged)
    Q_PROPERTY(QString rutaLegada READ rutaLegada NOTIFY avisoChanged)
    Q_PROPERTY(QString rutaNueva READ rutaNueva NOTIFY avisoChanged)
    Q_PROPERTY(QString rutaEfectiva READ rutaEfectiva NOTIFY avisoChanged)

public:
    enum class Motivo {
        Ninguno,
        Conflicto,        // la carpeta nueva ya tenia contenido: se usa la anterior
        Error,            // fallo al mover: se usa la anterior, nada se movio; o la
                          // ruta anterior no es carpeta: se usa la nueva (rutaEfectiva == rutaNueva)
        RaizNoDisponible, // la carpeta nueva no se pudo crear o no es carpeta
    };
    Q_ENUM(Motivo)

    struct Datos {
        Motivo motivo = Motivo::Ninguno;
        QString rutaLegada;   // carpeta anterior, relativa al home
        QString rutaNueva;    // "~/Archivo-Fiscal/paquetes"
        QString rutaEfectiva; // la que usa esta sesion
    };

    explicit AvisoRaizPaquetesViewModel(QObject* parent = nullptr);

    // Fija el aviso de la sesion. Solo la primera llamada con un motivo
    // distinto de Ninguno tiene efecto.
    void fijar(const Datos& datos);

    bool activo() const { return m_datos.motivo != Motivo::Ninguno; }
    Motivo motivo() const { return m_datos.motivo; }
    QString variante() const;
    QString titulo() const;
    QString descripcion() const;
    QString rutaLegada() const { return m_datos.rutaLegada; }
    QString rutaNueva() const { return m_datos.rutaNueva; }
    QString rutaEfectiva() const { return m_datos.rutaEfectiva; }

signals:
    void avisoChanged();

private:
    Datos m_datos;
};

} // namespace satcfdi
