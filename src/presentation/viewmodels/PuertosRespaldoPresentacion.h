#pragma once

// T025.6 (C2): puertos de presentacion del respaldo, la restauracion y los
// datos previos. RespaldoViewModel depende solo de estas interfaces; app_core
// las implementa sobre GestorRespaldo, RestauracionService y
// AppCompositionRoot (hayDatosPrevios, descartarDatosPrevios,
// restauracionAlArrancar). Las pruebas usan fakes.
//
// Reglas comunes:
// - Hilo grafico: todos los metodos se llaman desde el, las senales se emiten
//   en el y los futures se continuan con then(contexto, ...). Nada bloquea.
// - Saneado: ningun tipo de este archivo lleva rutas absolutas, nombres de
//   archivo publicados, RFC, contrasenas ni claves. Los `diagnostico` de los
//   errores de application no se muestran (solo la categoria).
// - Secretos: entran como std::shared_ptr<BufferSecreto> (el view model los
//   crea desde el QString de QML al instante) y la clave de recuperacion sale
//   una sola vez como BufferSecreto (configurarContrasena).

#include "application/respaldo/GestorRespaldo.h"
#include "application/respaldo/RespaldoService.h"
#include "application/respaldo/RestauracionService.h"
#include "domain/common/Resultado.h"
#include "ports/persistence/Exito.h"
#include "ports/respaldo/ContenedorRespaldo.h"
#include "ports/respaldo/DestinoRespaldo.h"
#include "ports/respaldo/FuenteRespaldo.h"
#include "ports/respaldo/Restauracion.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QFuture>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QtGlobal>

#include <functional>
#include <memory>
#include <optional>

namespace satcfdi {

// ---- Respaldo ----------------------------------------------------------------

// Espejo saneado de EstadoRespaldoGestor: nombre de la carpeta (ultimo
// componente, p. ej. "Respaldos"), nunca la ruta absoluta.
struct EstadoRespaldoPresentacion {
    enum class PendienteDiario { Ninguno, FaltaCarpeta, FaltaContrasena };

    // false: la plataforma no tiene almacen de claves o referencias de carpeta
    // (ErrorGestorRespaldo::NoDisponible); la pagina muestra "no disponible".
    bool disponible = true;
    bool cargado = false; // la configuracion ya se leyo
    bool carpetaConfigurada = false;
    QString nombreCarpeta;
    bool contrasenaConfigurada = false;
    bool diarioActivado = false;
    PendienteDiario pendienteDiario = PendienteDiario::Ninguno;
    bool enCurso = false; // un respaldo (manual o diario) esta corriendo
    std::optional<QDateTime> ultimoExitoUtc;
    std::optional<QDateTime> ultimoIntentoUtc;

    bool operator==(const EstadoRespaldoPresentacion&) const = default;
};

// C6: resultado estructurado de un respaldo terminado (manual o diario), y
// el ultimo persistido (ResumenUltimoRespaldo) al abrir la app. El adaptador
// lo llena desde ResultadoRespaldo o desde las claves estables persistidas.
// Errores previos al servicio (falta carpeta o KEK, bookmark que no resuelve)
// llegan como en GestorRespaldo: ErrorDestino/ErrorContenedor con o sin
// categoria.
struct ResultadoRespaldoPresentacion {
    respaldo::ResultadoRespaldo::Estado estado = respaldo::ResultadoRespaldo::Estado::Exito;
    std::optional<respaldo::ErrorDestino::Categoria> errorDestino;
    std::optional<respaldo::ErrorCaptura::Categoria> errorCaptura;
    std::optional<respaldo::ErrorRespaldo::Categoria> errorContenedor;
    QDateTime fechaUtc; // fin del respaldo
    quint64 bytes = 0;  // bytesDatos
    int faltantes = 0;
    int omitidos = 0;
    int depurados = 0;
    int copiasSinRegistro = 0; // D20: copias que hay que borrar a mano
    bool avisoDepuracion = false;
    bool avisoRegistro = false;
    // Q1 (solo memoria): el resumen de este respaldo no se pudo persistir y
    // no se vera al reabrir la app. Lo copia el adaptador (no esta en
    // ResultadoRespaldo).
    bool avisoPersistencia = false;

