#pragma once

// T024.2 (D9): fuentes sinteticas de Facturas (T023.4) e Historial (T023.5)
// alimentadas por DatosSinteticos. Todos los futures se resuelven al instante
// en el hilo grafico, salvo ejecutar() sin resultado declarado (queda
// pendiente). Sin hilos, temporizadores ni reloj real: ahoraUtc() es el de los
// datos. Las escrituras de datos (fijarEstadoMes, fijarHistorial) avisan con
// datosCambiados como lo haria la app.

#include "DatosSinteticos.h"

#include "presentation/viewmodels/FuenteFacturasMes.h"
#include "presentation/viewmodels/FuenteHistorial.h"

#include <QHash>
#include <QPromise>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace materiales {

class FuenteFacturasSintetica final : public FuenteFacturasMes {
public:
    explicit FuenteFacturasSintetica(DatosFacturas datos = datosFacturasDeEjemplo());
    ~FuenteFacturasSintetica() override;

    // Perfiles vivos de otra fuente (p. ej. los del guion del video, que se
    // crean y cambian de e.firma durante la grabacion). Vacio: los declarados.
    // Un perfil sin meses declarados tiene todos sus meses Sin descargar.
    std::function<QList<PerfilFacturas>()> proveedorPerfiles;

    const DatosFacturas& datos() const { return m_datos; }
    void setAhora(const QDateTime& ahoraUtc) { m_datos.ahoraUtc = ahoraUtc; }
    void setSolicitudesCreadasAlEjecutar(std::optional<int> n) { m_datos.solicitudesCreadasAlEjecutar = n; }
    const PerfilDeclarado* perfil(const PerfilId& id) const;
    // Cambia el estado (y conteos) de un mes y avisa datosCambiados.
    void fijarMes(const PerfilId& perfil, int anio, int mes, const MesDeclarado& declarado);
    void fijarPreplan(const PreplanDeclarado& preplan) { m_datos.preplan = preplan; }

    // Facturas de un mes con filtro de direccion (nullopt = ambas), en el
    // orden de la lista.
    QList<CfdiMensual> facturas(const PerfilId& perfil, MesCalendario mes, std::optional<TipoDescarga> direccion) const;
    // Estado declarado del mes (SinDescargar si no se declaro).
    MesDeclarado mes(const PerfilId& perfil, MesCalendario mes) const;

    QFuture<ResultadoPerfiles> perfiles() override;
    QFuture<ResultadoCatalogo> consultar(const ConsultaCatalogoMensual& consulta) override;
    QFuture<ResultadoDisponibilidad> disponibilidad(const PerfilId& perfil, MesCalendario mes, bool revalidar) override;
    QFuture<ResultadoPreplan> planificar(const PerfilId& perfil, MesCalendario mes, AccionGuiada accion) override;
    QFuture<ResultadoEjecucion> ejecutar(const PreplanGuiado& preplan, const DecisionesGuiadas& decisiones) override;
    QDateTime ahoraUtc() const override { return m_datos.ahoraUtc; }

    EvaluacionCoberturaMensual evaluacion(const PerfilId& perfil, MesCalendario mes) const;

private:
    DatosFacturas m_datos;
    // Ejecuciones sin resultado declarado: se conservan para no cancelar su future.
    std::vector<std::unique_ptr<QPromise<ResultadoEjecucion>>> m_pendientes;
};

class PreferenciasFacturasMemoria final : public PreferenciasFacturas {
public:
    std::optional<QString> ultimoPerfil() const override { return m_ultimo; }
    void setUltimoPerfil(const QString& perfilId) override { m_ultimo = perfilId; }

private:
    std::optional<QString> m_ultimo;
};

// Historial por lotes declarado por perfil (texto del id -> entradas, mas
// reciente primero). Un perfil sin entradas da un historial vacio.
class HistorialSintetico final : public FuenteHistorial {
public:
    QHash<QString, QList<EntradaHistorial>> entradas;
    // Entradas derivadas de otra fuente (p. ej. las solicitudes del guion);
    // si existe, sustituye a `entradas`.
    std::function<QList<EntradaHistorial>(const PerfilId&)> proveedor;

    void fijarHistorial(const PerfilId& perfil, QList<EntradaHistorial> lista);
    QFuture<ResultadoEntradas> historial(const PerfilId& perfil) override;
};

} // namespace materiales
