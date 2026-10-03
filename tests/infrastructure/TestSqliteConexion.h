#pragma once

#include <QObject>

// Proveedor de conexiones por hilo, PRAGMAs, WAL, SqliteUnitOfWork
// (BEGIN IMMEDIATE) y cierre controlado sin warnings.
class TestSqliteConexion : public QObject {
    Q_OBJECT

private slots:
    void init();
    void pragmasVerificados();
    void exigeWalEnBaseNoInicializada();
    void rutaInaccesibleEsAlmacenamiento();
    void beginImmediateTomaBloqueoDeEscritura();
    void unidadDeTrabajoValidaEstado();
    void escrituraSinTransaccionFalla();
    void conexionPorHiloSeCierraEnSuHilo();
    void cierreConTransaccionAbiertaRevierte();
    void cierreSinWarnings();
};
