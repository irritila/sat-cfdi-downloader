#pragma once

#include <QObject>

// T026.1 (unit): reglas puras de la exportacion de facturas: nombre
// sugerido y saneo del ZIP (D15), asignador de nombres de entrada (D18) y
// contenido exacto de omitidas.txt (D17).
class TestExportacionFacturas : public QObject {
    Q_OBJECT

private slots:
    void nombreSugeridoSinRfc();
    void sanearNombre_data();
    void sanearNombre();
    void sanearNombreRecortaA255BytesSinPartir();
    void asignadorUuidCanonicoSinUuidYColisiones();
    void omitidasByteABytes();
    void omitidasSinLineasNoHayArchivo();
    void omitidasNuncaSerializaIdentificadoresNoValidados();
    void omitidasSinUuidNoSeFusionan();
    void mesesDelAlcance();
};
