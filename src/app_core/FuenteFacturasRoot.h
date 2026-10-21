#pragma once

#include "presentation/viewmodels/FuenteFacturasMes.h"
#include "application/persistence/PuertosPersistencia.h"

#include <QSettings>
#include <QTimer>

namespace satcfdi {

class CatalogoMensualCfdiService;
class ConsultaPreparacionPerfiles;
class DescargaGuiadaService;

// T023.4: FuenteFacturasMes sobre los servicios reales. Las senales de
// cambio (ejecutor, indice, descarga guiada, eliminaciones) se agrupan en un
// solo datosCambiados(PerfilId nulo = cualquier perfil) con un retardo corto
// para no recargar el mes en rafaga. Hilo grafico.
class FuenteFacturasRoot final : public FuenteFacturasMes {
    Q_OBJECT

public:
    FuenteFacturasRoot(CatalogoMensualCfdiService& catalogo, DescargaGuiadaService& descarga,
                       ConsultaPreparacionPerfiles& preparacion, RelojUtc reloj, QObject* parent = nullptr);

    QFuture<ResultadoPerfiles> perfiles() override;
    QFuture<ResultadoCatalogo> consultar(const ConsultaCatalogoMensual& consulta) override;
    QFuture<ResultadoDisponibilidad> disponibilidad(const PerfilId& perfil, MesCalendario mes, bool revalidar) override;
    QFuture<ResultadoPreplan> planificar(const PerfilId& perfil, MesCalendario mes, AccionGuiada accion) override;
    QFuture<ResultadoEjecucion> ejecutar(const PreplanGuiado& preplan, const DecisionesGuiadas& decisiones) override;
    QDateTime ahoraUtc() const override { return m_reloj(); }

    // Agrupa avisos de cambio (conectado por el composition root).
    void avisarCambio();

private:
    CatalogoMensualCfdiService& m_catalogo;
    DescargaGuiadaService& m_descarga;
    ConsultaPreparacionPerfiles& m_preparacion;
    RelojUtc m_reloj;
    QTimer m_agrupar;
};

// T023.4: ultimo perfil usado, en un .ini del directorio de datos (no en las
// preferencias del usuario de macOS: las pruebas y --data-dir quedan aislados).
class PreferenciasFacturasArchivo final : public PreferenciasFacturas {
public:
    explicit PreferenciasFacturasArchivo(const QString& rutaIni);

    std::optional<QString> ultimoPerfil() const override;
    void setUltimoPerfil(const QString& perfilId) override;

private:
    mutable QSettings m_settings;
};

} // namespace satcfdi
