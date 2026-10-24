#pragma once

#include "CatalogoMensajes.h"
#include "PuertosRespaldoPresentacion.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>

namespace satcfdi {

// T025.6 (C1-C6): pagina Respaldo (⌘4) y franja global de avisos de
// restauracion. Solo adapta los tres puertos de PuertosRespaldoPresentacion.h
// a propiedades y textos saneados (catalogo::); las reglas viven en
// GestorRespaldo y RestauracionService.
//
// Sin puertos (nullptr) la pagina queda "no disponible" y las acciones no
// hacen nada. Los puertos no son propiedad del view model.
//
// Secretos (C3):
// - Contrasenas y claves tecleadas llegan como QString a los invocables y se
//   copian de inmediato a BufferSecreto; nunca son Q_PROPERTY, ni se guardan
//   como QString, ni se registran. QML limpia sus campos al invocar.
// - La clave de recuperacion (H2) queda PENDIENTE en un BufferSecreto
//   privado desde que configurarContrasena() termina hasta que el usuario
//   confirma "Ya la guardé" (olvidarClaveRecuperacion()). Solo se expone
//   `hayClavePendiente`; el texto lo pide el dialogo con
//   tomarClaveRecuperacion() al abrirse. Asi la clave no se pierde si la
//   pagina Respaldo se descarga (navegacion desde el menu bar o una
//   notificacion) mientras se crea la contrasena o con el dialogo abierto: al
//   volver, la pagina abre otra vez el dialogo. La franja global avisa
//   mientras haya una clave pendiente fuera de Respaldo.
// - "Copiar" usa CopiaTextoSecreto (nunca QClipboard) e "Imprimir…",
//   ImpresionTexto; sin ellos se ocultan.
//
// Hilos: todo en el hilo grafico; continua los futures con then(this, ...).
class RespaldoViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea PresentacionViewModels.")

    // ---- Configuracion (configuracionChanged) ----
    Q_PROPERTY(bool disponible READ disponible NOTIFY configuracionChanged)
    Q_PROPERTY(bool cargando READ cargando NOTIFY configuracionChanged)
    Q_PROPERTY(bool carpetaConfigurada READ carpetaConfigurada NOTIFY configuracionChanged)
    // Solo el nombre de la carpeta (nunca la ruta).
    Q_PROPERTY(QString nombreCarpeta READ nombreCarpeta NOTIFY configuracionChanged)
    Q_PROPERTY(bool contrasenaConfigurada READ contrasenaConfigurada NOTIFY configuracionChanged)
    Q_PROPERTY(bool diarioActivado READ diarioActivado NOTIFY configuracionChanged)
    Q_PROPERTY(QString textoPendienteDiario READ textoPendienteDiario NOTIFY configuracionChanged)
    Q_PROPERTY(QString textoUltimoExito READ textoUltimoExito NOTIFY configuracionChanged)
    // Una operacion de configuracion (carpeta, contrasena, diario) en curso.
    Q_PROPERTY(bool ocupado READ ocupado NOTIFY configuracionChanged)
    Q_PROPERTY(bool respaldando READ respaldando NOTIFY configuracionChanged)
    Q_PROPERTY(bool puedeRespaldar READ puedeRespaldar NOTIFY configuracionChanged)
    Q_PROPERTY(bool puedeImprimir READ puedeImprimir NOTIFY configuracionChanged)
    Q_PROPERTY(bool puedeCopiar READ puedeCopiar NOTIFY configuracionChanged)
    // H2: hay una clave de recuperacion que el usuario aun no confirma haber guardado.
    Q_PROPERTY(bool hayClavePendiente READ hayClavePendiente NOTIFY clavePendienteChanged)
    // El panel de impresion (sincrono, app-modal) esta abierto: el bucle de
    // eventos sigue vivo, asi que QML deshabilita Imprimir, Copiar y cerrar.
    Q_PROPERTY(bool imprimiendo READ imprimiendo NOTIFY configuracionChanged)

    // ---- Progreso del respaldo (progresoRespaldoChanged) ----
    Q_PROPERTY(QString textoFaseRespaldo READ textoFaseRespaldo NOTIFY progresoRespaldoChanged)
    // 0..1; -1 = indeterminado.
    Q_PROPERTY(double fraccionRespaldo READ fraccionRespaldo NOTIFY progresoRespaldoChanged)

    // ---- Ultimo resultado (resultadoChanged) ----
    Q_PROPERTY(bool hayResultado READ hayResultado NOTIFY resultadoChanged)
    Q_PROPERTY(QString varianteResultado READ varianteResultado NOTIFY resultadoChanged)
    Q_PROPERTY(QString tituloResultado READ tituloResultado NOTIFY resultadoChanged)
    Q_PROPERTY(QString descripcionResultado READ descripcionResultado NOTIFY resultadoChanged)
    Q_PROPERTY(QStringList advertenciasResultado READ advertenciasResultado NOTIFY resultadoChanged)
    Q_PROPERTY(QString avisoCopiasSinRegistro READ avisoCopiasSinRegistro NOTIFY resultadoChanged)

