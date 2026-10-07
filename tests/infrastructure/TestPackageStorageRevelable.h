#pragma once

#include <QObject>

// T009.1 (infrastructure): resoluciones revelables de FilesystemPackageStorage
// sobre QTemporaryDir (D2-D4, D7): presente, ausente, raiz ausente sin
// crearla, '..', absolutas, symlinks y carpeta de la solicitud, sin abrir el ZIP.
class TestPackageStorageRevelable : public QObject {
    Q_OBJECT

private slots:
    void archivoPresenteSinAbrirElZip();
    void archivoAusente();
    void raizAusenteNoSeCrea();
    void rutasInvalidasSeRechazan();
    void symlinksSeRechazan();
    void carpetaDeLaSolicitud();
    void raizRevelable();
};
