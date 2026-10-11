#include "application/cfdi/CalculoIndicePaquete.h"

#include "ports/cfdi/ArchivosZipCfdi.h"
#include "ports/cfdi/CfdiArchiveReader.h"

#include <utility>

namespace satcfdi {

namespace {

QString causaArchivo(ArchivoZipLocal::Estado estado)
{
    switch (estado) {
    case ArchivoZipLocal::Estado::NoExiste:
        return QStringLiteral("no-existe");
    case ArchivoZipLocal::Estado::Invalido:
        return QStringLiteral("ruta-invalida");
    case ArchivoZipLocal::Estado::ErrorLectura:
    case ArchivoZipLocal::Estado::Existe:
        break;
    }
    return QStringLiteral("error-lectura");
}

QString causaZip(ErrorPaqueteZip::Causa causa)
{
    switch (causa) {
    case ErrorPaqueteZip::Causa::NoExiste:
        return QStringLiteral("no-existe");
    case ErrorPaqueteZip::Causa::Corrupto:
        return QStringLiteral("corrupto");
    case ErrorPaqueteZip::Causa::Truncado:
        return QStringLiteral("truncado");
    case ErrorPaqueteZip::Causa::DemasiadasEntradas:
        return QStringLiteral("demasiadas-entradas");
    case ErrorPaqueteZip::Causa::ExcedeTotal:
        return QStringLiteral("excede-total");
    }
    return QStringLiteral("corrupto");
}

CalculoPaquete reemplazar(IndicePaquete indice)
{
    CalculoPaquete c;
    c.tipo = CalculoPaquete::Tipo::Reemplazar;
    c.indice = std::move(indice);
    return c;
}

CalculoPaquete cancelado()
{
    CalculoPaquete c;
    c.tipo = CalculoPaquete::Tipo::Cancelado;
    return c;
}

IndicePaquete base(const PaqueteIndexable& p, int version, const QDateTime& ahora)
{
    IndicePaquete i;
    i.paqueteId = p.paqueteId;
    i.versionParser = version;
    i.actualizadoEn = ahora;
    return i;
}

} // namespace

CalculoPaquete calcularIndicePaquete(const PaqueteIndexable& paquete, EntornoCalculoIndice& entorno,
                                     const std::function<bool()>& continuar, const QDateTime& ahoraUtc)
{
    const auto sigue = [&continuar] { return !continuar || continuar(); };
    if (!sigue()) {
        return cancelado();
    }
    const bool versionVigente = paquete.versionParser == entorno.versionParser;

    // 1. Origen.
    const ArchivoZipLocal archivo = entorno.archivos.consultar(paquete.rutaLocal);
    if (archivo.estado != ArchivoZipLocal::Estado::Existe) {
        if (paquete.estado == EstadoIndicePaquete::OrigenNoDisponible && versionVigente) {
            return CalculoPaquete{};
        }
        IndicePaquete i = base(paquete, entorno.versionParser, ahoraUtc);
        i.estado = EstadoIndicePaquete::OrigenNoDisponible;
        i.causa = causaArchivo(archivo.estado);
        return reemplazar(std::move(i));
    }

    // 2. Revalidacion de un indice vigente.
    const bool vigente = versionVigente && paquete.huella
                         && (paquete.estado == EstadoIndicePaquete::Indexado
                             || paquete.estado == EstadoIndicePaquete::Ilegible);
    std::optional<QString> sha;
    if (vigente) {
        if (paquete.huella->mismosMetadatos(archivo.tamano, archivo.modificadoMs)) {
            return CalculoPaquete{};
        }
        sha = entorno.archivos.sha256(archivo.rutaAbsoluta, sigue);
        if (!sha) {
            if (!sigue()) {
                return cancelado();
            }
            IndicePaquete i = base(paquete, entorno.versionParser, ahoraUtc);
            i.estado = EstadoIndicePaquete::OrigenNoDisponible;
            i.causa = QStringLiteral("error-lectura");
            return reemplazar(std::move(i));
        }
        if (*sha == paquete.huella->sha256) {
            CalculoPaquete c;
            c.tipo = CalculoPaquete::Tipo::ActualizarHuella;
            c.huella = HuellaZip{archivo.tamano, archivo.modificadoMs, *sha};
            return c;
        }
    }

    // 3. Indexar.
    if (!sha) {
        sha = entorno.archivos.sha256(archivo.rutaAbsoluta, sigue);
        if (!sha) {
            if (!sigue()) {
                return cancelado();
            }
            IndicePaquete i = base(paquete, entorno.versionParser, ahoraUtc);
            i.estado = EstadoIndicePaquete::OrigenNoDisponible;
            i.causa = QStringLiteral("error-lectura");
            return reemplazar(std::move(i));
        }
    }
    const HuellaZip huella{archivo.tamano, archivo.modificadoMs, *sha};

    IndicePaquete indice = base(paquete, entorno.versionParser, ahoraUtc);
    bool cancelada = false;
    auto lectura = entorno.lector.recorrer(archivo.rutaAbsoluta, [&](const EntradaXmlCfdi& entrada) {
        if (!sigue()) {
            cancelada = true;
            return false;
        }
        ++indice.conteos.xmlLeidos;
        ResultadoParseoCfdi resultado = entorno.parser(QByteArrayView(entrada.contenido));
        switch (resultado.estado()) {
        case EstadoParseo::NoEsCfdi:
            ++indice.conteos.xmlNoCfdi;
            return true; // se cuenta, no se indexa
        case EstadoParseo::Ilegible:
            ++indice.conteos.cfdiIlegibles;
            break;
        case EstadoParseo::VersionNoSoportada:
            ++indice.conteos.cfdiNoSoportados;
            break;
        case EstadoParseo::Completo:
        case EstadoParseo::Parcial:
            break;
        }
        CfdiIndexado cfdi;
        cfdi.posicion = entrada.indice;
        cfdi.textoBusqueda = textoBusquedaCfdi(resultado.resumen);
        cfdi.resumen = std::move(resultado.resumen);
        indice.cfdis.append(std::move(cfdi));
        return true;
    });
    if (cancelada || !sigue()) {
        return cancelado(); // nada se escribe: el paquete queda como estaba
    }
    if (!lectura.esExito()) {
        IndicePaquete i = base(paquete, entorno.versionParser, ahoraUtc);
        const ErrorPaqueteZip& error = lectura.error();
        i.causa = causaZip(error.causa);
        if (error.tipo == ErrorPaqueteZip::Tipo::NoExiste) {
            i.estado = EstadoIndicePaquete::OrigenNoDisponible;
        } else {
            i.estado = EstadoIndicePaquete::Ilegible;
            i.huella = huella;
        }
        return reemplazar(std::move(i)); // sin resultados parciales (D4)
    }

    // El ZIP cambio durante la lectura (no deberia: T008 escribe por rename).
    const ArchivoZipLocal despues = entorno.archivos.consultar(paquete.rutaLocal);
    if (despues.estado != ArchivoZipLocal::Estado::Existe || despues.tamano != archivo.tamano
        || despues.modificadoMs != archivo.modificadoMs) {
        IndicePaquete i = base(paquete, entorno.versionParser, ahoraUtc);
        i.estado = EstadoIndicePaquete::Pendiente;
        return reemplazar(std::move(i));
    }

    indice.conteos.omitidas = lectura.valor().omitidas;
    indice.estado = EstadoIndicePaquete::Indexado;
    indice.huella = huella;
    return reemplazar(std::move(indice));
}

} // namespace satcfdi
