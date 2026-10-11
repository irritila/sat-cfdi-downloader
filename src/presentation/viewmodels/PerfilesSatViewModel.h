#pragma once

#include "PerfilesSatListModel.h"

#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/PerfilConPreparacion.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

class ConsultaPreparacionPerfiles;
class CredencialesSatService;
class PerfilesSatService;

// Pantalla de perfiles SAT (T005.1, contrato de presentacion y DA4).
//
// Lista (`estadoLista`): Cargando, Vacia, Error y ConDatos. cargar() publica
// primero los perfiles persistidos en Verificando y despues verifica cada
// credencial por separado (ConsultaPreparacionPerfiles::verificar); un fallo
// al consultar es EstadoNoDisponible, reintentable por perfil con
// reintentarEstado(id). Un refresco con datos visibles conserva ConDatos.
//
// Formulario (`modo`): Ninguno, Nuevo o Edicion. En Nuevo se capturan RFC y
// nombre; en Edicion el RFC es identidad NO editable (rfcEditable=false) y
// solo cambia el nombre. Errores por campo (errorRfc, errorNombre), errorKey
// estable, errorMessage visible y campoConError ("rfc" | "nombre" | "");
// enfocarCampo(campo) pide a QML mover el foco (RfcDuplicado -> "rfc").
//
// Seleccion (por id, nunca por indice): datos derivados del perfil en edicion
// (preparacion, activo, tieneCredencial, puedeGestionarEFirma). La e.firma se
// gestiona solo con el perfil guardado (!sucio) y activo.
//
// Asincronia (DA4): generacion global de carga, generacion por perfil para
// verificaciones y generacion de formulario/guardado. Una respuesta tardia no
// actualiza una lista reemplazada, un formulario de otro perfil ni restaura
// un estado anterior. Sin rutas, contrasenas ni datos de certificado.
class PerfilesSatViewModel : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Lo crea PresentacionViewModels.")

    Q_PROPERTY(satcfdi::PerfilesSatListModel* perfiles READ perfiles CONSTANT)
    Q_PROPERTY(EstadoLista estadoLista READ estadoLista NOTIFY listaChanged)
    Q_PROPERTY(bool cargando READ cargando NOTIFY listaChanged)
    Q_PROPERTY(QString errorListaMessage READ errorListaMessage NOTIFY listaChanged)

    Q_PROPERTY(Modo modo READ modo NOTIFY formularioChanged)
    Q_PROPERTY(QString perfilId READ perfilId NOTIFY formularioChanged)
    Q_PROPERTY(QString rfc READ rfc WRITE setRfc NOTIFY formularioChanged)
    Q_PROPERTY(QString nombre READ nombre WRITE setNombre NOTIFY formularioChanged)
    Q_PROPERTY(bool rfcEditable READ rfcEditable NOTIFY formularioChanged)
    Q_PROPERTY(QString errorRfc READ errorRfc NOTIFY formularioChanged)
    Q_PROPERTY(QString errorNombre READ errorNombre NOTIFY formularioChanged)
    Q_PROPERTY(QString errorKey READ errorKey NOTIFY formularioChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY formularioChanged)
    Q_PROPERTY(QString campoConError READ campoConError NOTIFY formularioChanged)
    Q_PROPERTY(bool sucio READ sucio NOTIFY formularioChanged)
    Q_PROPERTY(bool guardando READ guardando NOTIFY formularioChanged)
    Q_PROPERTY(bool puedeGuardar READ puedeGuardar NOTIFY formularioChanged)

    Q_PROPERTY(bool seleccionActiva READ seleccionActiva NOTIFY seleccionChanged)
    Q_PROPERTY(QString seleccionPreparacion READ seleccionPreparacion NOTIFY seleccionChanged)
    Q_PROPERTY(QString seleccionEstadoTexto READ seleccionEstadoTexto NOTIFY seleccionChanged)
    Q_PROPERTY(bool seleccionListo READ seleccionListo NOTIFY seleccionChanged)
    // Vigencia de la e.firma del perfil seleccionado ("AAAA-MM-DD") o vacia.
    Q_PROPERTY(QString seleccionVigenteHasta READ seleccionVigenteHasta NOTIFY seleccionChanged)
    // T014.3 D2: dias para vencer de la e.firma seleccionada (0..30) o -1.
    Q_PROPERTY(int seleccionDiasParaVencer READ seleccionDiasParaVencer NOTIFY seleccionChanged)
    Q_PROPERTY(bool tieneCredencial READ tieneCredencial NOTIFY seleccionChanged)
    Q_PROPERTY(bool puedeGestionarEFirma READ puedeGestionarEFirma NOTIFY seleccionChanged)

