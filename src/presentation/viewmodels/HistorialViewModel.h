#pragma once

#include "FuenteHistorial.h"
#include "HistorialListModel.h"

#include "domain/cfdi/MesCalendario.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace satcfdi {

// Pantalla Historial (T023.5, Cmd+3; T023 D6, D14). Una fila por lote guiado
// del perfil (y "Descarga anterior" por solicitud previa a T023), mas
// reciente primero. Sin SQL ni SAT: todo por FuenteHistorial. El perfil lo
// fija AppViewModel (el de Facturas) con mostrarPerfil(); la ultima respuesta
// gana por token de generacion.
//
// Acciones (las ejecuta AppViewModel al recibir las senales):
// - verFacturas(fila): Facturas en el perfil y mes de la fila.
// - reintentar(fila): Facturas en ese mes y ejecuta su accion (Reintentar).
// - verDetallesTecnicos(fila, direccion): Diagnostico (DetalleSolicitudPage)
//   de la direccion elegida; "Atras" vuelve a Historial.
class HistorialViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea PresentacionViewModels.")

    Q_PROPERTY(QString perfilId READ perfilId NOTIFY perfilChanged)
    Q_PROPERTY(QString perfilRfc READ perfilRfc NOTIFY perfilChanged)
    Q_PROPERTY(QString perfilNombre READ perfilNombre NOTIFY perfilChanged)
    // "XAXX010101000 — Contadora Demo" (vacio sin perfil).
    Q_PROPERTY(QString subtitulo READ subtitulo NOTIFY perfilChanged)
    Q_PROPERTY(satcfdi::HistorialListModel* modelo READ modelo CONSTANT)
    // "sinPerfil" | "cargando" | "error" | "vacio" | "lista".
    Q_PROPERTY(QString vista READ vista NOTIFY vistaChanged)
    // Texto del estado vacio o error ("Aun no hay descargas de este perfil",
    // "No se pudo leer el historial").
    Q_PROPERTY(QString textoVista READ textoVista NOTIFY vistaChanged)
    // Recarga sin vaciar la lista (cambio de datos con la pagina visible).
    Q_PROPERTY(bool actualizando READ actualizando NOTIFY vistaChanged)
    // Live region: cambia una vez por carga terminada ("8 descargas").
    Q_PROPERTY(QString anuncio READ anuncio NOTIFY anuncioChanged)

public:
    // Direccion para verDetallesTecnicos. Ninguna = la unica disponible.
    enum class Direccion {
        Ninguna = -1,
        Emitidas = 0,
        Recibidas = 1,
    };
    Q_ENUM(Direccion)

    explicit HistorialViewModel(QObject* parent = nullptr);
    ~HistorialViewModel() override;

    // No propietaria; debe vivir mas que este objeto. nullptr = sin datos.
    void setFuente(FuenteHistorial* fuente);

    QString perfilId() const { return m_perfil.texto(); }
    QString perfilRfc() const { return m_perfilRfc; }
    QString perfilNombre() const { return m_perfilNombre; }
    QString subtitulo() const;
    HistorialListModel* modelo() const { return m_modelo; }
    QString vista() const { return m_vista; }
    QString textoVista() const { return m_textoVista; }
    bool actualizando() const { return m_actualizando; }
    QString anuncio() const { return m_anuncio; }

    // Perfil a mostrar (lo llama AppViewModel con el perfil de Facturas). Un
    // id vacio o no canonico deja la vista "sinPerfil".
    void mostrarPerfil(const QString& perfilId, const QString& rfc, const QString& nombre);
    // Al mostrar u ocultar la pagina: activa recarga y escucha cambios.
    Q_INVOKABLE void activar();
    Q_INVOKABLE void desactivar();
    Q_INVOKABLE void recargar();

    // false (sin emitir) si la fila no existe o la accion no aplica.
    Q_INVOKABLE bool verFacturas(int fila);
    Q_INVOKABLE bool reintentar(int fila);
    Q_INVOKABLE bool verDetallesTecnicos(int fila, satcfdi::HistorialViewModel::Direccion direccion = Direccion::Ninguna);

signals:
    void perfilChanged();
    void vistaChanged();
    void anuncioChanged();
    // perfilId canonico; mes 1..12.
    void facturasMesSolicitadas(const QString& perfilId, int anio, int mes);
    void reintentoSolicitado(const QString& perfilId, int anio, int mes);
    // solicitudId canonico (Diagnostico de esa direccion); "" (fila
    // Eliminada) -> lista de Diagnostico.
    void diagnosticoSolicitado(const QString& solicitudId);

private:
    struct Accion {
        std::optional<MesCalendario> mes;
        std::optional<SolicitudId> emitidas;
        std::optional<SolicitudId> recibidas;
    };

    void consultar(bool conservar);
    void aplicar(const FuenteHistorial::ResultadoEntradas& resultado);
    void setVista(const QString& vista, const QString& texto, bool actualizando);

    HistorialListModel* m_modelo;
    QPointer<FuenteHistorial> m_fuente;
    PerfilId m_perfil;
    QString m_perfilRfc;
    QString m_perfilNombre;
    QString m_vista = QStringLiteral("sinPerfil");
    QString m_textoVista;
    bool m_actualizando = false;
    QString m_anuncio;
    bool m_activo = false;
    quint64 m_generacion = 0;
    QHash<QString, Accion> m_acciones; // por FilaHistorial::clave
};

} // namespace satcfdi
