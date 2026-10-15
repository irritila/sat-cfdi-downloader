#pragma once

#include <QObject>

// T015.4 (unit): PrevisualizacionCfdiService con catalogo en memoria,
// FakeArchivosZipCfdi, FakeCfdiArchiveReader (leerEntrada por defecto del
// puerto), parser real, renderer y almacen falsos y el PersistenceDispatcher
// real: hilo propio, cache, errores sin PDF, revalidacion del ZIP con
// reindexacion, la ultima peticion gana, detener, guardarComo y logs (D11).
class TestPrevisualizacionCfdi : public QObject {
    Q_OBJECT

private slots:
    void generaFueraDelHiloGraficoYUsaCache();
    void versionNoSoportadaEIlegibleSinPdf();
    void posicionInexistenteEsIlegible();
    void origenFaltanteSinPdfYAvisaAlIndice();
    void paqueteFueraDelIndiceEsOrigenNoDisponible();
    void zipCambiadoReindexaAntesDelPdf();
    void zipCambiadoSinReindexarEsOrigenNoDisponible();
    void mismoContenidoOtraFechaNoEspera();
    void indiceNoVigenteReindexa();
    void otroCfdiEnLaPosicionEsOrigenNoDisponible();
    void ultimaPeticionGana();
    void detenerCancela();
    void guardarComo();
    void nombreSugerido();
    void logsSinDatosFiscales();
};
