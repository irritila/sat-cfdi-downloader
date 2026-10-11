#pragma once

#include <QObject>

// T015.1: parsearCfdi con XML sinteticos (RFC genericos, nombres y UUID
// inventados). Sin datos reales.
class TestParserCfdi : public QObject {
    Q_OBJECT

private slots:
    void versionesYTipos_data();
    void versionesYTipos();
    void ingreso40Completo();
    void egreso33ConRelacionadosYRetenciones();
    void pago40ConPagos20();
    void pago33ConPagos10();
    void nominaBasica_data();
    void nominaBasica();
    void version32NoSoportadaConDatosMinimos();
    void complementoDesconocidoQuedaParcial();
    void complementoDeConceptoDesconocidoQuedaParcial();
    void addendaNoAfectaEstado();
    void montoInvalidoQuedaParcial();
    void sinTimbreQuedaParcial();
    void doctypeEsIlegible_data();
    void doctypeEsIlegible();
    void malFormadoEsIlegible_data();
    void malFormadoEsIlegible();
    void codificacion_data();
    void codificacion();
    void profundidad();
    void limiteDeElementos();
    void limiteDeConceptosRecortaYQuedaParcial();
    void raizNoCfdi_data();
    void raizNoCfdi();
    void noEmiteLogs();
};
