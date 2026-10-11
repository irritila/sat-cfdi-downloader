#pragma once

#include <QObject>

// T015.2 (unit): normalizacion del texto de busqueda, regla por paquete
// (calcularIndicePaquete) con FakeCfdiArchiveReader/FakeArchivosZipCfdi y un
// parser falso, e IndexadorCfdi con un catalogo en memoria y el
// PersistenceDispatcher real: hilo propio, coalescencia, una senal por
// solicitud, cancelacion sin escribir y logs sin datos (D11).
class TestIndiceCfdi : public QObject {
    Q_OBJECT

private slots:
    void normalizarTexto();
    void textoBusquedaDeResumen();
    void clavesPersistidas();
    void origenNoDisponible();
    void indexaConConteos();
    void revalidacionRapidaSinSha();
    void mismoShaSoloActualizaHuella();
    void contenidoDistintoReindexa();
    void versionDistintaReindexa();
    void zipIlegibleSinCfdi();
    void cambioDuranteLecturaQuedaPendiente();
    void cancelacionNoEscribe();
    void indexadorUsaHiloPropioYUnaSenalPorSolicitud();
    void indexadorCoalesceYDetener();
    void indexadorDetenidoCancelaPaqueteEnCurso();
    void indexadorAgrupaPorSolicitudSinDependerDelOrden();
    void calculoTerminadoTrasDetenerNoSeEscribe();
    void logsSinDatosFiscales();
    void logsDeErrorDePersistenciaSinMensaje();
};
