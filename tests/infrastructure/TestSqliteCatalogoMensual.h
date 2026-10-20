#pragma once

#include <QObject>

// T023.2: migracion 007 desde la version vigente (006) con datos,
// conjunto vacio confirmado al aplicar una verificacion Terminada, lotes de
// descarga guiada (eliminacion parcial, perfil eliminado y recreado con el
// mismo RFC, revalidacion), evidencia mensual y catalogo mensual
// deduplicado (perfil, mes y direccion, limites 31 dic / 1 ene, fuente
// guiada frente a manual, eliminacion de la fuente, CFDI sin UUID, conteos
// coherentes con la lista) contra SQLite real en QTemporaryDir.
class TestSqliteCatalogoMensual : public QObject {
    Q_OBJECT

private slots:
    // Corte 2
    void migracion007DesdeVersionVigente();
    void conjuntoVacioConfirmadoAlVerificar();
    void lotesEliminacionParcialYPerfilRecreado();
    void insertarLoteRevalida();
    void loteVariosTramosPorDireccion();
    void evidenciaMensual();
    // Corte 3
    void catalogoFiltraPerfilMesYDireccion();
    void catalogoLimitesDeAnio();
    void catalogoDeduplicaPorUuidConFuenteGuiada();
    void catalogoEliminarFuenteEligeLaSiguiente();
    void catalogoVinculoNoCanonicoNoEsGuiado();
    void catalogoSinUuidNoSeDeduplica();
    void catalogoOcultaEliminadosYCuentaIgualQueLaLista();
    void catalogoFiltrosTextoTipoYPaginacion();
};
