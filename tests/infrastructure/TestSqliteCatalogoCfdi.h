#pragma once

#include <QObject>

// T015.2: migracion 006 (sobre una base T014 = 005 con datos, idempotente) y
// SqliteCfdiCatalogRepository contra SQLite real en QTemporaryDir:
// transaccion por paquete, revalidacion del paquete, filtros/orden de la
// lista, resumen, trigger de eliminacion logica y cascada.
class TestSqliteCatalogoCfdi : public QObject {
    Q_OBJECT

private slots:
    void migracion006SobreBase005ConDatos();
    void listarPaquetesIndexables();
    void reemplazoEnUnaTransaccion();
    void reemplazoRevalidaPaqueteYCoherencia();
    void uuidRepetidoEntrePaquetes();
    void actualizarMetadatosHuella();
    void listaFiltrosYOrden();
    void resumenDeSolicitud();
    void eliminarSolicitudBorraIndice();
    void borradoFisicoEnCascada();
};
