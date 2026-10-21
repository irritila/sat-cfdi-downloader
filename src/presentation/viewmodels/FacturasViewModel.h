#pragma once

#include "FacturasListModel.h"
#include "FuenteFacturasMes.h"
#include "MesesFacturasModel.h"
#include "PerfilesDisponiblesModel.h"
#include "FranjaMesViewModel.h"
#include "VisorPdfCfdiViewModel.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

namespace satcfdi {

class PrevisualizacionCfdi;

// Pantalla Facturas (T023.4): perfil, mes, franja de disponibilidad, pestanas
// Emitidas/Recibidas con solo la contraparte (D20), busqueda, filtro y PDF.
// Sin SQL, archivos ni SAT: todo por FuenteFacturasMes (futures continuados
// en el hilo grafico; la ultima respuesta gana por token de generacion).
//
// Seleccion inicial (activar()): ultimo perfil usado (PreferenciasFacturas)
// si sigue visible; si no, el primero. Mes: el anterior al mes en curso en
// Centro de Mexico (en enero, diciembre del anio previo). Cambiar de perfil
// guarda la preferencia y conserva el ultimo mes visto de cada perfil en la
// sesion (D16).
//
// Textos: todos los visibles se generan aqui o en FranjaMesViewModel, sin
// "solicitud", "paquete" ni "verificacion" (prueba de presentacion).
//
// Accion de la franja (ejecutarAccion, Cmd+R): planifica la accion ofrecida y
// la ejecuta. Si una direccion requiere confirmacion (descarga equivalente
// fallida o vencida), expone `confirmacion*` y espera confirmar()/cancelar()
// de UNA direccion a la vez; cancelar una no afecta la otra. PlanDesactualizado
// vuelve a planificar una vez y, si sigue cambiando, muestra el aviso.
// EFirmaNoLista deja la accion deshabilitada con motivo y ofrece
// "Registrar e.firma..." (senal registrarEFirmaSolicitado). Descargas
// guardadas pero sin enviar: la franja ofrece la accion del mes, que las
// reenvia sin crear nuevas (ADR 0017).
class FacturasViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea PresentacionViewModels.")

    // ---- Perfil ----
    Q_PROPERTY(satcfdi::PerfilesDisponiblesModel* perfiles READ perfiles CONSTANT)
    Q_PROPERTY(int perfilIndice READ perfilIndice NOTIFY perfilChanged)
    Q_PROPERTY(QString perfilId READ perfilId NOTIFY perfilChanged)
    Q_PROPERTY(QString perfilNombre READ perfilNombre NOTIFY perfilChanged)
    Q_PROPERTY(QString perfilRfc READ perfilRfc NOTIFY perfilChanged)
    Q_PROPERTY(bool hayPerfiles READ hayPerfiles NOTIFY perfilChanged)
    Q_PROPERTY(bool eFirmaLista READ eFirmaLista NOTIFY perfilChanged)
    // Motivo accesible si la e.firma no esta lista ("" si lista).
    Q_PROPERTY(QString motivoEFirma READ motivoEFirma NOTIFY perfilChanged)

    // ---- Mes ----
    Q_PROPERTY(int anio READ anio NOTIFY mesChanged)
    Q_PROPERTY(int mes READ mes NOTIFY mesChanged)
    Q_PROPERTY(QString tituloMes READ tituloMes NOTIFY mesChanged)          // "Septiembre 2026"
    Q_PROPERTY(bool puedeMesAnterior READ puedeMesAnterior NOTIFY mesChanged)
    Q_PROPERTY(bool puedeMesSiguiente READ puedeMesSiguiente NOTIFY mesChanged) // no a meses futuros
    Q_PROPERTY(bool puedeAnioAnterior READ puedeAnioAnterior NOTIFY mesChanged)
    Q_PROPERTY(bool puedeAnioSiguiente READ puedeAnioSiguiente NOTIFY mesChanged)
    Q_PROPERTY(satcfdi::MesesFacturasModel* meses READ meses CONSTANT)
    Q_PROPERTY(satcfdi::FranjaMesViewModel* franja READ franja CONSTANT)

