#pragma once

#include <QObject>

// T015.1: tipos de dominio CFDI (decimal exacto, UUID, tipo de comprobante).
class TestCfdiDominio : public QObject {
    Q_OBJECT

private slots:
    void importeAceptaGramaticaDecimal_data();
    void importeAceptaGramaticaDecimal();
    void importeConservaTextoOriginal();
    void importeComparaExacto_data();
    void importeComparaExacto();
    void importeNormalizado();
    void uuidCfdiValido_data();
    void uuidCfdiValido();
    void uuidCfdiSeNormalizaEnMayusculas();
    void tipoComprobanteDesdeTexto();
    void ultimosCaracteresDelSello();
};
