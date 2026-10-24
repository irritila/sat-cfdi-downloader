#pragma once

// T025.4: configuracion y ejecucion del respaldo (D4, D12, D14, D16, D18, D20).
// Une la configuracion local (preferencias.ini, seccion `respaldo`), el
// almacen de KEK (Keychain), la referencia a la carpeta (bookmark) y el
// RespaldoService de T025.3. Sin archivos ni Keychain propios: todo por
// puertos.
//
// Hilos:
// - QObject del hilo grafico; todos sus metodos publicos se llaman desde ahi.
// - Posee un QThread propio ("satcfdi-respaldo-config") donde corre, en serie,
//   todo lo que bloquea: leer y escribir la configuracion, Keychain (cargar,
//   guardar, borrar, listar), resolver el bookmark y Argon2id. La configuracion
//   autoritativa vive en ese hilo; el hilo grafico tiene un espejo (estado()).
// - RespaldoService::respaldar() y limpiarPartialPropios() se invocan desde el
//   hilo grafico (su contrato), con lo ya preparado en el hilo propio.
//
// D18: respaldarAhora() con otro respaldo en curso (manual o diario) devuelve
// YaEnCurso sin iniciar nada. Durante cada respaldo se mantiene un token de
// OSIntegration::iniciarActividadSistema(Respaldo).
//
// Nunca guarda ni registra la contrasena ni la clave de recuperacion; la clave
// formateada solo se devuelve una vez, en configurarContrasena().

#include "application/persistence/PuertosPersistencia.h"
#include "application/respaldo/RespaldoService.h"
#include "ports/OSIntegration.h"
#include "ports/respaldo/AlmacenKekRespaldo.h"
#include "ports/respaldo/CarpetaRespaldoSistema.h"
#include "ports/respaldo/ConfiguracionRespaldo.h"
#include "ports/respaldo/ContenedorRespaldo.h"
#include "ports/respaldo/DestinoRespaldo.h"

#include <QDateTime>
#include <QFuture>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>
#include <optional>

namespace satcfdi::respaldo {

struct ErrorGestorRespaldo {
    enum class Categoria {
        NoDisponible,           // sin almacen de KEK o sin referencia de carpetas (plataforma)
        FaltaCarpeta,
        FaltaContrasena,
        YaConfigurada,          // configurarContrasena() con una contrasena ya configurada
        Carpeta,                // CarpetaRespaldoSistema (ver diagnostico)
        Almacen,                // AlmacenKekRespaldo (ver diagnostico)
        Kdf,                    // Argon2id o generacion de claves
        VerificacionIncorrecta, // contrasena anterior o clave de recuperacion que no coincide
        Configuracion,          // leer o escribir preferencias.ini
        Detenido,
        EnCurso,                // cambiar la contrasena con un respaldo en curso
    };

    Categoria categoria = Categoria::Configuracion;
    QString diagnostico; // ASCII estable

    static ErrorGestorRespaldo de(Categoria c, QString d = {})
    {
        ErrorGestorRespaldo e;
        e.categoria = c;
        e.diagnostico = std::move(d);
        return e;
    }
};

QString claveEstable(ErrorGestorRespaldo::Categoria c);

// Verificacion para cambiar la contrasena (D16).
struct VerificacionCambio {
    enum class Tipo { ContrasenaAnterior, ClaveRecuperacion };
    Tipo tipo = Tipo::ContrasenaAnterior;
    std::shared_ptr<BufferSecreto> texto;
};

struct EstadoRespaldoGestor {
    enum class PendienteDiario { Ninguno, FaltaCarpeta, FaltaContrasena };

    bool cargado = false; // la configuracion ya se leyo (iniciar())
    // iniciar() termino (incluida la limpieza de `.partial`): solo entonces el
    // diario es elegible (barrera de arranque).
    bool arranqueCompleto = false;
    bool carpetaConfigurada = false;
    QString rutaCarpeta;
    bool contrasenaConfigurada = false;
    bool diarioActivado = false;
    bool enCurso = false;
    std::optional<QDateTime> ultimoExitoUtc;
    std::optional<QDateTime> ultimoIntentoUtc;
    QString ultimoResultado;
    // T025.6 (C6): resumen saneado del ultimo respaldo terminado (persistido;
    // disponible al abrir la app tras un diario).
    std::optional<ResumenUltimoRespaldo> ultimoResumen;
    PendienteDiario pendienteDiario = PendienteDiario::Ninguno;
};

class GestorRespaldo : public QObject {
    Q_OBJECT

public:
    using FabricaDestino =
        std::function<std::shared_ptr<DestinoRespaldo>(const QString& rutaCanonica, const QString& uuidVolumen)>;

