#pragma once

#include "SatPruebasComun.h"

#include <QObject>

#include <optional>

class TestC14nFirma : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void vectoresSubconjuntoExcC14nW3c();
    void vectorDocumentoCompleto();
    void vectorEnvelopedExcluyeFirma();
    void vectorTimestampDocSat();
    void erroresDeEntrada();
    void datosCertificadoIssuerYSerialDecimal();
    void firmaAutenticaTimestamp();
    void firmaEnvelopedDeCadaPeticion();
    void declaradaDistintaDeCalculada();

private:
    std::optional<satpruebas::MaterialPrueba> m_material;
};