    bool exito() const noexcept
    {
        return estado == respaldo::ResultadoRespaldo::Estado::Exito
               || estado == respaldo::ResultadoRespaldo::Estado::ExitoConAdvertencias;
    }
    bool operator==(const ResultadoRespaldoPresentacion&) const = default;

    // Copia saneada de un ResultadoRespaldo (sin diagnostico ni nombre publicado).
    static ResultadoRespaldoPresentacion desde(const respaldo::ResultadoRespaldo& r, const QDateTime& fechaUtc)
    {
        ResultadoRespaldoPresentacion p;
        p.estado = r.estado;
        p.errorDestino = r.errorDestino;
        p.errorCaptura = r.errorCaptura;
        p.errorContenedor = r.errorContenedor;
        p.fechaUtc = fechaUtc;
        p.bytes = r.bytesDatos;
        p.faltantes = r.faltantes;
        p.omitidos = r.omitidos;
        p.depurados = r.depurados;
        p.copiasSinRegistro = r.copiasSinRegistro;
        p.avisoDepuracion = r.avisoDepuracion;
        p.avisoRegistro = r.avisoRegistro;
        return p;
    }
};

// Adapta GestorRespaldo (y el progreso de RespaldoService).
class ControlRespaldoPresentacion : public QObject {
    Q_OBJECT

public:
    using ResultadoOperacion = Resultado<Exito, respaldo::ErrorGestorRespaldo>;
    using ResultadoClave = Resultado<std::shared_ptr<BufferSecreto>, respaldo::ErrorGestorRespaldo>;

    using QObject::QObject;
    ~ControlRespaldoPresentacion() override = default;

    virtual EstadoRespaldoPresentacion estado() const = 0;
    // Ultimo resultado conocido: el de esta sesion o el persistido (C6).
    virtual std::optional<ResultadoRespaldoPresentacion> ultimoResultado() const = 0;
    // B3: URL de la carpeta configurada, SOLO para abrir ahi el selector de
    // "Restaurar" (nunca se muestra). Vacia = sin carpeta o no disponible.
    virtual QUrl urlCarpeta() const { return {}; }

    // Ruta local elegida en el FolderDialog (el view model ya convirtio la URL).
    virtual QFuture<ResultadoOperacion> configurarCarpeta(const QString& rutaLocal) = 0;
    // Devuelve la clave de recuperacion formateada UNA vez.
    virtual QFuture<ResultadoClave> configurarContrasena(std::shared_ptr<BufferSecreto> contrasena) = 0;
    virtual QFuture<ResultadoOperacion> cambiarContrasena(std::shared_ptr<BufferSecreto> nueva,
                                                           respaldo::VerificacionCambio verificacion) = 0;
    virtual QFuture<ResultadoOperacion> activarDiario(bool activado) = 0;
    // YaEnCurso si ya corre otro (D18). El resultado tambien se emite con
    // respaldoTerminado.
    virtual QFuture<ResultadoRespaldoPresentacion> respaldarAhora() = 0;

signals:
    void estadoCambiado();
    // Fase y bytes del respaldo en curso (total 0 = indeterminado).
    void progreso(satcfdi::respaldo::RespaldoService::Fase fase, quint64 bytes, quint64 total);
    // Cada respaldo terminado, manual o diario (no se emite con YaEnCurso).
    void respaldoTerminado(const satcfdi::ResultadoRespaldoPresentacion& resultado);
};

// ---- Restauracion -------------------------------------------------------------

// Adapta RestauracionService; el adaptador crea el OrigenRestauracion del
// archivo elegido.
class ControlRestauracionPresentacion : public QObject {
    Q_OBJECT

public:
    using ResultadoPreparacion = Resultado<respaldo::ResumenRestauracion, respaldo::ErrorRestauracion>;

