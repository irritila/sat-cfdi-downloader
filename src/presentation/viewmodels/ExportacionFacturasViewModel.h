#pragma once

#include "application/cfdi/ExportacionFacturasService.h"
#include "application/persistence/PuertosPersistencia.h"
#include "domain/cfdi/ExportacionFacturas.h"
#include "HorizonteFacturas.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

namespace satcfdi {

class FacturasViewModel;

// T026.3 (D3, D8-D10, D14-D16, D20, D22; C1-C6): exportacion de facturas
// desde la pantalla Facturas. Solo adapta ExportacionFacturasService (plan,
// destino, progreso y resultado) a propiedades y textos (catalogo::); las
// reglas viven en el servicio y en el dominio.
//
// Estados (C1): Inactivo -> Planeando -> Dialogo -> Exportando ->
// [EsperandoNombre ->] Resultado. Planeando y Dialogo son el dialogo abierto
// con el plan pendiente o listo. Cada planificacion lleva un token propio:
// cambiar modo, rango, direccion o perfil, o cerrar el dialogo, lo invalida y
// la respuesta de un plan obsoleto se descarta (el servicio no da token por
// solicitud). El plan mostrado (conteo D16) es el que se exporta: iniciar() no
// vuelve a planear.
//
// Seleccion (C2, D14): mapa por identidad (paqueteId, posicion) que sobrevive
// filtros, pestanas y recargas de Facturas; cambiar de perfil la vacia. QML
// consulta estaSeleccionada() con los roles PaqueteIdRole y PosicionRole de
// FacturasListModel (enlazando revisionSeleccion para reevaluar); no cambia el
// contrato de FacturasViewModel ni de FacturasListModel.
//
// Rango (C3): horizonte_facturas::validarRango con el mismo horizonte que la
// navegacion de Facturas (mes en curso en hora Centro con el reloj inyectado).
//
// Destino (C4, D15): la carpeta elegida (FolderDialog) se guarda solo en
// privado; QML ve unicamente su nombre (nombreCarpetaDestino). El nombre del
// ZIP es editable y se sanea con sanearNombreZip() al iniciar o publicar.
// - DestinoExistente al reservar (o un error de carpeta/nombre corregible):
//   vuelve a Dialogo con el mismo plan y carpeta y errorDestino; iniciar() de
//   nuevo reinvoca el servicio.
// - destinoExistente() al publicar: EsperandoNombre; publicarConNombre()
//   llama publicarComo() con el MISMO parcial; cancelar() lo descarta.
//
// Textos y datos (C5, C6, D20): todos los textos vienen de catalogo:: con
// estado, conteos y codigos. `nombrePublicado` solo lo muestra la ventana de
// exportacion. Este view model no registra logs ni emite notificaciones del
// sistema.
//
// Hilos: todo en el hilo grafico; los futures del servicio se continuan con
// then(this, ...).
class ExportacionFacturasViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea PresentacionViewModels.")

    // ---- Estado (estadoChanged) ----
    Q_PROPERTY(bool disponible READ disponible NOTIFY estadoChanged)
    Q_PROPERTY(Estado estado READ estado NOTIFY estadoChanged)
    Q_PROPERTY(bool dialogoAbierto READ dialogoAbierto NOTIFY estadoChanged)
    // Planeando o Exportando (sin EsperandoNombre).
    Q_PROPERTY(bool ocupado READ ocupado NOTIFY estadoChanged)
    // "Exportar este mes" / rango / seleccion: hay servicio, perfil y mes navegable.
    Q_PROPERTY(bool puedeExportar READ puedeExportar NOTIFY estadoChanged)

    // ---- Configuracion (configuracionChanged) ----
    Q_PROPERTY(Modo modo READ modo NOTIFY configuracionChanged)
    Q_PROPERTY(int desdeAnio READ desdeAnio NOTIFY configuracionChanged)
    Q_PROPERTY(int desdeMes READ desdeMes NOTIFY configuracionChanged)
    Q_PROPERTY(int hastaAnio READ hastaAnio NOTIFY configuracionChanged)
    Q_PROPERTY(int hastaMes READ hastaMes NOTIFY configuracionChanged)
    Q_PROPERTY(bool rangoValido READ rangoValido NOTIFY configuracionChanged)
    // Meses del horizonte de Facturas para los combos del rango, del mas
    // reciente al mas antiguo: [{anio, mes, texto: "Septiembre 2026"}].
    Q_PROPERTY(QVariantList opcionesMeses READ opcionesMeses NOTIFY estadoChanged)
    Q_PROPERTY(QString errorRango READ errorRango NOTIFY configuracionChanged)
    // Titulo del alcance: "Septiembre 2026", "Agosto 2026 a septiembre 2026"
    // o "Facturas elegidas".
    Q_PROPERTY(QString textoAlcance READ textoAlcance NOTIFY configuracionChanged)
    // D8: oculta e inactiva con facturas elegidas.
    Q_PROPERTY(bool mostrarDireccion READ mostrarDireccion NOTIFY configuracionChanged)
    Q_PROPERTY(Direccion direccion READ direccion WRITE setDireccion NOTIFY configuracionChanged)
    Q_PROPERTY(bool conPdf READ conPdf WRITE setConPdf NOTIFY configuracionChanged)
    Q_PROPERTY(QString aviso READ aviso CONSTANT) // D10, fijo

