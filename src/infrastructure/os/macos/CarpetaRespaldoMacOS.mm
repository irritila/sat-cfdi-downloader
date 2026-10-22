// CarpetaRespaldoMacOS: bookmark y ruta canonica de la carpeta de respaldo
// (T025.4). Objective-C++ con ARC. Ver contrato en
// ports/respaldo/CarpetaRespaldoSistema.h.

#import <Foundation/Foundation.h>

#include "infrastructure/os/macos/CarpetaRespaldoMacOS.h"

#include <QDir>
#include <QFile>
#include <QStringList>

#include <cerrno>
#include <cstdlib>
#include <optional>
#include <sys/stat.h>

#if !__has_feature(objc_arc)
#error "CarpetaRespaldoMacOS.mm requiere ARC (-fobjc-arc)"
#endif

namespace satcfdi::macos {

namespace {

using respaldo::CarpetaRespaldoResuelta;
using respaldo::ErrorCarpetaRespaldo;
using respaldo::ReferenciaCarpetaRespaldo;
using Categoria = ErrorCarpetaRespaldo::Categoria;

struct Canonica {
    QString ruta;
    bool esCarpeta = false;
};

// realpath(3): resuelve todos los enlaces (tambien el ultimo componente).
// nullopt con errno si no existe o no se puede resolver.
std::optional<Canonica> canonicalizar(const QString& ruta)
{
    const QByteArray nativa = QFile::encodeName(ruta);
    char* resuelta = ::realpath(nativa.constData(), nullptr);
    if (resuelta == nullptr) {
        return std::nullopt;
    }
    Canonica c;
    c.ruta = QFile::decodeName(resuelta);
    std::free(resuelta);
    struct stat st {};
    c.esCarpeta = ::stat(QFile::encodeName(c.ruta).constData(), &st) == 0 && S_ISDIR(st.st_mode);
    return c;
}

QString uuidVolumen(NSURL* url)
{
    id valor = nil;
    if ([url getResourceValue:&valor forKey:NSURLVolumeUUIDStringKey error:nil] && [valor isKindOfClass:[NSString class]]) {
        return QString::fromNSString(static_cast<NSString*>(valor));
    }
    return {};
}

bool enPapelera(NSURL* url, const QString& rutaCanonica)
{
    // Papelera del usuario y de otros volumenes (.Trashes/<uid>).
    const QStringList componentes = rutaCanonica.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (componentes.contains(QStringLiteral(".Trash")) || componentes.contains(QStringLiteral(".Trashes"))) {
        return true;
    }
    NSURLRelationship relacion = NSURLRelationshipOther;
    if ([[NSFileManager defaultManager] getRelationship:&relacion
                                            ofDirectory:NSTrashDirectory
                                               inDomain:NSUserDomainMask
                                            toItemAtURL:url
                                                  error:nil]) {
        return relacion == NSURLRelationshipContains || relacion == NSURLRelationshipSame;
    }
    return false;
}

} // namespace

Resultado<ReferenciaCarpetaRespaldo, ErrorCarpetaRespaldo>
CarpetaRespaldoMacOS::crearReferencia(const QString& rutaElegida) const
{
    using R = Resultado<ReferenciaCarpetaRespaldo, ErrorCarpetaRespaldo>;
    if (rutaElegida.isEmpty() || !QDir::isAbsolutePath(rutaElegida)) {
        return R::fallo(ErrorCarpetaRespaldo::de(Categoria::RutaInvalida, QStringLiteral("ruta.relativa")));
    }
    @autoreleasepool {
        const auto canonica = canonicalizar(rutaElegida);
        if (!canonica) {
            const int e = errno;
            return R::fallo(ErrorCarpetaRespaldo::de(e == ENOENT || e == ENOTDIR ? Categoria::NoExiste : Categoria::Interno,
                                                     QStringLiteral("ruta.realpath")));
        }
        if (!canonica->esCarpeta) {
            return R::fallo(ErrorCarpetaRespaldo::de(Categoria::NoEsCarpeta, QStringLiteral("ruta.no_carpeta")));
        }
        NSURL* url = [NSURL fileURLWithPath:canonica->ruta.toNSString() isDirectory:YES];
        NSError* error = nil;
        NSData* datos = [url bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:&error];
        if (datos == nil) {
            return R::fallo(ErrorCarpetaRespaldo::de(Categoria::Interno, QStringLiteral("bookmark.crear")));
        }
        ReferenciaCarpetaRespaldo r;
        r.rutaCanonica = canonica->ruta;
        r.bookmark = QByteArray::fromNSData(datos);
        r.uuidVolumen = uuidVolumen(url);
        return R::exito(std::move(r));
    }
}

Resultado<CarpetaRespaldoResuelta, ErrorCarpetaRespaldo> CarpetaRespaldoMacOS::resolver(const QByteArray& bookmark) const
{
    using R = Resultado<CarpetaRespaldoResuelta, ErrorCarpetaRespaldo>;
    if (bookmark.isEmpty()) {
        return R::fallo(ErrorCarpetaRespaldo::de(Categoria::BookmarkInvalido, QStringLiteral("bookmark.vacio")));
    }
    @autoreleasepool {
        BOOL obsoleto = NO;
        NSError* error = nil;
        NSURL* url = [NSURL URLByResolvingBookmarkData:bookmark.toNSData()
                                               options:NSURLBookmarkResolutionWithoutUI
                                                       | NSURLBookmarkResolutionWithoutMounting
                                         relativeToURL:nil
                                   bookmarkDataIsStale:&obsoleto
                                                 error:&error];
        if (url == nil) {
            // Datos que no son un bookmark -> NSFileReadCorruptFileError;
            // carpeta borrada o volumen ausente -> NSFileNoSuchFileError u otro.
            const bool corrupto = error != nil && [error.domain isEqualToString:NSCocoaErrorDomain]
                && error.code == NSFileReadCorruptFileError;
            return R::fallo(ErrorCarpetaRespaldo::de(corrupto ? Categoria::BookmarkInvalido : Categoria::NoResuelve,
                                                     corrupto ? QStringLiteral("bookmark.corrupto")
                                                              : QStringLiteral("bookmark.resolver")));
        }
        const auto canonica = canonicalizar(QString::fromNSString(url.path));
        if (!canonica) {
            return R::fallo(ErrorCarpetaRespaldo::de(Categoria::NoResuelve, QStringLiteral("ruta.realpath")));
        }
        if (!canonica->esCarpeta) {
            return R::fallo(ErrorCarpetaRespaldo::de(Categoria::NoEsCarpeta, QStringLiteral("ruta.no_carpeta")));
        }
        NSURL* urlCanonica = [NSURL fileURLWithPath:canonica->ruta.toNSString() isDirectory:YES];
        if (enPapelera(urlCanonica, canonica->ruta)) {
            return R::fallo(ErrorCarpetaRespaldo::de(Categoria::EnPapelera, QStringLiteral("ruta.papelera")));
        }
        CarpetaRespaldoResuelta r;
        r.rutaCanonica = canonica->ruta;
        r.uuidVolumen = uuidVolumen(urlCanonica);
        r.obsoleto = obsoleto == YES;
        return R::exito(std::move(r));
    }
}

} // namespace satcfdi::macos