    // ---- Pestanas y lista ----
    Q_PROPERTY(Direccion direccion READ direccion WRITE setDireccion NOTIFY direccionChanged)
    Q_PROPERTY(int conteoEmitidas READ conteoEmitidas NOTIFY conteosChanged)
    Q_PROPERTY(int conteoRecibidas READ conteoRecibidas NOTIFY conteosChanged)
    Q_PROPERTY(QString textoPestanaEmitidas READ textoPestanaEmitidas NOTIFY conteosChanged)   // "Emitidas (12)"
    Q_PROPERTY(QString textoPestanaRecibidas READ textoPestanaRecibidas NOTIFY conteosChanged) // "Recibidas (40)"
    Q_PROPERTY(QString encabezadoContraparte READ encabezadoContraparte NOTIFY direccionChanged) // "Receptor" | "Emisor"
    Q_PROPERTY(QString placeholderBusqueda READ placeholderBusqueda NOTIFY direccionChanged)
    Q_PROPERTY(satcfdi::FacturasListModel* modelo READ modelo CONSTANT)
    Q_PROPERTY(QString texto READ texto WRITE setTexto NOTIFY filtrosChanged)
    Q_PROPERTY(QString tipo READ tipo WRITE setTipo NOTIFY filtrosChanged) // "" | I | E | T | N | P
    Q_PROPERTY(bool hayFiltros READ hayFiltros NOTIFY filtrosChanged)
    // Lo que muestra el area de lista: "sinPerfil" | "cargando" | "error" |
    // "lista" | "sinCoincidencias" | "llenandose" (lista vacia mientras se
    // prepara o indexa; sin estado vacio ni error modal) | "sinFacturas" |
    // "futuro".
    Q_PROPERTY(QString vista READ vista NOTIFY vistaChanged)
    Q_PROPERTY(QString textoVista READ textoVista NOTIFY vistaChanged) // texto del estado vacio/error
    Q_PROPERTY(bool actualizando READ actualizando NOTIFY vistaChanged)
    Q_PROPERTY(satcfdi::VisorPdfCfdiViewModel* visor READ visor CONSTANT)

    // ---- Confirmacion por direccion ----
    Q_PROPERTY(bool confirmacionPendiente READ confirmacionPendiente NOTIFY confirmacionChanged)
    Q_PROPERTY(QString confirmacionTitulo READ confirmacionTitulo NOTIFY confirmacionChanged)
    Q_PROPERTY(QString confirmacionTexto READ confirmacionTexto NOTIFY confirmacionChanged)
    Q_PROPERTY(QString textoConfirmar READ textoConfirmar NOTIFY confirmacionChanged)   // "Descargar de nuevo"
    Q_PROPERTY(QString textoCancelar READ textoCancelar NOTIFY confirmacionChanged)     // "Omitir emitidas"

    // ---- Accesibilidad ----
    // Anuncio para un elemento live region: cambia una vez por cambio de
    // estado del mes (no en cada recarga).
    Q_PROPERTY(QString anuncio READ anuncio NOTIFY anuncioChanged)

public:
    enum class Direccion {
        Emitidas,
        Recibidas,
    };
    Q_ENUM(Direccion)

    static constexpr int kIntervaloBusquedaMs = 250;

    explicit FacturasViewModel(QObject* parent = nullptr);
    ~FacturasViewModel() override;

    // No propietarios; deben vivir mas que este objeto. nullptr = sin datos.
    void setFuente(FuenteFacturasMes* fuente);
    void setPreferencias(PreferenciasFacturas* preferencias);
    void setPrevisualizacion(PrevisualizacionCfdi* previsualizacion);
    void setIntervaloBusqueda(int ms) { m_retardo.setInterval(ms); }

    PerfilesDisponiblesModel* perfiles() const { return m_perfiles; }
    int perfilIndice() const;
    QString perfilId() const;
    QString perfilNombre() const;
    QString perfilRfc() const;
    bool hayPerfiles() const;
    bool eFirmaLista() const;
    QString motivoEFirma() const;

    int anio() const;
    int mes() const;
    QString tituloMes() const;
    bool puedeMesAnterior() const;
    bool puedeMesSiguiente() const;
    bool puedeAnioAnterior() const;
    bool puedeAnioSiguiente() const;
    MesesFacturasModel* meses() const { return m_meses; }
    FranjaMesViewModel* franja() const { return m_franja; }

    Direccion direccion() const { return m_direccion; }
    void setDireccion(Direccion direccion);
    int conteoEmitidas() const;
    int conteoRecibidas() const;
    QString textoPestanaEmitidas() const;
    QString textoPestanaRecibidas() const;
    QString encabezadoContraparte() const;
    QString placeholderBusqueda() const;
    FacturasListModel* modelo() const { return m_modelo; }
    QString texto() const { return m_texto; }
    void setTexto(const QString& texto);
    QString tipo() const { return m_tipo; }
    void setTipo(const QString& tipo);
    bool hayFiltros() const;
    QString vista() const;
    QString textoVista() const;
    bool actualizando() const;
    VisorPdfCfdiViewModel* visor() const { return m_visor; }

    bool confirmacionPendiente() const;
    QString confirmacionTitulo() const;
    QString confirmacionTexto() const;
    QString textoConfirmar() const;
    QString textoCancelar() const;

    QString anuncio() const { return m_anuncio; }