    // ---- Seleccion (seleccionChanged) ----
    // Casillas por fila visibles en Facturas.
    Q_PROPERTY(bool seleccionando READ seleccionando NOTIFY seleccionChanged)
    Q_PROPERTY(int conteoSeleccionadas READ conteoSeleccionadas NOTIFY seleccionChanged)
    // Cambia con cada cambio de la seleccion (para reevaluar bindings de fila).
    Q_PROPERTY(int revisionSeleccion READ revisionSeleccion NOTIFY seleccionChanged)

    // ---- Plan (planChanged) ----
    Q_PROPERTY(bool hayPlan READ hayPlan NOTIFY planChanged)
    Q_PROPERTY(int conteoPrevisto READ conteoPrevisto NOTIFY planChanged)
    // D16: "Se exportarán N facturas." / "No hay facturas para exportar".
    Q_PROPERTY(QString textoConteo READ textoConteo NOTIFY planChanged)
    Q_PROPERTY(int omitidasPrevistas READ omitidasPrevistas NOTIFY planChanged)
    Q_PROPERTY(int lineasMesConAviso READ lineasMesConAviso NOTIFY planChanged)
    Q_PROPERTY(QStringList avisosPlan READ avisosPlan NOTIFY planChanged)
    Q_PROPERTY(QString errorPlan READ errorPlan NOTIFY planChanged)

    // ---- Destino (destinoChanged) ----
    Q_PROPERTY(QString nombreZip READ nombreZip WRITE setNombreZip NOTIFY destinoChanged)
    Q_PROPERTY(QString nombreSugerido READ nombreSugerido NOTIFY destinoChanged)
    Q_PROPERTY(bool hayCarpetaDestino READ hayCarpetaDestino NOTIFY destinoChanged)
    // Solo el nombre de la carpeta (nunca la ruta).
    Q_PROPERTY(QString nombreCarpetaDestino READ nombreCarpetaDestino NOTIFY destinoChanged)
    Q_PROPERTY(QString errorDestino READ errorDestino NOTIFY destinoChanged)
    // Dialogo con plan de 1 o mas facturas, carpeta y nombre.
    Q_PROPERTY(bool puedeIniciar READ puedeIniciar NOTIFY destinoChanged)

    // ---- Progreso (progresoChanged) ----
    Q_PROPERTY(QString textoFase READ textoFase NOTIFY progresoChanged)
    Q_PROPERTY(int actual READ actual NOTIFY progresoChanged)
    Q_PROPERTY(int total READ total NOTIFY progresoChanged)
    // 0..1; -1 = indeterminado.
    Q_PROPERTY(double fraccion READ fraccion NOTIFY progresoChanged)
    Q_PROPERTY(QString textoProgreso READ textoProgreso NOTIFY progresoChanged) // "3 de 10"
    Q_PROPERTY(int exportadasProgreso READ exportadasProgreso NOTIFY progresoChanged)
    Q_PROPERTY(int sinPdfProgreso READ sinPdfProgreso NOTIFY progresoChanged)
    Q_PROPERTY(int omitidasProgreso READ omitidasProgreso NOTIFY progresoChanged)

    // ---- Resultado (resultadoChanged) ----
    Q_PROPERTY(bool hayResultado READ hayResultado NOTIFY resultadoChanged)
    Q_PROPERTY(QString varianteResultado READ varianteResultado NOTIFY resultadoChanged)
    Q_PROPERTY(QString tituloResultado READ tituloResultado NOTIFY resultadoChanged)
    Q_PROPERTY(QString descripcionResultado READ descripcionResultado NOTIFY resultadoChanged)
    Q_PROPERTY(QStringList detallesResultado READ detallesResultado NOTIFY resultadoChanged)
    Q_PROPERTY(QString advertenciaResultado READ advertenciaResultado NOTIFY resultadoChanged)
    Q_PROPERTY(int exportadas READ exportadas NOTIFY resultadoChanged)
    Q_PROPERTY(int sinPdf READ sinPdf NOTIFY resultadoChanged)
    Q_PROPERTY(int omitidas READ omitidas NOTIFY resultadoChanged)
    // Solo para la ventana de exportacion (D20): nunca a logs ni notificaciones.
    Q_PROPERTY(QString nombrePublicado READ nombrePublicado NOTIFY resultadoChanged)

public:
    enum class Estado { Inactivo, Planeando, Dialogo, Exportando, EsperandoNombre, Resultado };
    Q_ENUM(Estado)
    enum class Modo { Mes, Rango, Seleccion };
    Q_ENUM(Modo)
    enum class Direccion { Emitidas, Recibidas, Ambas };
    Q_ENUM(Direccion)

