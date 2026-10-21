#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

class AccionesFinder;
class ConsultaPrimerUso;

class EFirmaFormViewModel;
class ExploradorCfdiViewModel;
class FacturasViewModel;
class HistorialViewModel;
class ResolucionDestinoNotificacion;
struct DestinoNavegacion;
class PerfilesSatViewModel;
class SolicitudDetailViewModel;

// Navegacion del shell (T023.5: T023 D6, D10).
//
// Paginas del recorrido normal: Facturas (Cmd+1, principal), Perfiles SAT
// (Cmd+2) e Historial (Cmd+3). Fuera del recorrido normal, solo desde
// Historial, "Requiere atencion" / "Ver detalles tecnicos" de Facturas, el
// menu "Ir > Diagnostico" y las notificaciones: Diagnostico (lista tecnica de
// solicitudes, antes SolicitudesPage) y Detalle (DetalleSolicitudPage, vista
// de diagnostico de una solicitud). NuevaSolicitudPage ya no existe.
//
// "Atras" (regresar(), Escape): pila de origenes. Facturas y Perfiles son
// raices (vacian la pila). Abrir Historial, Diagnostico, Detalle o
// FacturasSolicitud apila la pagina actual; si la pagina destino ya esta en la
// pila, la pila se recorta hasta ella (sin ciclos). regresar() desapila; con
// pila vacia va a Facturas. `paginaOrigen`/`textoRegresar` describen el
// destino de regresar().
//
// Acciones:
// - abrirDetalle(id): Detalle desde la pagina actual; ids no canonicos se
//   rechazan. SolicitudDetailViewModel::eliminada(id) regresa al origen.
// - FacturasViewModel::detallesTecnicosSolicitados(perfil, anio, mes,
//   solicitudId): Detalle de esa solicitud (origen Facturas); solicitudId
//   vacio -> Diagnostico (lista). registrarEFirmaSolicitado(perfil) ->
//   Perfiles con ese perfil.
// - HistorialViewModel: facturasMesSolicitadas -> Facturas en ese perfil y
//   mes; reintentoSolicitado -> igual y ejecuta la accion del mes;
//   diagnosticoSolicitado(id) -> Detalle (origen Historial).
// - mostrarHistorial(): Historial del perfil seleccionado en Facturas.
//
// Notificaciones (T014.4 + T023.5, desde AppLifecycleController):
// - abrirSolicitudNotificada(id): resuelve el destino con
//   ResolucionDestinoNotificacion y llama abrirDestino(): FacturasMes ->
//   Facturas en ese perfil y mes (FacturasViewModel::irAMes, que se aplica
//   aunque Facturas aun no haya cargado perfiles: app arrancada por la
//   notificacion); Diagnostico -> Detalle (origen Facturas; si la carga
//   termina en NoEncontrada, Facturas sin error); Facturas -> Facturas con la
//   seleccion vigente. Id no canonico -> Facturas. Una navegacion explicita
//   posterior descarta una resolucion pendiente (token de generacion). Sin
//   resolucion configurada, se trata como Diagnostico.
// - mostrarPerfilSeleccionado(perfilId): Perfiles SAT con ese perfil.
// - mostrarCarpetaSolicitudEnFinder(id): flujo de T009.1. Si Finder no lo
//   muestra: Detalle con el aviso D7 (o Diagnostico con el aviso si la
//   solicitud ya no existe) y avisoFinderSolicitudPresentado().
//
// T015.3: abrirFacturas(id) abre el explorador de CFDI de la solicitud
// (FacturasSolicitud, desde Detalle) y regresarDeFacturas() equivale a
// regresar() desde esa pagina.
class AppViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea el composition root.")

    Q_PROPERTY(Pagina pagina READ pagina NOTIFY paginaChanged)
    Q_PROPERTY(QString solicitudSeleccionadaId READ solicitudSeleccionadaId
                   NOTIFY solicitudSeleccionadaIdChanged)
    // T023.5: destino de regresar() ("Atras"). puedeRegresar es false en las
    // raices (Facturas, Perfiles). textoRegresar: "Facturas", "Historial",
    // "Diagnóstico", "Perfiles SAT" o "Detalles técnicos".
    Q_PROPERTY(Pagina paginaOrigen READ paginaOrigen NOTIFY paginaChanged)
    Q_PROPERTY(bool puedeRegresar READ puedeRegresar NOTIFY paginaChanged)
    Q_PROPERTY(QString textoRegresar READ textoRegresar NOTIFY paginaChanged)
    // T009 D9: las notificaciones del sistema estan denegadas o no disponibles
    // (lo fija el composition root desde OSIntegration). No afecta estados.
    Q_PROPERTY(bool notificacionesDeshabilitadas READ notificacionesDeshabilitadas
                   NOTIFY notificacionesDeshabilitadasChanged)
    // T009.1: "Abrir carpeta de paquetes" (lista y menu bar). mensajeFinder:
    // aviso D7 accesible cuando no se pudo abrir (vacio si se abrio).
    Q_PROPERTY(bool puedeAbrirCarpetaPaquetes READ puedeAbrirCarpetaPaquetes NOTIFY mensajeFinderChanged)
    Q_PROPERTY(QString mensajeFinder READ mensajeFinder NOTIFY mensajeFinderChanged)
    // T014.1 D1: filtros de la lista durante la sesion (no se persisten).
    // SolicitudesPage los enlaza a su SolicitudesFiltroModel.
    Q_PROPERTY(QString filtroTexto READ filtroTexto WRITE setFiltroTexto NOTIFY filtrosChanged)
    Q_PROPERTY(QString filtroEstado READ filtroEstado WRITE setFiltroEstado NOTIFY filtrosChanged)
    Q_PROPERTY(QString filtroTipo READ filtroTipo WRITE setFiltroTipo NOTIFY filtrosChanged)
    Q_PROPERTY(QString filtroMes READ filtroMes WRITE setFiltroMes NOTIFY filtrosChanged)
    // T014.3 D1: primer uso, derivado de ConsultaPrimerUso (sin bandera).
    // - primerUsoCargando: consulta en curso sin un resultado previo.
    // - primerUsoError: la ultima consulta fallo (y no hay resultado previo).
    // - primerUsoPasos: [hayPerfiles, hayEFirmaLista, haySolicitudes].
    // - mostrarGuiaPrimerUso: hay resultado, no carga ni fallo y aun no hay
    //   solicitudes. Un refresco con resultado previo lo conserva (sin parpadeo).
    Q_PROPERTY(bool primerUsoCargando READ primerUsoCargando NOTIFY primerUsoChanged)
    Q_PROPERTY(bool primerUsoError READ primerUsoError NOTIFY primerUsoChanged)
    Q_PROPERTY(QVariantList primerUsoPasos READ primerUsoPasos NOTIFY primerUsoChanged)
    Q_PROPERTY(bool mostrarGuiaPrimerUso READ mostrarGuiaPrimerUso NOTIFY primerUsoChanged)

