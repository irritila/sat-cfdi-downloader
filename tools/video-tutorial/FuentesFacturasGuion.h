#pragma once

// T022 D6: pantallas Facturas e Historial de la UI vigente (T023) sobre los
// datos del guion, como las conecta la app real (setFuenteFacturas,
// setFuenteHistorial). Sin SQLite, catalogo mensual ni descarga guiada:
// - Facturas: perfiles visibles con su e.firma; catalogo mensual vacio y
//   disponibilidad calculada con la regla real sin evidencia; planificar y
//   ejecutar no hacen nada.
// - Historial: una entrada "Anterior" por solicitud visible del perfil, con
//   "Ver detalles tecnicos" hacia su detalle (Verificar ahora, Ver facturas).
// Solo RFC genericos y nombres del guion (D2).

#include "presentation/viewmodels/FuenteFacturasMes.h"
#include "presentation/viewmodels/FuenteHistorial.h"

#include <QDateTime>
#include <optional>

namespace video {

class PerfilesGuion;
class CredencialesGuion;
class SolicitudesGuion;

class FacturasGuion final : public satcfdi::FuenteFacturasMes {
public:
    FacturasGuion(const PerfilesGuion& perfiles, const CredencialesGuion& credenciales);
    void setAhora(const QDateTime& ahora) { m_ahora = ahora; }

    QFuture<ResultadoPerfiles> perfiles() override;
    QFuture<ResultadoCatalogo> consultar(const satcfdi::ConsultaCatalogoMensual& consulta) override;
    QFuture<ResultadoDisponibilidad> disponibilidad(const satcfdi::PerfilId& perfil, satcfdi::MesCalendario mes,
                                                    bool revalidar) override;
    QFuture<ResultadoPreplan> planificar(const satcfdi::PerfilId& perfil, satcfdi::MesCalendario mes,
                                         satcfdi::AccionGuiada accion) override;
    QFuture<ResultadoEjecucion> ejecutar(const satcfdi::PreplanGuiado& preplan,
                                         const satcfdi::DecisionesGuiadas& decisiones) override;
    QDateTime ahoraUtc() const override { return m_ahora; }

private:
    const PerfilesGuion& m_perfiles;
    const CredencialesGuion& m_credenciales;
    QDateTime m_ahora;
};

class PreferenciasFacturasGuion final : public satcfdi::PreferenciasFacturas {
public:
    std::optional<QString> ultimoPerfil() const override { return m_ultimo; }
    void setUltimoPerfil(const QString& perfilId) override { m_ultimo = perfilId; }

private:
    std::optional<QString> m_ultimo;
};

class HistorialGuion final : public satcfdi::FuenteHistorial {
public:
    HistorialGuion(const PerfilesGuion& perfiles, const SolicitudesGuion& solicitudes);

    QFuture<ResultadoEntradas> historial(const satcfdi::PerfilId& perfil) override;

private:
    const PerfilesGuion& m_perfiles;
    const SolicitudesGuion& m_solicitudes;
};

} // namespace video