    explicit ExportacionFacturasViewModel(QObject* parent = nullptr);
    ~ExportacionFacturasViewModel() override;

    // No propietarios; deben vivir mas que este objeto. Sin servicio la
    // exportacion queda "no disponible". `reloj`: el de Facturas (mes en curso
    // y fecha del nombre sugerido de una seleccion).
    void setServicio(ExportacionFacturasService* servicio, RelojUtc reloj = relojSistema());
    void setFacturas(FacturasViewModel* facturas);

    bool disponible() const { return m_servicio != nullptr; }
    Estado estado() const { return m_estado; }
    bool dialogoAbierto() const { return m_estado != Estado::Inactivo; }
    bool ocupado() const { return m_estado == Estado::Planeando || m_estado == Estado::Exportando; }
    bool puedeExportar() const;

    Modo modo() const { return m_modo; }
    int desdeAnio() const { return m_desde.anio(); }
    int desdeMes() const { return m_desde.mes(); }
    int hastaAnio() const { return m_hasta.anio(); }
    int hastaMes() const { return m_hasta.mes(); }
    bool rangoValido() const;
    QVariantList opcionesMeses() const;
    // Indice de (anio, mes) en opcionesMeses; -1 si no esta.
    Q_INVOKABLE int indiceOpcionMes(int anio, int mes) const;
    QString errorRango() const;
    QString textoAlcance() const;
    bool mostrarDireccion() const { return m_modo != Modo::Seleccion; }
    Direccion direccion() const { return m_direccion; }
    void setDireccion(Direccion direccion);
    bool conPdf() const { return m_conPdf; }
    void setConPdf(bool conPdf);
    QString aviso() const;

    bool seleccionando() const { return m_seleccionando; }
    int conteoSeleccionadas() const { return int(m_seleccion.size()); }
    int revisionSeleccion() const { return m_revisionSeleccion; }

    bool hayPlan() const { return m_plan.has_value(); }
    int conteoPrevisto() const { return m_plan ? m_plan->conteoPrevisto() : 0; }
    QString textoConteo() const;
    int omitidasPrevistas() const;
    int lineasMesConAviso() const;
    QStringList avisosPlan() const;
    QString errorPlan() const { return m_errorPlan; }

    QString nombreZip() const { return m_nombreZip; }
    void setNombreZip(const QString& nombre);
    QString nombreSugerido() const { return m_nombreSugerido; }
    bool hayCarpetaDestino() const { return !m_carpetaDestino.isEmpty(); }
    QString nombreCarpetaDestino() const;
    QString errorDestino() const { return m_errorDestino; }
    bool puedeIniciar() const;

    QString textoFase() const { return m_textoFase; }
    int actual() const { return m_actual; }
    int total() const { return m_total; }
    double fraccion() const;
    QString textoProgreso() const;
    int exportadasProgreso() const { return m_exportadasProgreso; }
    int sinPdfProgreso() const { return m_sinPdfProgreso; }
    int omitidasProgreso() const { return m_omitidasProgreso; }

    bool hayResultado() const { return m_resultado.has_value(); }
    QString varianteResultado() const;
    QString tituloResultado() const;
    QString descripcionResultado() const;
    QStringList detallesResultado() const;
    QString advertenciaResultado() const;
    int exportadas() const { return m_resultado ? m_resultado->exportadas : 0; }
    int sinPdf() const { return m_resultado ? m_resultado->sinPdf : 0; }
    int omitidas() const { return m_resultado ? m_resultado->omitidas : 0; }
    QString nombrePublicado() const { return m_resultado ? m_resultado->nombrePublicado : QString(); }