public:
    enum class Pagina {
        Facturas,          // T023.4: pantalla principal (Facturas por mes)
        Historial,         // T023.5: historial de descargas del perfil (Cmd+3)
        Perfiles,
        Diagnostico,       // T023.5: lista tecnica de solicitudes ("Ir > Diagnostico")
        Detalle,           // DetalleSolicitudPage (vista de diagnostico)
        FacturasSolicitud, // explorador de CFDI de una solicitud (T015.3, desde Detalle)
    };
    Q_ENUM(Pagina)

    // Todos los view models son obligatorios y deben vivir mas que este objeto.
    AppViewModel(SolicitudDetailViewModel* detalle,
                 PerfilesSatViewModel* perfiles,
                 EFirmaFormViewModel* eFirma,
                 QObject* parent = nullptr);

    Pagina pagina() const { return m_pagina; }
    QString solicitudSeleccionadaId() const { return m_solicitudSeleccionadaId; }
    Pagina paginaOrigen() const { return m_origenes.isEmpty() ? Pagina::Facturas : m_origenes.constLast(); }
    bool puedeRegresar() const;
    QString textoRegresar() const;

    bool notificacionesDeshabilitadas() const { return m_notificacionesDeshabilitadas; }
    void setNotificacionesDeshabilitadas(bool valor)
    {
        if (m_notificacionesDeshabilitadas != valor) {
            m_notificacionesDeshabilitadas = valor;
            emit notificacionesDeshabilitadasChanged();
        }
    }

    void setAccionesFinder(AccionesFinder* acciones)
    {
        m_finder = acciones;
        emit mensajeFinderChanged();
    }
    bool puedeAbrirCarpetaPaquetes() const { return m_finder != nullptr; }
    QString mensajeFinder() const { return m_mensajeFinder; }
    void setMensajeFinder(const QString& mensaje)
    {
        if (m_mensajeFinder != mensaje) {
            m_mensajeFinder = mensaje;
            emit mensajeFinderChanged();
        }
    }
    QString filtroTexto() const { return m_filtroTexto; }
    void setFiltroTexto(const QString& valor) { cambiarFiltro(m_filtroTexto, valor); }
    QString filtroEstado() const { return m_filtroEstado; }
    void setFiltroEstado(const QString& valor) { cambiarFiltro(m_filtroEstado, valor); }
    QString filtroTipo() const { return m_filtroTipo; }
    void setFiltroTipo(const QString& valor) { cambiarFiltro(m_filtroTipo, valor); }
    QString filtroMes() const { return m_filtroMes; }
    void setFiltroMes(const QString& valor) { cambiarFiltro(m_filtroMes, valor); }
    Q_INVOKABLE void limpiarFiltros();

    Q_INVOKABLE void abrirCarpetaPaquetes();

    // T014.3 D1: no propietaria; nullptr = sin guia (pruebas, shell). Al
    // fijarla se consulta; refrescarPrimerUso() se llama cuando cambian la
    // lista, los perfiles o una credencial (PresentacionViewModels).
    void setConsultaPrimerUso(ConsultaPrimerUso* consulta);
    Q_INVOKABLE void refrescarPrimerUso();
    bool primerUsoCargando() const { return m_primerUsoCargando; }
    bool primerUsoError() const { return m_primerUsoError; }
    QVariantList primerUsoPasos() const;
    bool mostrarGuiaPrimerUso() const;

    Q_INVOKABLE void mostrarPerfiles();
    // T023.5: Historial (Cmd+3) del perfil de Facturas; no navega sin
    // historial configurado.
    Q_INVOKABLE void mostrarHistorial();
    // T023.5: "Ir > Diagnostico" (lista tecnica de solicitudes).
    Q_INVOKABLE void mostrarDiagnostico();
    // T023.5: "Atras" / Escape en Historial, Diagnostico, Detalle y
    // FacturasSolicitud. Sin efecto en las raices.
    Q_INVOKABLE void regresar();
    // T023.5: no propietario; nullptr = sin Historial.
    void setHistorial(HistorialViewModel* historial);
    // T023.5: no propietario; nullptr = las notificaciones de solicitud abren
    // su Detalle (comportamiento de T014.4 con respaldo a Facturas).
    void setResolucionDestinos(ResolucionDestinoNotificacion* resolucion);
    // T023.5: Facturas en ese perfil y mes (Historial, notificaciones).
    // `reintentar`: ejecuta la accion del mes si la franja ofrece Reintentar.
    void abrirFacturasMes(const QString& perfilId, int anio, int mes, bool reintentar = false);
    void abrirDestino(const DestinoNavegacion& destino);
    // T023.4: pantalla Facturas (Cmd+1). Activa el view model al entrar y lo
    // desactiva al salir.
    Q_INVOKABLE void mostrarFacturas();
    // T023.4: no propietario; nullptr = sin pantalla Facturas (mostrarFacturas
    // no navega). "Registrar e.firma..." abre Perfiles con ese perfil y "Ver
    // detalles tecnicos" abre la lista de diagnostico actual (T023.5 la
    // sustituye por Diagnostico del mes).
    void setFacturas(FacturasViewModel* facturas);
    // T015.3: no propietario; nullptr = sin explorador (abrirFacturas no navega).
    void setExplorador(ExploradorCfdiViewModel* explorador);
    // false (sin navegar) si no hay explorador o `id` no es canonico.
    Q_INVOKABLE bool abrirFacturas(const QString& id);
    Q_INVOKABLE void regresarDeFacturas();
    // Devuelve false (sin navegar) si `id` no es un UUID canonico.
    Q_INVOKABLE bool abrirDetalle(const QString& id);

    // T014.4/T023.5: ver la descripcion de la clase.
    void abrirSolicitudNotificada(const QString& id);
    void mostrarPerfilSeleccionado(const QString& perfilId);
    void mostrarCarpetaSolicitudEnFinder(const QString& id);

