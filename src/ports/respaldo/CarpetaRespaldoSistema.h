#pragma once

// Puerto de referencia del sistema a la carpeta de respaldo (T025.4; T025 D1,
// D14; pendientes N2/N3 de T025.3). Convierte la carpeta que el usuario elige
// en una referencia persistible (ruta canonica, bookmark y UUID del volumen) y
// la vuelve a resolver antes de cada respaldo, aunque se haya renombrado o
// movido dentro del mismo volumen.
//
// macOS (infrastructure/os/macos/CarpetaRespaldoMacOS): bookmark normal SIN
// security scope (la app no usa App Sandbox), resolucion sin UI y sin montar
// volumenes. La ruta canonica resuelve enlaces (realpath): `~/Dropbox` ->
// `~/Library/CloudStorage/Dropbox`, `/var` -> `/private/var`.
//
// No valida dueno, escritura ni espacio: eso lo hace DestinoRespaldo::abrir()
// en cada respaldo. Esta referencia solo dice DONDE esta la carpeta.
//
// Errores: categoria y diagnostico ASCII estable; nunca rutas ni nombres.
//
// Hilo: bloquea (sistema de archivos, posible volumen de red). SOLO fuera del
// hilo grafico. Sin estado: reentrante.

#include "domain/common/Resultado.h"

#include <QByteArray>
#include <QString>

#include <utility>

namespace satcfdi::respaldo {

// Lo que se persiste en preferencias.ini (seccion `respaldo`).
struct ReferenciaCarpetaRespaldo {
    QString rutaCanonica; // absoluta, sin enlaces
    QByteArray bookmark;  // opaco; no es secreto
    // UUID del volumen (NSURLVolumeUUIDStringKey, mayusculas). Vacio si el
    // sistema de archivos no lo reporta (algunos volumenes de red).
    QString uuidVolumen;

    bool operator==(const ReferenciaCarpetaRespaldo&) const = default;
};

struct CarpetaRespaldoResuelta {
    QString rutaCanonica; // donde esta ahora la carpeta (sin enlaces)
    QString uuidVolumen;  // como en ReferenciaCarpetaRespaldo
    // El sistema pide regenerar el bookmark (p. ej. tras renombrar o mover):
    // el llamador debe llamar crearReferencia(rutaCanonica) y persistirla.
    bool obsoleto = false;

    bool operator==(const CarpetaRespaldoResuelta&) const = default;
};

struct ErrorCarpetaRespaldo {
    enum class Categoria {
        RutaInvalida,      // vacia o relativa
        NoExiste,          // la ruta elegida no existe
        NoEsCarpeta,       // existe pero no es un directorio
        BookmarkInvalido,  // los datos no son un bookmark (corruptos o vacios)
        NoResuelve,        // la carpeta se borro o su volumen no esta montado
        EnPapelera,        // el bookmark lleva a la carpeta movida a la Papelera
        Interno,
    };

    Categoria categoria = Categoria::Interno;
    QString diagnostico; // ASCII estable, sin rutas

    static ErrorCarpetaRespaldo de(Categoria c, QString diagnostico = {})
    {
        ErrorCarpetaRespaldo e;
        e.categoria = c;
        e.diagnostico = std::move(diagnostico);
        return e;
    }

    bool operator==(const ErrorCarpetaRespaldo&) const = default;
};

class CarpetaRespaldoSistema {
public:
    virtual ~CarpetaRespaldoSistema() = default;

    // `rutaElegida`: absoluta (la del dialogo nativo; puede ser un enlace).
    // Errores: RutaInvalida, NoExiste, NoEsCarpeta, Interno.
    virtual Resultado<ReferenciaCarpetaRespaldo, ErrorCarpetaRespaldo>
    crearReferencia(const QString& rutaElegida) const = 0;

    // Errores: BookmarkInvalido, NoResuelve, NoEsCarpeta, EnPapelera, Interno.
    virtual Resultado<CarpetaRespaldoResuelta, ErrorCarpetaRespaldo>
    resolver(const QByteArray& bookmark) const = 0;
};

} // namespace satcfdi::respaldo
