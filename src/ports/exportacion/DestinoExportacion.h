#pragma once

// T026.1 (D12, D20): destino atomico de un ZIP de exportacion de facturas.
//
// El adaptador real es infrastructure/storage/DestinoExportacionPosix; las
// pruebas de servicio usan tests/fakes/FakeDestinoExportacion.h.
//
// - El `.partial` es propio, oculto y opaco: nombres::esParcialExportacion().
//   Se crea en la carpeta privada de la app (<datos>/exportacion/parciales,
//   0700) si esta en el mismo volumen que la carpeta destino; si no, en la
//   carpeta destino, registrado localmente ANTES de crearse para poder
//   limpiarlo al arrancar.
// - Ningun error lleva rutas ni nombres; los logs solo fases, conteos y errno.
// - Thread-safe: reservar() y limpiarParcialesPropios() pueden llamarse desde
//   cualquier hilo; cada ArchivoParcialExportacion pertenece a su hilo.

#include "domain/common/Resultado.h"
#include "ports/exportacion/ArchivoParcialExportacion.h"
#include "ports/exportacion/ErrorDestinoExportacion.h"

#include <QString>
#include <QStringView>

#include <memory>

namespace satcfdi::exportacion {

struct ResumenLimpiezaExportacion {
    int borrados = 0;    // `.partial` propios borrados
    int conservados = 0; // con patron propio pero no borrados (enlace, no regular,
                         // carpeta registrada con otra identidad o no disponible)

    bool operator==(const ResumenLimpiezaExportacion&) const = default;
};

class DestinoExportacion {
public:
    virtual ~DestinoExportacion() = default;

    // Reserva un `.partial` nuevo para publicar `nombreFinal` (ya saneado,
    // D15) dentro de `carpetaDestino` (absoluta). Falla con DestinoExistente
    // si el nombre ya existe (tambien un enlace), NombreInvalido,
    // CarpetaNoDisponible / NoEsCarpeta, CarpetaNoPermitida (la carpeta, por
    // identidad st_dev/st_ino, es el directorio de datos de la app o esta
    // dentro de el), SinPermiso, EspacioInsuficiente,
    // Registro o Creacion. Si falla, no queda ningun archivo nuevo.
    virtual Resultado<std::unique_ptr<ArchivoParcialExportacion>, ErrorDestinoExportacion>
    reservar(const QString& carpetaDestino, const QString& nombreFinal) = 0;

    // Al arrancar, antes de cualquier exportacion: borra los `.partial`
    // propios huerfanos (archivos regulares con el patron exacto, sin seguir
    // enlaces) de la carpeta privada y de las carpetas de otro volumen
    // registradas, estas solo si conservan la identidad registrada.
    // Enlaces, archivos ajenos y nombres fuera del patron permanecen.
    virtual Resultado<ResumenLimpiezaExportacion, ErrorDestinoExportacion> limpiarParcialesPropios() = 0;
};

namespace nombres {

inline const QString kPrefijoParcialExportacion = QStringLiteral(".archivo-fiscal-exportacion-");
inline const QString kSufijoParcialExportacion = QStringLiteral(".partial");

// `.archivo-fiscal-exportacion-<16 hex minusculas>.partial` exacto.
inline bool esParcialExportacion(QStringView nombre)
{
    const qsizetype p = kPrefijoParcialExportacion.size();
    const qsizetype s = kSufijoParcialExportacion.size();
    if (nombre.size() != p + 16 + s || !nombre.startsWith(kPrefijoParcialExportacion)
        || !nombre.endsWith(kSufijoParcialExportacion)) {
        return false;
    }
    for (const QChar c : nombre.mid(p, 16)) {
        if (!((c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'))) {
            return false;
        }
    }
    return true;
}

} // namespace nombres

} // namespace satcfdi::exportacion
