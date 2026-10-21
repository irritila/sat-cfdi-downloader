#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

// Franja de disponibilidad del mes elegido en Facturas (T023.4, D7, D11,
// D17). Solo representa: la calcula y actualiza FacturasViewModel. Todos los
// textos estan en lenguaje de usuario (sin "solicitud", "paquete" ni
// "verificacion"). El estado nunca depende solo del color: `estadoTexto` e
// `icono` acompanan al `tono`.
class FranjaMesViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo expone FacturasViewModel.franja.")

    Q_PROPERTY(Estado estado READ estado NOTIFY cambio)
    Q_PROPERTY(QString estadoTexto READ estadoTexto NOTIFY cambio)
    Q_PROPERTY(QString tono READ tono NOTIFY cambio)
    Q_PROPERTY(QString icono READ icono NOTIFY cambio)
    Q_PROPERTY(QString titular READ titular NOTIFY cambio)
    Q_PROPERTY(QString descripcion READ descripcion NOTIFY cambio)
    // Accion principal (Cmd+R) y su texto; Ninguna = sin boton.
    Q_PROPERTY(Accion accion READ accion NOTIFY cambio)
    Q_PROPERTY(QString textoAccion READ textoAccion NOTIFY cambio)
    Q_PROPERTY(bool accionHabilitada READ accionHabilitada NOTIFY cambio)
    // Motivo accesible cuando la accion esta deshabilitada ("" si habilitada).
    Q_PROPERTY(QString motivoDeshabilitada READ motivoDeshabilitada NOTIFY cambio)
    // Accion en curso (planificando o confirmando): progreso indeterminado
    // corto en el boton, no la etapa del mes.
    Q_PROPERTY(bool ejecutando READ ejecutando NOTIFY cambio)
    // Preparando: progreso indeterminado con la etapa en lenguaje de usuario
    // y la nota "Puedes cerrar la ventana; te avisaremos".
    Q_PROPERTY(bool ocupado READ ocupado NOTIFY cambio)
    Q_PROPERTY(QString etapa READ etapa NOTIFY cambio)
    Q_PROPERTY(QString nota READ nota NOTIFY cambio)
    // "15 de octubre, 14:05" (cobertura contigua); "" si no aplica.
    Q_PROPERTY(QString hasta READ hasta NOTIFY cambio)
    // Requiere atencion: causa en lenguaje de usuario.
    Q_PROPERTY(QString causa READ causa NOTIFY cambio)
    // Enlace secundario "Ver detalles tecnicos" (destino de diagnostico,
    // T023.5); vacio = sin enlace.
    Q_PROPERTY(QString textoEnlace READ textoEnlace NOTIFY cambio)
    // Aviso puntual (p. ej. confirmacion cancelada, plan desactualizado);
    // "" sin aviso.
    Q_PROPERTY(QString aviso READ aviso NOTIFY cambio)
    // Resumen accesible de la franja: "<estado>. <titular>. <descripcion>".
    Q_PROPERTY(QString nombreAccesible READ nombreAccesible NOTIFY cambio)

public:
    enum class Estado {
        SinPerfil,
        Cargando,
        SinDescargar,
        Preparando,
        Disponibles,
        DisponiblesParcialmente,
        RequiereAtencion,
        Futuro,
        Error, // no se pudo leer el estado del mes
    };
    Q_ENUM(Estado)

    enum class Accion {
        Ninguna,
        DescargarFacturas,
        ActualizarMes,
        CompletarMes,
        Reintentar,       // causa de atencion recuperable con la accion del mes
        RegistrarEFirma,  // abre Perfiles SAT con el perfil
        VerDetalles,      // 5005 u otra causa sin accion: lleva al diagnostico
        ReintentarLectura,// Error: volver a leer el mes
    };
    Q_ENUM(Accion)

    // Valores que calcula FacturasViewModel.
    struct Datos {
        Estado estado = Estado::SinPerfil;
        QString estadoTexto;
        QString tono;
        QString icono;
        QString titular;
        QString descripcion;
        Accion accion = Accion::Ninguna;
        QString textoAccion;
        bool accionHabilitada = false;
        QString motivoDeshabilitada;
        bool ejecutando = false;
        bool ocupado = false;
        QString etapa;
        QString nota;
        QString hasta;
        QString causa;
        QString textoEnlace;
        QString aviso;

        friend bool operator==(const Datos&, const Datos&) = default;
    };

    explicit FranjaMesViewModel(QObject* parent = nullptr);

    Estado estado() const { return m_d.estado; }
    QString estadoTexto() const { return m_d.estadoTexto; }
    QString tono() const { return m_d.tono; }
    QString icono() const { return m_d.icono; }
    QString titular() const { return m_d.titular; }
    QString descripcion() const { return m_d.descripcion; }
    Accion accion() const { return m_d.accion; }
    QString textoAccion() const { return m_d.textoAccion; }
    bool accionHabilitada() const { return m_d.accionHabilitada; }
    QString motivoDeshabilitada() const { return m_d.motivoDeshabilitada; }
    bool ejecutando() const { return m_d.ejecutando; }
    bool ocupado() const { return m_d.ocupado; }
    QString etapa() const { return m_d.etapa; }
    QString nota() const { return m_d.nota; }
    QString hasta() const { return m_d.hasta; }
    QString causa() const { return m_d.causa; }
    QString textoEnlace() const { return m_d.textoEnlace; }
    QString aviso() const { return m_d.aviso; }
    QString nombreAccesible() const;
    const Datos& datos() const { return m_d; }

    // Uso de FacturasViewModel. Emite `cambio` solo si algo cambio.
    void fijar(const Datos& datos);

signals:
    void cambio();

private:
    Datos m_d;
};

} // namespace satcfdi
