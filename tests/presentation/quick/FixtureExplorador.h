#pragma once

// Fixture C++ de las pruebas Qt Quick Test del explorador de CFDI (T015.3):
// grafo de view models reales (PresentacionViewModels) sobre fakes con
// promesas manuales, la consulta del indice (ConsultaCatalogoFake) y un
// Finder espia. Context property "fixtureExplorador". Datos ficticios.

#include "../ConsultaCatalogoFake.h"
#include "../ServiciosAsincronosFake.h"
#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakePerfilesSatService.h"

#include "AccionesFinder.h"
#include "AppViewModel.h"
#include "ExploradorCfdiViewModel.h"
#include "PresentacionViewModels.h"
#include "SolicitudDetailViewModel.h"

#include <QObject>
#include <QVariantMap>

#include <memory>
#include <vector>

class FixtureExplorador : public QObject {
    Q_OBJECT
    Q_PROPERTY(satcfdi::AppViewModel* app READ app NOTIFY reiniciado)
    Q_PROPERTY(satcfdi::SolicitudDetailViewModel* detalle READ detalle NOTIFY reiniciado)
    Q_PROPERTY(satcfdi::ExploradorCfdiViewModel* explorador READ explorador NOTIFY reiniciado)
    Q_PROPERTY(QString solicitudId READ solicitudId CONSTANT)

public:
    explicit FixtureExplorador(QObject* parent = nullptr);
    ~FixtureExplorador() override;

    satcfdi::AppViewModel* app() const;
    satcfdi::SolicitudDetailViewModel* detalle() const;
    satcfdi::ExploradorCfdiViewModel* explorador() const;
    QString solicitudId() const;

    // Grafo nuevo (llamar en init(), sin objetos QML vivos del anterior).
    Q_INVOKABLE void reiniciar();

    // Abre el detalle de la solicitud y lo resuelve con un paquete en cada
    // estado de `estados` ("Descargado", "Disponible", "Error", "Vencido").
    Q_INVOKABLE void abrirDetalle(const QStringList& estados);

    // Resumen `indice`: {total, indexando, pendientes, noDisponibles,
    // ilegibles, cfdiIlegibles} (faltantes = 0/false).
    Q_INVOKABLE void resolverResumen(int indice, const QVariantMap& resumen);
    Q_INVOKABLE void fallarResumen(int indice);
    Q_INVOKABLE int numResumenes() const;
    // Lista `indice` con `n` CFDI sinteticos (posiciones 0..n-1 de PAQ_01).
    Q_INVOKABLE void resolverLista(int indice, int n);
    Q_INVOKABLE void fallarLista(int indice);
    Q_INVOKABLE int numListas() const;
    Q_INVOKABLE QString ultimoTexto() const;
    Q_INVOKABLE QString ultimoTipo() const;
    Q_INVOKABLE void emitirIndiceCambiado();

    Q_INVOKABLE int numPrevisualizaciones() const;
    // "paquete:posicion" de la ultima intencion de previsualizar.
    Q_INVOKABLE QString ultimaPrevisualizacion() const;
    Q_INVOKABLE int numCarpetas() const;

signals:
    void reiniciado();

private:
    struct Grafo;
    std::unique_ptr<Grafo> m_grafo;
    std::vector<std::unique_ptr<Grafo>> m_retirados;
};
