#pragma once

#include "PrevisualizacionCfdi.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace satcfdi {

class AccionesFinder;
struct FilaCfdi;

// Visor del PDF de un CFDI dentro del explorador (T015.4, T015 D7). Lo crea y
// lo abre ExploradorCfdiViewModel al pedir la previsualizacion de una fila.
//
// Estados (`estado`): Inactivo (cerrado), Generando, Listo (`fuente` es la URL
// local del PDF) y Error (`motivo`, `errorMessage`, `errorDescripcion`;
// `puedeReintentar` salvo en version no soportada e ilegible, que no cambian
// al reintentar). Motivos: "origenNoDisponible", "versionNoSoportada",
// "ilegible", "cancelada", "fallo" y "documento" (el PDF existe pero el visor
// no pudo abrirlo: documentoIlegible()).
//
// La ultima peticion gana: abrir otra fila, reintentar o cerrar invalida las
// respuestas en curso (generacion, guardado y Vista Previa), que se ignoran.
//
// Acciones (solo con PDF): guardarEn(url) copia el PDF donde el usuario eligio
// en el dialogo nativo; abrirEnVistaPrevia() lo abre en Vista Previa
// (AccionesFinder). Sus resultados son mensajes en linea (`mensaje`,
// `mensajeEsError`), sin modales; un fallo no cambia nada mas.
class VisorPdfCfdiViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea ExploradorCfdiViewModel.")

    Q_PROPERTY(bool disponible READ disponible NOTIFY disponibleChanged)
    Q_PROPERTY(bool abierto READ abierto NOTIFY estadoChanged)
    Q_PROPERTY(Estado estado READ estado NOTIFY estadoChanged)
    Q_PROPERTY(QString titulo READ titulo NOTIFY cfdiChanged)
    Q_PROPERTY(QString subtitulo READ subtitulo NOTIFY cfdiChanged)
    Q_PROPERTY(QString nombreAccesibleCfdi READ nombreAccesibleCfdi NOTIFY cfdiChanged)
    Q_PROPERTY(QUrl fuente READ fuente NOTIFY estadoChanged)
    Q_PROPERTY(QString nombreSugerido READ nombreSugerido NOTIFY estadoChanged)
    Q_PROPERTY(QString motivo READ motivo NOTIFY estadoChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY estadoChanged)
    Q_PROPERTY(QString errorDescripcion READ errorDescripcion NOTIFY estadoChanged)
    Q_PROPERTY(bool puedeReintentar READ puedeReintentar NOTIFY estadoChanged)
    Q_PROPERTY(bool puedeGuardar READ puedeGuardar NOTIFY accionesChanged)
    Q_PROPERTY(bool puedeAbrirVistaPrevia READ puedeAbrirVistaPrevia NOTIFY accionesChanged)
    Q_PROPERTY(bool guardando READ guardando NOTIFY accionesChanged)
    Q_PROPERTY(bool abriendoVistaPrevia READ abriendoVistaPrevia NOTIFY accionesChanged)
    Q_PROPERTY(QString mensaje READ mensaje NOTIFY mensajeChanged)
    Q_PROPERTY(bool mensajeEsError READ mensajeEsError NOTIFY mensajeChanged)
    Q_PROPERTY(QUrl carpetaGuardado READ carpetaGuardado NOTIFY carpetaGuardadoChanged)
    Q_PROPERTY(QUrl archivoSugerido READ archivoSugerido NOTIFY carpetaGuardadoChanged)

public:
    enum class Estado {
        Inactivo,
        Generando,
        Listo,
        Error,
    };
    Q_ENUM(Estado)

    explicit VisorPdfCfdiViewModel(QObject* parent = nullptr);

    // No propietarios; nullptr = sin previsualizacion (el visor no se abre) o
    // sin Vista Previa.
    void setServicio(PrevisualizacionCfdi* servicio);
    void setAccionesFinder(AccionesFinder* acciones);

    bool disponible() const { return m_servicio != nullptr; }
    bool abierto() const { return m_estado != Estado::Inactivo; }
    Estado estado() const { return m_estado; }
    QString titulo() const { return m_titulo; }
    QString subtitulo() const { return m_subtitulo; }
    QString nombreAccesibleCfdi() const { return m_nombreAccesible; }
    QUrl fuente() const;
    QString nombreSugerido() const { return m_pdf ? m_pdf->nombreSugerido : QString(); }
    QString motivo() const { return m_motivo; }
    QString errorMessage() const;
    QString errorDescripcion() const;
    bool puedeReintentar() const;
    bool puedeGuardar() const;
    bool puedeAbrirVistaPrevia() const;
    bool guardando() const { return m_genGuardar != 0; }
    bool abriendoVistaPrevia() const { return m_genVistaPrevia != 0; }
    QString mensaje() const { return m_mensaje; }
    bool mensajeEsError() const { return m_mensajeEsError; }
    // Carpeta inicial del dialogo de guardado: la ultima usada en la sesion o
    // Documentos.
    QUrl carpetaGuardado() const;
    // carpetaGuardado + nombreSugerido (seleccion inicial del dialogo).
    QUrl archivoSugerido() const;

    // Abre el visor con el CFDI de la fila y pide su PDF. false (sin abrir)
    // si no hay servicio.
    bool abrir(const SolicitudId& solicitud, const FilaCfdi& fila);
    // Vuelve a pedir el PDF del CFDI abierto (estado Error).
    Q_INVOKABLE void reintentar();
    // Cierra el visor e ignora las respuestas en curso.
    Q_INVOKABLE void cerrar();
    // Copia el PDF al archivo elegido en el dialogo (URL local absoluta).
    Q_INVOKABLE void guardarEn(const QUrl& destino);
    Q_INVOKABLE void abrirEnVistaPrevia();
    // El visor QML no pudo abrir el PDF entregado (PdfDocument.Error).
    Q_INVOKABLE void documentoIlegible();

signals:
    void disponibleChanged();
    void estadoChanged();
    void cfdiChanged();
    void accionesChanged();
    void mensajeChanged();
    void carpetaGuardadoChanged();

private:
    struct Peticion {
        SolicitudId solicitud;
        QString paqueteId;
        int posicion = 0;
    };

    void generar();
    void aplicarGeneracion(quint64 generacion, const std::optional<PrevisualizacionCfdi::ResultadoGenerar>& r);
    void fijarEstado(Estado estado, const QString& motivo = {});
    QString rutaCarpetaGuardado() const;
    void setMensaje(const QString& mensaje, bool esError);
    void invalidarAcciones();

    QPointer<PrevisualizacionCfdi> m_servicio;
    AccionesFinder* m_finder = nullptr;

    Estado m_estado = Estado::Inactivo;
    std::optional<Peticion> m_peticion;
    std::optional<PdfPrevisualizado> m_pdf;
    QString m_motivo;
    QString m_titulo;
    QString m_subtitulo;
    QString m_nombreAccesible;

    // Tokens: != 0 = respuesta esperada con ese valor; las demas se ignoran.
    quint64 m_generacion = 0;
    quint64 m_genPdf = 0;
    quint64 m_genGuardar = 0;
    quint64 m_genVistaPrevia = 0;

    QString m_mensaje;
    bool m_mensajeEsError = false;
    QString m_carpetaGuardado;
};

} // namespace satcfdi
