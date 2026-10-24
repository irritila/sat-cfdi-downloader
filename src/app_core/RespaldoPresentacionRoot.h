#pragma once

// T025.6 (C2, corte B): adaptadores de los puertos de presentacion del
// respaldo sobre application y el composition root. Hilo grafico: todos los
// metodos se llaman desde el y las senales se reemiten en el.

#include "presentation/viewmodels/PuertosRespaldoPresentacion.h"

#include <optional>

namespace satcfdi {

class AppCompositionRoot;

// Clave estable persistida (C6) -> enum; nullopt si no es una clave conocida.
std::optional<respaldo::ResultadoRespaldo::Estado> estadoRespaldoDesdeClave(const QString& clave);
std::optional<respaldo::ErrorDestino::Categoria> errorDestinoDesdeClave(const QString& clave);
std::optional<respaldo::ErrorCaptura::Categoria> errorCapturaDesdeClave(const QString& clave);
std::optional<respaldo::ErrorRespaldo::Categoria> errorContenedorDesdeClave(const QString& clave);
// Resumen persistido -> resultado de presentacion; nullopt si el estado no es
// una clave conocida (una categoria desconocida queda vacia).
std::optional<ResultadoRespaldoPresentacion> resultadoDesdeResumen(const respaldo::ResumenUltimoRespaldo& r);

// Sobre GestorRespaldo (no propietario; debe vivir mas que este objeto).
// `disponible`: la plataforma tiene almacen de KEK y referencias de carpeta.
class ControlRespaldoGestor final : public ControlRespaldoPresentacion {
    Q_OBJECT

public:
    ControlRespaldoGestor(respaldo::GestorRespaldo& gestor, bool disponible, QObject* parent = nullptr);

    EstadoRespaldoPresentacion estado() const override;
    std::optional<ResultadoRespaldoPresentacion> ultimoResultado() const override;
    // Solo para abrir el FileDialog en la carpeta configurada; nunca se muestra.
    QUrl urlCarpeta() const override;
    QFuture<ResultadoOperacion> configurarCarpeta(const QString& rutaLocal) override;
    QFuture<ResultadoClave> configurarContrasena(std::shared_ptr<BufferSecreto> contrasena) override;
    QFuture<ResultadoOperacion> cambiarContrasena(std::shared_ptr<BufferSecreto> nueva,
                                                   respaldo::VerificacionCambio verificacion) override;
    QFuture<ResultadoOperacion> activarDiario(bool activado) override;
    QFuture<ResultadoRespaldoPresentacion> respaldarAhora() override;

private:
    respaldo::GestorRespaldo& m_gestor;
    bool m_disponible;
};

// Sobre RestauracionService (no propietario). preparar() crea un
// OrigenRestauracionArchivo con la ruta elegida.
class ControlRestauracionServicio final : public ControlRestauracionPresentacion {
    Q_OBJECT

public:
    explicit ControlRestauracionServicio(respaldo::RestauracionService& servicio, QObject* parent = nullptr);

    QFuture<ResultadoPreparacion> preparar(const QString& rutaArchivo,
                                           respaldo::PeticionRestauracion::Credencial credencial,
                                           std::shared_ptr<BufferSecreto> secreto) override;
    void cancelar() override;
    QFuture<void> descartar() override;
    QFuture<respaldo::ResultadoSolicitudRestauracion> solicitar() override;
    bool enCurso() const override;
    bool preparada() const override;

private:
    respaldo::RestauracionService& m_servicio;
};

// Sobre AppCompositionRoot (no propietario).
class DatosPreviosRestauracionRoot final : public DatosPreviosRestauracionPresentacion {
public:
    explicit DatosPreviosRestauracionRoot(AppCompositionRoot& root) : m_root(root) {}

    QFuture<bool> hayDatosPrevios() override;
    QFuture<ResultadoDescartePresentacion> descartarDatosPrevios() override;
    ResultadoArranquePresentacion restauracionAlArrancar() const override;

private:
    AppCompositionRoot& m_root;
};

} // namespace satcfdi
