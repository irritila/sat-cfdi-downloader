#pragma once

#include <QObject>

// T005 corte A: tipos del puerto SecretStore (move-only, limpieza, refs) y
// CredencialesSatServicePersistido con FakeSecretStore y fakes de persistencia.
class TestCredencialesSat : public QObject {
    Q_OBJECT

private slots:
    // Tipos
    void tiposSensiblesSonMoveOnlySinConversion();
    void bufferSecretoMueveYLimpia();
    void bufferSecretoDesdeTextoUtf8();
    void credencialRefFormatoYValidacion();
    void credencialPreparadaDescartaSinConfirmar();
    void catalogosCerrados();

    // Servicio
    void importarValidoDejaListaYUnaGeneracion();
    void importarErrorDelAlmacenPorCategoria();
    void importarErroresDeValidacionDelFake();
    void importarPerfilInexistenteOInactivo();
    void importarConCredencialExistenteFalla();
    void importarFalloDeCommitDescartaCandidata();
    void reemplazoFallidoEnPreparacionConservaAnterior();
    void reemplazoFallidoEnEscrituraOCommitConservaAnterior();
    void reemplazoValidoDejaUnaGeneracion();
    void reemplazoConLimpiezaFallidaNoRevierteYReconciliaDespues();
    void reemplazarSinCredencialFalla();
    void obtenerEstadoSinCredencialListaYVencida();
    void eliminarBorraFilaYGeneracion();
    void obtenerMaterialFirmaEnHiloDeTrabajo();
    void estadoEnHiloDeTrabajoSincrono();
    void reconciliarConListaIlegibleNoBorra();
    void fakeSoloAceptaCategoriasDelContrato();
    void obtenerMaterialFirmaDesdeHiloGraficoFalla();
    void materialConcurrenteEsperaAReemplazoEnCurso();
    void reemplazoEsperaAMaterialEnCurso();
    void reconciliarTerminaAntesDePrepararEncolado();
    // T005.1
    void origenDeErrorNormalizado();
    void importarPropagaOrigenDelAlmacen();
    void obtenerResumenIncluyeVigenciaSinDescifrar();
};