    // ---- Mensaje de la ultima operacion de configuracion (mensajeChanged) ----
    Q_PROPERTY(QString mensaje READ mensaje NOTIFY mensajeChanged)
    Q_PROPERTY(QString varianteMensaje READ varianteMensaje NOTIFY mensajeChanged)

    // ---- Dialogos de contrasena (formularioChanged) ----
    Q_PROPERTY(bool procesandoFormulario READ procesandoFormulario NOTIFY formularioChanged)
    Q_PROPERTY(QString errorFormulario READ errorFormulario NOTIFY formularioChanged)

    // ---- Restauracion (restauracionChanged) ----
    Q_PROPERTY(bool restauracionDisponible READ restauracionDisponible NOTIFY restauracionChanged)
    Q_PROPERTY(EstadoRestauracion estadoRestauracion READ estadoRestauracion NOTIFY restauracionChanged)
    // Nombre del `.afbak` elegido (sin carpeta).
    Q_PROPERTY(QString nombreArchivoRestauracion READ nombreArchivoRestauracion NOTIFY restauracionChanged)
    Q_PROPERTY(QString errorRestauracion READ errorRestauracion NOTIFY restauracionChanged)
    // Resultado de confirmar (Relanzando, SinRelanzamiento, FalloTrasDetener).
    Q_PROPERTY(QString varianteSolicitud READ varianteSolicitud NOTIFY restauracionChanged)
    Q_PROPERTY(QString tituloSolicitud READ tituloSolicitud NOTIFY restauracionChanged)
    Q_PROPERTY(QString descripcionSolicitud READ descripcionSolicitud NOTIFY restauracionChanged)
    // Resumen autenticado (con Preparada).
    Q_PROPERTY(QString fechaRespaldoRestauracion READ fechaRespaldoRestauracion NOTIFY restauracionChanged)
    Q_PROPERTY(QString idRespaldoRestauracion READ idRespaldoRestauracion NOTIFY restauracionChanged)
    Q_PROPERTY(QString versionAppRestauracion READ versionAppRestauracion NOTIFY restauracionChanged)
    Q_PROPERTY(QString conteosRestauracion READ conteosRestauracion NOTIFY restauracionChanged)
    Q_PROPERTY(QString avisoMigracionRestauracion READ avisoMigracionRestauracion NOTIFY restauracionChanged)

    // ---- Progreso de la restauracion (progresoRestauracionChanged) ----
    Q_PROPERTY(QString textoFaseRestauracion READ textoFaseRestauracion NOTIFY progresoRestauracionChanged)
    Q_PROPERTY(double fraccionRestauracion READ fraccionRestauracion NOTIFY progresoRestauracionChanged)

    // ---- Avisos globales (avisosChanged) ----
    Q_PROPERTY(bool hayDatosPrevios READ hayDatosPrevios NOTIFY avisosChanged)
    Q_PROPERTY(bool descartandoDatosPrevios READ descartandoDatosPrevios NOTIFY avisosChanged)
    Q_PROPERTY(QString tituloDatosPrevios READ tituloDatosPrevios NOTIFY avisosChanged)
    Q_PROPERTY(QString descripcionDatosPrevios READ descripcionDatosPrevios NOTIFY avisosChanged)
    Q_PROPERTY(QString varianteDescarte READ varianteDescarte NOTIFY avisosChanged)
    Q_PROPERTY(QString tituloDescarte READ tituloDescarte NOTIFY avisosChanged)
    Q_PROPERTY(QString descripcionDescarte READ descripcionDescarte NOTIFY avisosChanged)
    Q_PROPERTY(bool avisoArranqueActivo READ avisoArranqueActivo NOTIFY avisosChanged)
    Q_PROPERTY(QString varianteArranque READ varianteArranque NOTIFY avisosChanged)
    Q_PROPERTY(QString tituloArranque READ tituloArranque NOTIFY avisosChanged)
    Q_PROPERTY(QString descripcionArranque READ descripcionArranque NOTIFY avisosChanged)

public:
    enum class EstadoRestauracion {
        Inactiva,         // sin archivo preparado
        Preparando,       // verificando en segundo plano (se puede cancelar)
        Preparada,        // resumen autenticado listo; se puede confirmar o descartar
        Solicitando,      // "Restaurar y reiniciar" en curso
        Relanzando,       // la app se cierra y se abre de nuevo
        SinRelanzamiento, // la app se cierra; hay que abrirla a mano (C5)
        FalloTrasDetener, // la app se reinicia sin restaurar
    };
    Q_ENUM(EstadoRestauracion)