    struct Dependencias {
        AlmacenConfiguracionRespaldo* configuracion = nullptr;
        AlmacenKekRespaldo* almacenKek = nullptr;          // nulo = plataforma sin almacen
        CarpetaRespaldoSistema* carpetas = nullptr;        // nulo = plataforma sin bookmarks
        std::shared_ptr<ContenedorRespaldo> contenedor;
        RespaldoService* servicio = nullptr;
        FabricaDestino fabricaDestino;
        RelojUtc reloj = relojSistema();
    };

    explicit GestorRespaldo(Dependencias dependencias, QObject* parent = nullptr);
    ~GestorRespaldo() override; // cerrar()

    GestorRespaldo(const GestorRespaldo&) = delete;
    GestorRespaldo& operator=(const GestorRespaldo&) = delete;

    // Para la actividad de sistema; puede ser nulo (pruebas o antes de
    // iniciarCicloDeVida). No es propiedad del gestor.
    void fijarOS(OSIntegration* os);

    EstadoRespaldoGestor estado() const;
    bool enCurso() const;

    // Arranque: lee la configuracion, reconcilia KEK huerfanas (solo si la
    // configuracion se leyo bien) y, con carpeta configurada, borra los
    // `.partial` propios (T025.3). Devuelve cuantos borro (-1 si no aplico).
    QFuture<int> iniciar();

    QFuture<Resultado<Exito, ErrorGestorRespaldo>> configurarCarpeta(const QString& rutaElegida);

    // Primera vez: sal fija nueva, clave de recuperacion nueva, KEK de
    // contrasena y de recuperacion en el almacen. Devuelve la clave formateada
    // (UNICA vez; nadie mas la guarda).
    QFuture<Resultado<std::shared_ptr<BufferSecreto>, ErrorGestorRespaldo>>
    configurarContrasena(std::shared_ptr<BufferSecreto> contrasena);

    // Con un respaldo en curso devuelve EnCurso sin hacer nada.
    // D16: verifica con la contrasena anterior o la clave de recuperacion;
    // sal nueva, misma clave de recuperacion; guarda la KEK nueva, persiste su
    // referencia y solo despues borra la anterior; reconcilia huerfanas.
    QFuture<Resultado<Exito, ErrorGestorRespaldo>> cambiarContrasena(std::shared_ptr<BufferSecreto> nueva,
                                                                      VerificacionCambio verificacion);

    QFuture<Resultado<Exito, ErrorGestorRespaldo>> activarDiario(bool activado);

    // Respaldo manual o diario. Errores previos al servicio (falta carpeta o
    // KEK, bookmark que no resuelve, Keychain) se devuelven como
    // ResultadoRespaldo con su diagnostico. Registra el resultado en la
    // configuracion.
    QFuture<ResultadoRespaldo> respaldarAhora();

    // ProgramadorRespaldoDiario informa por que el diario no puede correr.
    void fijarPendienteDiario(EstadoRespaldoGestor::PendienteDiario pendiente);

    void cerrar();

    // T025.6 (C6): resumen saneado de `r` terminado en `finUtc`: estado y
    // categorias de error como claves estables, conteos y banderas; sin
    // diagnostico, rutas ni nombre publicado. Pura.
    static ResumenUltimoRespaldo resumenDe(const ResultadoRespaldo& r, const QDateTime& finUtc);

signals:
    void estadoCambiado();
    // T025.6 (C6): al terminar cada respaldo manual o diario que llego a
    // ejecutarse o a validarse (no se emite para YaEnCurso ni Detenido), en el
    // hilo grafico, despues de persistir el resumen y de actualizar estado().
    void respaldoTerminado(const satcfdi::respaldo::ResumenUltimoRespaldo& resumen);
    // T025.6: progreso del respaldo en curso, reexpuesto desde RespaldoService
    // y entregado en el hilo grafico (conexion encolada).
    void progreso(satcfdi::respaldo::RespaldoService::Fase fase, quint64 bytes, quint64 total);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace satcfdi::respaldo
