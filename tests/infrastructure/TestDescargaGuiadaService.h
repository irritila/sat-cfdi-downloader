#pragma once

#include <QObject>

// T023.3: DescargaGuiadaService con SQLite real (QTemporaryDir), el
// PersistenceDispatcher real, reloj inyectable y gate de e.firma / encolado
// simulados (sin SAT): mes cerrado sin evidencia, reutilizacion y repeticion,
// direccion cubierta, confirmacion por direccion, mes en curso (dia 10/12),
// completar por huecos, e.firma no lista, perfil eliminado y plan
// desactualizado.
class TestDescargaGuiadaService : public QObject {
    Q_OBJECT

private slots:
    void mesCerradoSinEvidenciaCreaDosYLote();
    void reutilizaEnCursoYRepetirNoCreaNada();
    void direccionCubiertaNoCreaNada();
    void confirmacionPorDireccion();
    void mesEnCursoDiaDiezYDoce();
    void completarMesUnaPorHueco();
    void eFirmaNoListaYPerfilEliminado();
    void planDesactualizadoNoCrea();
    void accionNoDisponibleSeRechaza();
    void reservaParcialCreaSoloLoLibre();
};
