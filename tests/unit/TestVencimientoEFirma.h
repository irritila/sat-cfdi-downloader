#pragma once

#include <QObject>

// T014.3 (unit): reglas de vencimiento, AvisoVencimientoEFirma con reloj y
// programador falsos y dedupe persistido falso, textos de la notificacion,
// diasParaVencer de la lista de perfiles y ConsultaPrimerUsoPersistida.
class TestVencimientoEFirma : public QObject {
    Q_OBJECT

private slots:
    void reglasPuras();
    void avisa30UnaVezYNoRepiteTrasReinicio();
    void avisa7YRevisionDiaria();
    void reemplazoVuelveAAvisar();
    void vencidaONoListaNoAvisa();
    void permisoDenegadoNoCambiaNada();
    void textosSinRfcCompleto();
    void diasParaVencerEnLaLista();
    void primerUso();
};