    // Abren el dialogo (desde Inactivo) y planean. Mes: el mes visible de
    // Facturas; Rango: empieza con ese mes en ambos extremos; Seleccion: lo
    // elegido (puede estar vacio: conteo 0, D16). Direccion inicial: la
    // pestana actual (D8).
    Q_INVOKABLE void abrirMesActual();
    Q_INVOKABLE void abrirRango();
    Q_INVOKABLE void abrirSeleccion();
    // Con el dialogo en Planeando o Dialogo: cambia el alcance y vuelve a planear.
    Q_INVOKABLE void cambiarModo(satcfdi::ExportacionFacturasViewModel::Modo modo);
    // Rango inclusivo; invalido (mes fuera de 1..12, futuro, invertido o fuera
    // del horizonte) -> errorRango y sin plan.
    Q_INVOKABLE void fijarRango(int desdeAnio, int desdeMes, int hastaAnio, int hastaMes);
    // Reintento tras un error del plan.
    Q_INVOKABLE void planear();
    // URL del FolderDialog (solo carpetas locales).
    Q_INVOKABLE void elegirDestino(const QUrl& carpeta);
    // Carpeta inicial del FolderDialog (la ultima elegida en la sesion; no se muestra).
    Q_INVOKABLE QUrl carpetaInicial() const;
    Q_INVOKABLE void iniciar();
    Q_INVOKABLE void publicarConNombre();
    // Planeando: cierra. Exportando/EsperandoNombre: cancela el trabajo (el
    // resultado llega como Cancelada; tras el rename no cambia nada).
    // Dialogo/Resultado: cierra.
    Q_INVOKABLE void cancelar();
    // Cierra el dialogo salvo con un trabajo en curso (Exportando/EsperandoNombre).
    Q_INVOKABLE void cerrarDialogo();

    // Modo de seleccion de Facturas (casillas por fila).
    Q_INVOKABLE void iniciarSeleccion();
    // Sale del modo de seleccion y vacia lo elegido.
    Q_INVOKABLE void terminarSeleccion();
    // Fila visible de Facturas por identidad; mes y direccion son los de la
    // pantalla (los de la lista visible). false si la fila ya no esta.
    Q_INVOKABLE bool alternarSeleccion(const QString& paqueteId, int posicion);
    Q_INVOKABLE bool estaSeleccionada(const QString& paqueteId, int posicion) const;
    Q_INVOKABLE void limpiarSeleccion();

signals:
    void estadoChanged();
    void configuracionChanged();
    void seleccionChanged();
    void planChanged();
    void destinoChanged();
    void progresoChanged();
    void resultadoChanged();

private:
    std::optional<MesCalendario> mesEnCurso() const;
    std::optional<PerfilId> perfilFacturas() const;
    std::optional<MesCalendario> mesFacturas() const;
    std::optional<TipoDescarga> tipoDireccion() const;
    std::optional<AlcanceExportacion> alcanceSinValidar() const;
    std::optional<AlcanceExportacion> alcance() const;
    bool puedeConfigurar() const { return m_estado == Estado::Planeando || m_estado == Estado::Dialogo; }
    void abrir(Modo modo);
    void fijarEstado(Estado estado);
    void invalidarPlan();
    void actualizarNombreSugerido();
    void fijarErrorDestino(const QString& texto);
    void reiniciarProgreso();
    void alCambiarPerfil();
    void alProgreso(FaseExportacion fase, int actual, int total, int exportadas, int sinPdf, int omitidas);
    void alDestinoExistente();
    void alTerminar(const ResultadoExportacion& resultado);

    QPointer<ExportacionFacturasService> m_servicio;
    QPointer<FacturasViewModel> m_facturas;
    RelojUtc m_reloj = relojSistema();

    Estado m_estado = Estado::Inactivo;
    Modo m_modo = Modo::Mes;
    MesCalendario m_mesAlcance = *MesCalendario::crear(2000, 1); // modo Mes
    MesCalendario m_desde = *MesCalendario::crear(2000, 1);
    MesCalendario m_hasta = *MesCalendario::crear(2000, 1);
    std::optional<horizonte_facturas::ErrorRango> m_errorRangoForzado; // mes invalido
    Direccion m_direccion = Direccion::Emitidas;
    bool m_conPdf = false;
    PerfilId m_perfilAlcance; // perfil del dialogo abierto

    bool m_seleccionando = false;
    PerfilId m_perfilSeleccion;
    QHash<ReferenciaFactura, FacturaSeleccionada> m_seleccion;
    QList<ReferenciaFactura> m_ordenSeleccion; // orden de eleccion (plan determinista)
    int m_revisionSeleccion = 0;

    quint64 m_tokenPlan = 0;
    std::optional<PlanExportacion> m_plan;
    QString m_errorPlan;

    QString m_nombreZip;
    QString m_nombreSugerido;
    QString m_carpetaDestino; // ruta absoluta: privada, nunca se expone
    QString m_errorDestino;

    quint64 m_tokenTrabajo = 0;
    QString m_textoFase;
    int m_actual = 0;
    int m_total = 0;
    int m_exportadasProgreso = 0;
    int m_sinPdfProgreso = 0;
    int m_omitidasProgreso = 0;
    bool m_preparando = false; // fase Preparando: progreso indeterminado

    std::optional<ResultadoExportacion> m_resultado;
};

} // namespace satcfdi