public:
    enum class EstadoLista {
        Cargando,
        Vacia,
        Error,
        ConDatos,
    };
    Q_ENUM(EstadoLista)

    enum class Modo {
        Ninguno,
        Nuevo,
        Edicion,
    };
    Q_ENUM(Modo)

    // Ambos servicios son obligatorios y deben vivir mas que el view model.
    // Construye su propia ConsultaPreparacionPerfiles. No carga al construir:
    // la carga empieza con cargar() (al abrir la pagina). `reloj` (T014.3):
    // el mismo que usa el aviso de vencimiento, para que el badge coincida.
    PerfilesSatViewModel(PerfilesSatService* perfiles, CredencialesSatService* credenciales,
                         QObject* parent = nullptr, RelojUtc reloj = relojSistema());

    PerfilesSatListModel* perfiles() const { return m_modelo; }
    EstadoLista estadoLista() const;
    bool cargando() const { return m_cargando; }
    QString errorListaMessage() const { return m_errorLista; }

    Modo modo() const { return m_modo; }
    QString perfilId() const { return m_perfilId; }
    QString rfc() const { return m_rfc; }
    QString nombre() const { return m_nombre; }
    bool rfcEditable() const { return m_modo == Modo::Nuevo; }
    QString errorRfc() const { return m_errorRfc; }
    QString errorNombre() const { return m_errorNombre; }
    QString errorKey() const { return m_errorKey; }
    QString errorMessage() const { return m_errorMessage; }
    QString campoConError() const { return m_campoConError; }
    bool sucio() const;
    bool guardando() const { return m_guardando; }
    bool puedeGuardar() const;

    bool seleccionActiva() const;
    QString seleccionPreparacion() const;
    QString seleccionEstadoTexto() const;
    bool seleccionListo() const;
    QString seleccionVigenteHasta() const;
    int seleccionDiasParaVencer() const;
    bool tieneCredencial() const;
    bool puedeGestionarEFirma() const;

    void setRfc(const QString& valor);
    void setNombre(const QString& valor);

    Q_INVOKABLE void cargar();
    // Edita el perfil `id` (UUID texto). false si no esta en la lista.
    Q_INVOKABLE bool seleccionar(const QString& id);
    // T014.4: selecciona `id` en cuanto la lista este cargada (de inmediato si
    // ya lo esta y no hay una carga en curso; si no, al terminar la siguiente
    // carga exitosa). Si el perfil no existe queda sin seleccion y sin error;
    // si la carga falla, la seleccion pendiente se descarta. seleccionar(),
    // nuevo() y cerrarFormulario() descartan la seleccion pendiente.
    void seleccionarAlCargar(const QString& id);
    Q_INVOKABLE void nuevo();
    Q_INVOKABLE void guardar();
    // Nuevo -> Ninguno; Edicion -> restaura el nombre guardado.
    Q_INVOKABLE void descartar();
    // Cierra el formulario (al salir de la pagina). Invalida guardados en curso.
    Q_INVOKABLE void cerrarFormulario();
    Q_INVOKABLE void reintentarEstado(const QString& id);

signals:
    void listaChanged();
    void formularioChanged();
    void seleccionChanged();
    // QML enfoca el campo ("rfc" | "nombre") tras un error de guardado.
    void enfocarCampo(const QString& campo);
    void guardado(const QString& id);

private:
    void verificarPerfil(const PerfilResumen& perfil);
    std::optional<PerfilConPreparacion> seleccion() const;
    void limpiarErrores();
    void abrirEdicion(const PerfilResumen& perfil);
    void aplicarError(const QString& clave, const QString& mensaje, const QString& campo);
    void aplicarSeleccionPendiente(const QString& id);

    QPointer<PerfilesSatService> m_perfilesServicio;
    ConsultaPreparacionPerfiles* m_consulta;
    PerfilesSatListModel* m_modelo;

    bool m_cargando = false;
    bool m_cargado = false;
    QString m_errorLista;

    Modo m_modo = Modo::Ninguno;
    QString m_perfilId;
    QString m_rfc;
    QString m_nombre;
    QString m_nombreGuardado;
    QString m_errorRfc;
    QString m_errorNombre;
    QString m_errorKey;
    QString m_errorMessage;
    QString m_campoConError;
    bool m_guardando = false;
    QString m_seleccionPendiente; // T014.4: se aplica al terminar la carga

    // Generaciones (DA4).
    quint64 m_genCarga = 0;
    quint64 m_genFormulario = 0;
    quint64 m_genGuardar = 0;
    QHash<QString, quint64> m_genPerfil;
};

} // namespace satcfdi
