#pragma once

// Fixture C++ para las pruebas Qt Quick Test de PerfilesSatPage y
// EFirmaDialogo (T005.1, DA7). Expone a QML (context property "fixture") los
// view models reales sobre fakes con promesas manuales y comandos para
// resolverlas. Solo pruebas: nunca expone rutas completas ni contrasenas; las
// URLs de archivo apuntan a un directorio centinela inexistente.

#include "../ServiciosAsincronosFake.h"
#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakePerfilesSatService.h"

#include "AppViewModel.h"
#include "EFirmaFormViewModel.h"
#include "PerfilesSatViewModel.h"
#include "PresentacionViewModels.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <vector>

class FixturePerfiles : public QObject {
    Q_OBJECT
    Q_PROPERTY(satcfdi::AppViewModel* app READ app NOTIFY reiniciado)
    Q_PROPERTY(satcfdi::PerfilesSatViewModel* perfiles READ perfiles NOTIFY reiniciado)
    Q_PROPERTY(satcfdi::EFirmaFormViewModel* eFirma READ eFirma NOTIFY reiniciado)

public:
    explicit FixturePerfiles(QObject* parent = nullptr);
    ~FixturePerfiles() override;

    satcfdi::AppViewModel* app() const;
    satcfdi::PerfilesSatViewModel* perfiles() const;
    satcfdi::EFirmaFormViewModel* eFirma() const;

    // Grafo nuevo (llamar en init(), sin objetos QML vivos del anterior).
    Q_INVOKABLE void reiniciar();

    // Resuelve listarNoEliminados `indice` con [{rfc, nombre, activo}] y
    // devuelve los ids generados (en orden).
    Q_INVOKABLE QStringList resolverLista(int indice, const QVariantList& perfiles);
    Q_INVOKABLE int numListas() const;
    // Estado: clave de EstadoCredencial ("Lista", "SinCredencial", "Vencida"...).
    Q_INVOKABLE void resolverResumen(int indice, const QString& estado);
    Q_INVOKABLE void fallarResumen(int indice); // -> EstadoNoDisponible
    Q_INVOKABLE int numResumenes() const;

    Q_INVOKABLE void resolverCreacion(int indice, const QString& rfc, const QString& nombre);
    Q_INVOKABLE void resolverCreacionDuplicado(int indice);
    Q_INVOKABLE int numCreaciones() const;

    // Registro/reemplazo de e.firma.
    Q_INVOKABLE int numRegistros() const;
    Q_INVOKABLE int largoContrasena(int indice) const;
    Q_INVOKABLE bool registroConRutas(int indice) const;
    Q_INVOKABLE bool registroEsReemplazo(int indice) const;
    // categoria vacia = exito; origen: "Certificado" | "Llave" | "Contrasena" | "".
    Q_INVOKABLE void resolverImportacion(int indice, const QString& categoria, const QString& origen);
    Q_INVOKABLE void resolverReemplazo(int indice, const QString& categoria);

    // URL local de un archivo en el directorio centinela.
    Q_INVOKABLE QUrl urlCentinela(const QString& nombre) const;
    Q_INVOKABLE QString directorioCentinela() const;

signals:
    void reiniciado();

private:
    struct Grafo;
    std::unique_ptr<Grafo> m_grafo;
    // Grafos anteriores: viven hasta destruir el fixture para que ningun
    // objeto QML pendiente de borrado apunte a view models destruidos.
    std::vector<std::unique_ptr<Grafo>> m_retirados;
};
