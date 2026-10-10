#pragma once

#include <QObject>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

namespace satcfdi {

// T014.1 D6 (SUG-13): copia un identificador visible (Id de solicitud SAT,
// nombre de paquete) al portapapeles del sistema. Solo texto plano; nunca se
// usa para rutas ni secretos.
class Portapapeles : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Portapapeles(QObject* parent = nullptr);

    // Copia `texto` completo; false si esta vacio o no hay portapapeles.
    Q_INVOKABLE bool copiar(const QString& texto) const;
};

} // namespace satcfdi