signals:
    void paginaChanged();
    void solicitudSeleccionadaIdChanged();
    void notificacionesDeshabilitadasChanged();
    void mensajeFinderChanged();
    void filtrosChanged();
    void primerUsoChanged();
    // T014.4: el resultado de mostrarCarpetaSolicitudEnFinder no fue Mostrado
    // y el aviso ya esta visible en el detalle o en la lista.
    void avisoFinderSolicitudPresentado();

private:
    // Raiz (Facturas, Perfiles): vacia la pila. Secundaria: apila el origen.
    void irARaiz(Pagina pagina);
    void irASecundaria(Pagina pagina);
    void setPagina(Pagina pagina);
    void cargarDetalle(const QString& id);
    void navegarADetalleConRespaldo(const QString& id, const QString& avisoFinder, Pagina respaldo);
    void abrirDiagnosticoDe(const QString& solicitudId);
    void alCambiarEstadoDetalle();
    void cambiarFiltro(QString& campo, const QString& valor)
    {
        if (campo != valor) {
            campo = valor;
            emit filtrosChanged();
        }
    }

    QPointer<SolicitudDetailViewModel> m_detalle;
    QPointer<PerfilesSatViewModel> m_perfiles;
    QPointer<EFirmaFormViewModel> m_eFirma;
    QPointer<ExploradorCfdiViewModel> m_explorador;
    QPointer<FacturasViewModel> m_facturas;
    QPointer<HistorialViewModel> m_historial;
    ResolucionDestinoNotificacion* m_resolucion = nullptr;
    quint64 m_genDestino = 0;
    QList<Pagina> m_origenes; // pila de "Atras" (T023.5)
    Pagina m_pagina = Pagina::Diagnostico;
    QString m_solicitudSeleccionadaId;
    bool m_notificacionesDeshabilitadas = false;
    AccionesFinder* m_finder = nullptr;
    QString m_mensajeFinder;
    quint64 m_genFinder = 0;
    // T014.4/T023.5: detalle abierto por una notificacion; si la carga
    // termina en NoEncontrada se va a m_respaldo (Facturas, o Diagnostico con
    // el aviso de Finder).
    QString m_detalleConRespaldoLista;
    Pagina m_respaldo = Pagina::Facturas;
    QString m_avisoFinderPendiente;
    quint64 m_genFinderSolicitud = 0;
    QString m_filtroTexto;
    QString m_filtroEstado;
    QString m_filtroTipo;
    QString m_filtroMes;
    ConsultaPrimerUso* m_consultaPrimerUso = nullptr;
    quint64 m_genPrimerUso = 0;
    bool m_primerUsoCargando = false;
    bool m_primerUsoError = false;
    bool m_primerUsoConocido = false;
    bool m_hayPerfiles = false;
    bool m_hayEFirmaLista = false;
    bool m_haySolicitudes = false;
};

} // namespace satcfdi