    using QObject::QObject;
    ~ControlRestauracionPresentacion() override = default;

    // Ruta local del `.afbak` elegido en el FileDialog.
    virtual QFuture<ResultadoPreparacion> preparar(const QString& rutaArchivo,
                                                   respaldo::PeticionRestauracion::Credencial credencial,
                                                   std::shared_ptr<BufferSecreto> secreto) = 0;
    // Resuelve de inmediato el future de preparar() con Cancelado.
    virtual void cancelar() = 0;
    // Descarta la preparacion lista (staging y marcador).
    virtual QFuture<void> descartar() = 0;
    // Relanzando / SinRelanzamiento: la app sale enseguida (salir() del servicio).
    virtual QFuture<respaldo::ResultadoSolicitudRestauracion> solicitar() = 0;
    virtual bool enCurso() const = 0;
    virtual bool preparada() const = 0;

signals:
    void progreso(satcfdi::respaldo::RestauracionService::Fase fase, quint64 bytes, quint64 total);
};

// ---- Datos previos y resultado al arrancar -------------------------------------

// Saneado de ResultadoDescarteDatosPrevios (app_core).
struct ResultadoDescartePresentacion {
    enum class Estado {
        Descartados,         // carpetas propias borradas y e.firma reconciliada
        SinDatosPrevios,     // no habia carpetas propias
        Incompleto,          // alguna carpeta propia no se pudo borrar: no se reconcilio
        ErrorReconciliacion, // borradas, pero la limpieza del llavero fallo
    };
    Estado estado = Estado::SinDatosPrevios;
    int carpetasBorradas = 0;
    int carpetasAjenas = 0; // `pre-restauracion-*` no creadas por la app o enlazadas: intactas
};

// Saneado de ResultadoRestauracionArranque (app_core). SinMarcador -> Ninguno;
// Error nunca llega (la app no arranca); Interrumpida es solo de pruebas.
struct ResultadoArranquePresentacion {
    enum class Estado { Ninguno, Aplicada, Descartada, Revertida };
    Estado estado = Estado::Ninguno;
    QByteArray idRespaldo;       // con Aplicada
    QDateTime creadoRespaldoUtc; // con Aplicada
};

class DatosPreviosRestauracionPresentacion {
public:
    virtual ~DatosPreviosRestauracionPresentacion() = default;
    // Hay carpetas `pre-restauracion-*` propias sin descartar (fuera del hilo grafico).
    virtual QFuture<bool> hayDatosPrevios() = 0;
    virtual QFuture<ResultadoDescartePresentacion> descartarDatosPrevios() = 0;
    // Fijo durante la sesion.
    virtual ResultadoArranquePresentacion restauracionAlArrancar() const = 0;
};

// ---- Impresion (C4) -------------------------------------------------------------

// Imprime `texto` con el dialogo nativo, sin archivo intermedio
// (OSIntegration::imprimirTexto). Devuelve false si no se pudo mostrar.
// Vacia = sin impresion (se oculta "Imprimir…").
// El panel es sincrono y app-modal (el bucle de eventos sigue vivo). true =
// enviado; false = cancelado o fallo (mensaje neutro).
using ImpresionTexto = std::function<bool(const QString& titulo, const QString& texto)>;

// H1: copia un secreto (la clave de recuperacion) al portapapeles solo de
// este equipo (sin Handoff), oculto y transitorio para gestores de
// portapapeles, y lo borra a los 90 s (OSIntegration::copiarTextoSecreto).
// Nunca QClipboard. Vacia = sin "Copiar".
using CopiaTextoSecreto = std::function<bool(const QString& texto)>;

} // namespace satcfdi

Q_DECLARE_METATYPE(satcfdi::ResultadoRespaldoPresentacion)