    // Carga perfiles y selecciona perfil y mes iniciales (al mostrar Facturas).
    Q_INVOKABLE void activar();
    // Al salir de la pagina: cierra el visor y deja de recargar con senales.
    Q_INVOKABLE void desactivar();
    Q_INVOKABLE void seleccionarPerfil(int indice);
    // Selecciona el perfil por id (p. ej. desde una notificacion); false si no existe.
    Q_INVOKABLE bool seleccionarPerfilId(const QString& perfilId);
    // Meses futuros no se seleccionan (false).
    Q_INVOKABLE bool seleccionarMes(int anio, int mes);
    // Cmd+Arriba/Abajo: -1 / +1; false si no hay mes.
    Q_INVOKABLE bool moverMes(int delta);
    Q_INVOKABLE bool cambiarAnio(int delta);
    Q_INVOKABLE void limpiarFiltros();
    // Accion de la franja (boton y Cmd+R). No hace nada si no esta habilitada.
    Q_INVOKABLE void ejecutarAccion();
    // Enlace "Ver detalles tecnicos": emite detallesTecnicosSolicitados.
    Q_INVOKABLE void verDetallesTecnicos();
    Q_INVOKABLE void confirmar();
    Q_INVOKABLE void cancelarConfirmacion();
    // Enter sobre una fila: abre el visor PDF (T015.4). false si no existe.
    Q_INVOKABLE bool previsualizar(int fila);
    // Reintentar tras error de lectura.
    Q_INVOKABLE void recargar();

    // T023.5 (Historial y notificaciones, desde AppViewModel): muestra ese
    // perfil y mes. Si los perfiles aun no se cargaron (app arrancada por una
    // notificacion) queda pendiente y sustituye a la seleccion inicial de
    // activar(). Perfil inexistente -> se conserva la seleccion vigente (o la
    // inicial) sin error; mes futuro o invalido -> el perfil con su mes
    // vigente. `alLlegar` = Reintentar: cuando la disponibilidad del mes
    // termina de cargar, si la franja ofrece Reintentar habilitado, la ejecuta
    // una vez (igual que el boton, con sus confirmaciones). Una seleccion
    // manual posterior descarta lo pendiente.
    enum class AlLlegar { Nada, Reintentar };
    void irAMes(const QString& perfilId, int anio, int mes, AlLlegar alLlegar = AlLlegar::Nada);

signals:
    void perfilChanged();
    void mesChanged();
    void direccionChanged();
    void conteosChanged();
    void filtrosChanged();
    void vistaChanged();
    void confirmacionChanged();
    void anuncioChanged();
    // "Registrar e.firma...": abrir Perfiles SAT con ese perfil.
    void registrarEFirmaSolicitado(const QString& perfilId);
    // "Ver detalles tecnicos" (T023.5): `solicitudId` es la solicitud a
    // abrir en Diagnostico (la de la causa principal; si no hay causa, la
    // canonica mas reciente de la pestana activa, en curso o acreditada); ""
    // si el mes no tiene ninguna -> AppViewModel abre la lista Diagnostico.
    void detallesTecnicosSolicitados(const QString& perfilId, int anio, int mes, const QString& solicitudId);

private:
    std::optional<MesCalendario> mesEnCurso() const;
    bool esFuturo(MesCalendario mes) const;
    bool navegable(MesCalendario mes) const;
    void cargarPerfiles();
    void aplicarPerfiles(QList<PerfilFacturas> perfiles);
    void aplicarPerfil(int indice, bool guardar);
    void aplicarMes(std::optional<MesCalendario> mes, bool recargarAnio);
    void recargarMes(bool revalidar);
    void consultarLista(bool cambioDeFiltro);
    void consultarConteosAnio();
    void consultarDisponibilidad(bool revalidar);
    void consultarAnio();
    void actualizarMeses();
    void actualizarFranja();
    void anunciar(const FranjaMesViewModel::Datos& datos);
    void planificar(AccionGuiada accion);
    void decidir(DecisionConfirmacion decision);
    void ejecutarPreplan();
    void terminarConError(const ErrorDescargaGuiada& error);
    void aplicarDestinoPendiente();
    QString solicitudDiagnostico() const;

    PerfilesDisponiblesModel* m_perfiles;
    MesesFacturasModel* m_meses;
    FranjaMesViewModel* m_franja;
    FacturasListModel* m_modelo;
    VisorPdfCfdiViewModel* m_visor;
    QTimer m_retardo;
    Direccion m_direccion = Direccion::Emitidas;
    QString m_texto;
    QString m_tipo;
    QString m_anuncio;
    QPointer<FuenteFacturasMes> m_fuente;
    PreferenciasFacturas* m_preferencias = nullptr;
    // Estado interno: perfil y mes elegidos, resultados, generaciones y
    // confirmacion en curso.
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace satcfdi
