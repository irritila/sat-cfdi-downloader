#include "app_core/IdInstalacionRespaldo.h"

#include "domain/common/UuidCanonico.h"

#include <QSettings>

namespace satcfdi {

QUuid obtenerOCrearIdInstalacion(const QString& rutaPreferencias)
{
    QSettings ini(rutaPreferencias, QSettings::IniFormat);
    const QString clave = QString::fromLatin1(kClaveIdInstalacion);
    const QString actual = ini.value(clave).toString();
    if (uuid::esCanonico(actual)) {
        return QUuid::fromString(actual);
    }
    const QString nuevo = uuid::generarCanonico();
    ini.setValue(clave, nuevo);
    ini.sync();
    return QUuid::fromString(nuevo);
}

} // namespace satcfdi