    // Valores de PeticionRestauracion::Credencial (nombres unicos en QML).
    enum class Credencial { CredencialAutomatica, CredencialContrasena, CredencialClave };
    Q_ENUM(Credencial)

    // Valores de VerificacionCambio::Tipo (nombres unicos en QML).
    enum class Verificacion { VerificarConContrasena, VerificarConClave };
    Q_ENUM(Verificacion)

    explicit RespaldoViewModel(QObject* parent = nullptr);
    ~RespaldoViewModel() override;

    // No propietarios; nullptr = no disponible.
    void setControlRespaldo(ControlRespaldoPresentacion* control);
    void setControlRestauracion(ControlRestauracionPresentacion* control);
    void setDatosPrevios(DatosPreviosRestauracionPresentacion* datos);
    void setImpresion(ImpresionTexto impresion);
    void setCopiaSecreta(CopiaTextoSecreto copia);

    bool disponible() const;
    bool cargando() const;
    bool carpetaConfigurada() const { return m_estado.carpetaConfigurada; }
    QString nombreCarpeta() const { return m_estado.nombreCarpeta; }
    bool contrasenaConfigurada() const { return m_estado.contrasenaConfigurada; }
    bool diarioActivado() const { return m_estado.diarioActivado; }
    QString textoPendienteDiario() const;
    QString textoUltimoExito() const;
    bool ocupado() const { return m_ocupado; }
    bool respaldando() const { return m_estado.enCurso || m_respaldoManual; }
    bool puedeRespaldar() const;
    bool puedeImprimir() const { return static_cast<bool>(m_impresion); }
    bool imprimiendo() const { return m_imprimiendo; }
    bool puedeCopiar() const { return static_cast<bool>(m_copia); }
    bool hayClavePendiente() const { return m_claveRecuperacion != nullptr; }

    // D23: minimo de caracteres de una contrasena nueva (crear o cambiar).
    static constexpr int kMinimoContrasena = 12;

    QString textoFaseRespaldo() const { return m_textoFaseRespaldo; }
    double fraccionRespaldo() const { return m_fraccionRespaldo; }

    bool hayResultado() const { return m_resultado.has_value(); }
    QString varianteResultado() const;
    QString tituloResultado() const;
    QString descripcionResultado() const;
    QStringList advertenciasResultado() const;
    QString avisoCopiasSinRegistro() const;

    QString mensaje() const { return m_mensaje; }
    QString varianteMensaje() const { return m_varianteMensaje; }

    bool procesandoFormulario() const { return m_procesandoFormulario; }
    QString errorFormulario() const { return m_errorFormulario; }

    bool restauracionDisponible() const { return m_restauracion != nullptr; }
    EstadoRestauracion estadoRestauracion() const { return m_estadoRestauracion; }
    QString nombreArchivoRestauracion() const { return m_nombreArchivo; }
    QString errorRestauracion() const { return m_errorRestauracion; }
    QString varianteSolicitud() const { return m_solicitud.variante; }
    QString tituloSolicitud() const { return m_solicitud.titulo; }
    QString descripcionSolicitud() const { return m_solicitud.descripcion; }
    QString fechaRespaldoRestauracion() const;
    QString idRespaldoRestauracion() const;
    QString versionAppRestauracion() const;
    QString conteosRestauracion() const;
    QString avisoMigracionRestauracion() const;

    QString textoFaseRestauracion() const { return m_textoFaseRestauracion; }
    double fraccionRestauracion() const { return m_fraccionRestauracion; }

    bool hayDatosPrevios() const { return m_hayDatosPrevios; }
    bool descartandoDatosPrevios() const { return m_descartando; }
    QString tituloDatosPrevios() const;
    QString descripcionDatosPrevios() const;
    QString varianteDescarte() const { return m_descarte.variante; }
    QString tituloDescarte() const { return m_descarte.titulo; }
    QString descripcionDescarte() const { return m_descarte.descripcion; }
    bool avisoArranqueActivo() const { return !m_arranque.titulo.isEmpty(); }
    QString varianteArranque() const { return m_arranque.variante; }
    QString tituloArranque() const { return m_arranque.titulo; }
    QString descripcionArranque() const { return m_arranque.descripcion; }

    // Al entrar a la pagina: relee la configuracion y consulta datos previos.
    Q_INVOKABLE void activar();

