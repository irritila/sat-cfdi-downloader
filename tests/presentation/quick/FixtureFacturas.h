#pragma once

// Fixture C++ de las pruebas Qt Quick Test de la pantalla Facturas (T023.4):
// grafo de view models reales (PresentacionViewModels) con una
// FuenteFacturasMes sintetica (futures ya resueltos; planificar/ejecutar
// quedan pendientes y se cuentan). Context property "fixtureFacturas".
// Datos ficticios: XAXX010101000 (e.firma lista) y XEXX010101000 (sin
// e.firma); reloj fijo 15 oct 2026 12:00 Centro (septiembre por omision,
// octubre en curso, noviembre y diciembre futuros).

#include "AppViewModel.h"
#include "ExportacionFacturasViewModel.h"
#include "FacturasViewModel.h"
#include "FranjaMesViewModel.h"
#include "HistorialViewModel.h"

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <vector>

class FixtureFacturas : public QObject {
    Q_OBJECT
    Q_PROPERTY(satcfdi::AppViewModel* app READ app NOTIFY reiniciado)
    Q_PROPERTY(satcfdi::FacturasViewModel* facturas READ facturas NOTIFY reiniciado)
    // T023.5: Historial con una FuenteHistorial sintetica (filas mixtas).
    Q_PROPERTY(satcfdi::HistorialViewModel* historial READ historial NOTIFY reiniciado)
    // T026.3: exportacion (sin servicio hasta habilitarExportacion()).
    Q_PROPERTY(satcfdi::ExportacionFacturasViewModel* exportacion READ exportacion NOTIFY reiniciado)
    // Franja suelta para probar FranjaMes con cualquier combinacion de datos.
    Q_PROPERTY(satcfdi::FranjaMesViewModel* franjaSuelta READ franjaSuelta CONSTANT)

public:
    explicit FixtureFacturas(QObject* parent = nullptr);
    ~FixtureFacturas() override;

    satcfdi::AppViewModel* app() const;
    satcfdi::FacturasViewModel* facturas() const;
    satcfdi::HistorialViewModel* historial() const;
    satcfdi::ExportacionFacturasViewModel* exportacion() const;

    // T026.3: ExportacionFacturasService real (hilo propio) con un ZIP
    // sintetico de 2 facturas, FakeDestinoExportacion en memoria y un lector
    // con compuerta. Cada plan tiene `conteoPlan` facturas (0..2).
    Q_INVOKABLE void habilitarExportacion();
    Q_INVOKABLE void fijarConteoPlan(int conteo);
    // Cerrada: la exportacion se queda en Exportando al leer la primera factura.
    Q_INVOKABLE void cerrarCompuerta();
    Q_INVOKABLE void liberarCompuerta();
    Q_INVOKABLE bool compuertaBloqueada() const;
    // El nombre final aparece justo antes de guardar (EsperandoNombre) una vez.
    Q_INVOKABLE void colisionAlPublicar();
    Q_INVOKABLE int parcialesVivos() const;
    Q_INVOKABLE int archivosEnCarpeta(const QString& carpeta) const;
    satcfdi::FranjaMesViewModel* franjaSuelta() const { return m_franja; }

    // Grafo nuevo (llamar en init(), sin objetos QML vivos del anterior).
    // `conPerfiles` = false: sin perfiles (guia de primer uso).
    Q_INVOKABLE void reiniciar(bool conPerfiles = true);
    // Estado sintetico de un mes de 2026 del primer perfil: "SinDescargar",
    // "Preparando", "Disponibles", "Parcial" (cerrado: faltan recibidas) o
    // "Atencion" (ZIP ilegible de recibidas). Por omision, enero-agosto
    // Disponibles, septiembre SinDescargar y octubre Parcial (en curso).
    Q_INVOKABLE void fijarEstadoMes(int mes, const QString& estado);
    // Emite datosCambiados (recarga sin vaciar la lista).
    Q_INVOKABLE void emitirDatosCambiados();
    // Historial: "mixto" (por omision: Completa, Requiere atencion con
    // reintento y dos direcciones, En preparacion, Sin facturas, Cubierta por
    // otra descarga, Eliminada, eliminacion parcial y Descarga anterior de
    // varios meses), "vacio" o "error". Emite datosCambiados del historial.
    Q_INVOKABLE void fijarHistorial(const QString& caso);
    Q_INVOKABLE int numHistoriales() const;
    Q_INVOKABLE int numPlanificaciones() const;
    Q_INVOKABLE int numConsultas() const;
    Q_INVOKABLE QString perfilSinEFirma() const;
    // Carpeta para capturas (variable SATCFDI_CAPTURAS); "" = no guardar.
    Q_INVOKABLE QString directorioCapturas() const;

    // Franja suelta: claves de FranjaMesViewModel::Datos (estado y accion como
    // enteros de sus enums).
    Q_INVOKABLE void fijarFranja(const QVariantMap& datos);

signals:
    void reiniciado();

private:
    struct Grafo;
    std::unique_ptr<Grafo> m_grafo;
    std::vector<std::unique_ptr<Grafo>> m_retirados;
    satcfdi::FranjaMesViewModel* m_franja;
};
