#pragma once

#include <QObject>

// RegexLogSanitizer (T003 "LogSanitizer"): catalogo cerrado de marcadores,
// redaccion, preservacion, base64, limites, UTF-16 e idempotencia.
class TestRegexLogSanitizer : public QObject {
    Q_OBJECT

private slots:
    void redacta_data();
    void redacta();
    void preserva_data();
    void preserva();
    void base64Huerfano_data();
    void base64Huerfano();
    void marcadoresPertenecenAlCatalogo();
    void marcadorFalsoNoEvitaRedaccion();
    void marcadorConSufijoNoSePreserva_data();
    void marcadorConSufijoNoSePreserva();
    void idempotente_data();
    void idempotente();
    void limitesDeMensajeYDetalle_data();
    void limitesDeMensajeYDetalle();
    void truncadoNoPartePareSustituto();
    void truncadoNoParteMarcador();
    void entradaMayorAUnMiB();
    void sanitizarCopiaMetadataYArmaPayload();
    void sanitizarRespetaCkLogCodigoSat();
    void sanitizarSinDatosDejaPayloadNulo();
    void sanitizarRedactaTextoLibreYFiltros();
};