    // URL del FolderDialog (solo archivos locales).
    Q_INVOKABLE void elegirCarpeta(const QUrl& carpeta);
    // Primera contrasena: exito -> claveRecuperacionLista (una vez).
    Q_INVOKABLE void crearContrasena(const QString& contrasena, const QString& confirmacion);
    // D16: con la contrasena anterior o la clave de recuperacion; exito ->
    // contrasenaCambiada (la clave de recuperacion no cambia).
    Q_INVOKABLE void cambiarContrasena(const QString& verificacion, satcfdi::RespaldoViewModel::Verificacion tipo,
                                       const QString& nueva, const QString& confirmacion);
    Q_INVOKABLE void limpiarFormulario();
    Q_INVOKABLE void activarDiario(bool activado);
    Q_INVOKABLE void respaldarAhora();

    // Clave pendiente (H2). Solo la llama DialogoClaveRecuperacion al abrirse.
    // Devuelve el texto mientras la clave esta pendiente (hayClavePendiente) y
    // nunca despues de olvidarClaveRecuperacion() ("Ya la guardé"): entonces
    // devuelve una cadena vacia.
    Q_INVOKABLE QString tomarClaveRecuperacion() const;
    // Imprimir: true = enviado; false = cancelado o fallo (sin mensaje de
    // error). Durante la impresion, copiar e imprimir no hacen nada y olvidar
    // se difiere al terminar. olvidar = "Ya la guardé".
    Q_INVOKABLE bool copiarClaveRecuperacion();
    Q_INVOKABLE bool imprimirClaveRecuperacion();
    Q_INVOKABLE void olvidarClaveRecuperacion();

    Q_INVOKABLE void prepararRestauracion(const QUrl& archivo, const QString& secreto,
                                          satcfdi::RespaldoViewModel::Credencial credencial);
    Q_INVOKABLE void cancelarRestauracion();
    // B3: carpeta inicial del selector de "Restaurar" (no se muestra).
    Q_INVOKABLE QUrl carpetaInicialRestauracion() const;
    Q_INVOKABLE void descartarRestauracion();
    Q_INVOKABLE void confirmarRestauracion();

    Q_INVOKABLE void consultarDatosPrevios();
    Q_INVOKABLE void descartarDatosPrevios();
    Q_INVOKABLE void cerrarAvisoArranque();

signals:
    void configuracionChanged();
    void progresoRespaldoChanged();
    void resultadoChanged();
    void mensajeChanged();
    void formularioChanged();
    void restauracionChanged();
    void progresoRestauracionChanged();
    void avisosChanged();
    // Hay una clave nueva pendiente (sin el texto: tomarClaveRecuperacion()).
    void claveRecuperacionLista();
    void clavePendienteChanged();
    void contrasenaCambiada();

private:
    void leerEstado();
    void fijarMensaje(const QString& variante, const QString& texto);
    void fijarFormulario(bool procesando, const QString& error);
    void fijarOcupado(bool ocupado);
    void fijarResultado(const ResultadoRespaldoPresentacion& resultado);
    void fijarEstadoRestauracion(EstadoRestauracion estado);
    void reiniciarProgresoRespaldo();
    void reiniciarProgresoRestauracion();
    void alProgresoRespaldo(respaldo::RespaldoService::Fase fase, quint64 bytes, quint64 total);
    void alProgresoRestauracion(respaldo::RestauracionService::Fase fase, quint64 bytes, quint64 total);

    QPointer<ControlRespaldoPresentacion> m_respaldo;
    QPointer<ControlRestauracionPresentacion> m_restauracion;
    DatosPreviosRestauracionPresentacion* m_datosPrevios = nullptr;
    ImpresionTexto m_impresion;
    CopiaTextoSecreto m_copia;

    EstadoRespaldoPresentacion m_estado;
    std::optional<ResultadoRespaldoPresentacion> m_resultado;
    bool m_ocupado = false;
    bool m_respaldoManual = false;
    QString m_textoFaseRespaldo;
    double m_fraccionRespaldo = -1;
    QString m_mensaje;
    QString m_varianteMensaje;
    bool m_procesandoFormulario = false;
    QString m_errorFormulario;
    std::unique_ptr<BufferSecreto> m_claveRecuperacion;
    bool m_imprimiendo = false;
    bool m_olvidarAlImprimir = false;

    EstadoRestauracion m_estadoRestauracion = EstadoRestauracion::Inactiva;
    quint64 m_genRestauracion = 0;
    QString m_nombreArchivo;
    QString m_errorRestauracion;
    catalogo::MensajeVisible m_solicitud;
    std::optional<respaldo::ResumenRestauracion> m_resumen;
    QString m_textoFaseRestauracion;
    double m_fraccionRestauracion = -1;

    bool m_hayDatosPrevios = false;
    bool m_descartando = false;
    quint64 m_genDatosPrevios = 0;
    catalogo::MensajeVisible m_descarte;
    catalogo::MensajeVisible m_arranque;
};

} // namespace satcfdi
