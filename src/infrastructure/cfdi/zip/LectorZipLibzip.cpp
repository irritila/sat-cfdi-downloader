#include "infrastructure/cfdi/zip/LectorZipLibzip.h"

#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>

#include <zip.h>

#include <sys/stat.h>

#include <algorithm>
#include <limits>
#include <cctype>
#include <memory>
#include <optional>
#include <utility>

namespace satcfdi {

namespace {

// D11: solo causa y conteos. Nunca ruta del ZIP, nombres de entradas ni contenido.
Q_LOGGING_CATEGORY(lcLectorZip, "satcfdi.cfdi.zip")

// Firma del End Of Central Directory y del encabezado local (APPNOTE 4.3).
constexpr char kFirmaEocd[] = "PK\x05\x06";
constexpr char kFirmaLocal[] = "PK\x03\x04";
// El EOCD esta en los ultimos 22 bytes + comentario (max 65535).
constexpr qint64 kVentanaEocd = 22 + 65535;

struct CerrarZip {
    void operator()(zip_t* zip) const noexcept { zip_discard(zip); }
};
struct CerrarEntrada {
    void operator()(zip_file_t* entrada) const noexcept { zip_fclose(entrada); }
};
using ZipUnico = std::unique_ptr<zip_t, CerrarZip>;
using EntradaUnica = std::unique_ptr<zip_file_t, CerrarEntrada>;

using ResultadoLectura = Resultado<ResumenLecturaZip, ErrorPaqueteZip>;

const char* nombreCausa(ErrorPaqueteZip::Causa causa)
{
    switch (causa) {
    case ErrorPaqueteZip::Causa::NoExiste: return "no-existe";
    case ErrorPaqueteZip::Causa::Corrupto: return "corrupto";
    case ErrorPaqueteZip::Causa::Truncado: return "truncado";
    case ErrorPaqueteZip::Causa::DemasiadasEntradas: return "demasiadas-entradas";
    case ErrorPaqueteZip::Causa::ExcedeTotal: return "excede-total";
    }
    return "desconocida";
}

const char* nombreMotivo(MotivoOmision motivo)
{
    switch (motivo) {
    case MotivoOmision::RutaInvalida: return "ruta-invalida";
    case MotivoOmision::Enlace: return "enlace";
    case MotivoOmision::Cifrada: return "cifrada";
    case MotivoOmision::NoXml: return "no-xml";
    case MotivoOmision::ExcedeTamano: return "excede-tamano";
    case MotivoOmision::RazonCompresion: return "razon-compresion";
    case MotivoOmision::ErrorLectura: return "error-lectura";
    }
    return "desconocido";
}

ResultadoLectura fallo(ErrorPaqueteZip::Tipo tipo, ErrorPaqueteZip::Causa causa)
{
    // Info (no Warning): un ZIP ausente o corrupto es una condicion de datos
    // que el indice registra como estado del paquete (T015.2), no un fallo de la app.
    qCInfo(lcLectorZip).noquote() << "paquete ZIP rechazado; causa:" << nombreCausa(causa);
    return ResultadoLectura::fallo(ErrorPaqueteZip{tipo, causa});
}

ResultadoLectura ilegible(ErrorPaqueteZip::Causa causa)
{
    return fallo(ErrorPaqueteZip::Tipo::Ilegible, causa);
}

// Un ZIP que empieza con un encabezado local pero no tiene EOCD en su cola se
// considera truncado (descarga o copia incompleta); cualquier otro fallo de
// apertura es corrupto.
ErrorPaqueteZip::Causa clasificarIlegible(const QString& rutaZip)
{
    QFile archivo(rutaZip);
    if (!archivo.open(QIODevice::ReadOnly))
        return ErrorPaqueteZip::Causa::Corrupto;
    const qint64 tamano = archivo.size();
    const QByteArray inicio = archivo.read(4);
    if (inicio != QByteArray(kFirmaLocal, 4))
        return ErrorPaqueteZip::Causa::Corrupto;
    const qint64 desde = std::max<qint64>(0, tamano - kVentanaEocd);
    if (!archivo.seek(desde))
        return ErrorPaqueteZip::Causa::Corrupto;
    const QByteArray cola = archivo.read(tamano - desde);
    return cola.contains(QByteArray(kFirmaEocd, 4)) ? ErrorPaqueteZip::Causa::Corrupto
                                                    : ErrorPaqueteZip::Causa::Truncado;
}

// Nombre interno en bytes crudos: solo se inspecciona, nunca se usa como ruta.
bool esRutaInvalida(const QByteArray& nombre)
{
    if (nombre.startsWith('/') || nombre.contains('\\'))
        return true;
    // Unidad estilo "C:" (absoluta fuera de Unix).
    if (nombre.size() >= 2 && nombre.at(1) == ':' && std::isalpha(static_cast<unsigned char>(nombre.at(0))))
        return true;
    const QList<QByteArray> componentes = nombre.split('/');
    return std::any_of(componentes.cbegin(), componentes.cend(),
                       [](const QByteArray& c) { return c == ".."; });
}

bool esEnlace(zip_t* zip, zip_uint64_t indice)
{
    zip_uint8_t sistema = 0;
    zip_uint32_t atributos = 0;
    if (zip_file_get_external_attributes(zip, indice, 0, &sistema, &atributos) != 0)
        return false;
    if (sistema != ZIP_OPSYS_UNIX && sistema != ZIP_OPSYS_OS_X)
        return false;
    const auto modo = static_cast<mode_t>((atributos >> 16) & 0xFFFFu);
    return (modo & S_IFMT) == S_IFLNK;
}

bool esXml(const QByteArray& nombre)
{
    return nombre.toLower().endsWith(".xml");
}

// descomprimido / comprimido > razonMax, solo por encima del umbral. Sin
// division ni desbordamiento.
bool razonExcedida(qint64 descomprimido, zip_uint64_t comprimido, const LimitesLectorZip& limites)
{
    if (descomprimido <= limites.umbralRazonBytes)
        return false;
    const auto maxComprimido = static_cast<zip_uint64_t>(std::numeric_limits<qint64>::max() / limites.razonCompresionMax);
    if (comprimido >= maxComprimido)
        return false;
    return descomprimido > static_cast<qint64>(comprimido) * limites.razonCompresionMax;
}

// Resultado de leer una entrada: contenido aceptado, motivo de omision o
// exceso del total descomprimido del ZIP (error del paquete).
struct LecturaEntrada {
    std::optional<QByteArray> contenido;
    std::optional<MotivoOmision> motivo;
    bool excedeTotal = false;
};

// Reglas por entrada (D4), compartidas por recorrer() y leerEntrada().
// `totalDescomprimido` acumula los bytes reales leidos del ZIP.
LecturaEntrada leerUna(zip_t* zip, zip_uint64_t indice, const LimitesLectorZip& limites, qint64& totalDescomprimido)
{
    LecturaEntrada r;
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat_index(zip, indice, ZIP_FL_ENC_RAW, &st) != 0 || !(st.valid & ZIP_STAT_NAME) || st.name == nullptr) {
        r.motivo = MotivoOmision::ErrorLectura;
        return r;
    }
    const QByteArray nombre(st.name);

    if (esRutaInvalida(nombre)) {
        r.motivo = MotivoOmision::RutaInvalida;
        return r;
    }
    if (esEnlace(zip, indice)) {
        r.motivo = MotivoOmision::Enlace;
        return r;
    }
    if ((st.valid & ZIP_STAT_ENCRYPTION_METHOD) && st.encryption_method != ZIP_EM_NONE) {
        r.motivo = MotivoOmision::Cifrada;
        return r;
    }
    if (!esXml(nombre)) {
        r.motivo = MotivoOmision::NoXml;
        return r;
    }

    const bool conTamano = (st.valid & ZIP_STAT_SIZE) != 0;
    const bool conComprimido = (st.valid & ZIP_STAT_COMP_SIZE) != 0;
    const zip_uint64_t comprimido = conComprimido ? st.comp_size : 0;
    // Atajo por tamanos declarados (un encabezado que miente a la baja lo
    // detecta la lectura acotada).
    if (conTamano && st.size > static_cast<zip_uint64_t>(limites.maxBytesEntrada)) {
        r.motivo = MotivoOmision::ExcedeTamano;
        return r;
    }
    if (conTamano && conComprimido && razonExcedida(static_cast<qint64>(st.size), comprimido, limites)) {
        r.motivo = MotivoOmision::RazonCompresion;
        return r;
    }

    EntradaUnica entrada(zip_fopen_index(zip, indice, 0));
    if (!entrada) {
        r.motivo = MotivoOmision::ErrorLectura;
        return r;
    }

    // Lectura acotada: nunca mas de maxBytesEntrada + 1 bytes en memoria.
    QByteArray contenido;
    if (conTamano)
        contenido.reserve(static_cast<qsizetype>(st.size));
    qint64 leidos = 0;
    for (;;) {
        const qint64 permitido = limites.maxBytesEntrada + 1 - leidos;
        const qint64 pedir = std::min(limites.bytesPorTrozo, permitido);
        contenido.resize(static_cast<qsizetype>(leidos + pedir));
        const zip_int64_t n = zip_fread(entrada.get(), contenido.data() + leidos, static_cast<zip_uint64_t>(pedir));
        if (n < 0) {
            r.motivo = MotivoOmision::ErrorLectura;
            return r;
        }
        if (n == 0)
            break;
        leidos += n;
        totalDescomprimido += n;
        if (totalDescomprimido > limites.maxBytesTotal) {
            r.excedeTotal = true;
            return r;
        }
        if (leidos > limites.maxBytesEntrada) {
            r.motivo = MotivoOmision::ExcedeTamano;
            return r;
        }
        if (razonExcedida(leidos, comprimido, limites)) {
            r.motivo = MotivoOmision::RazonCompresion;
            return r;
        }
    }
    contenido.resize(static_cast<qsizetype>(leidos));
    r.contenido = std::move(contenido);
    return r;
}

// Apertura comun: valida existencia, ZIP vacio, apertura y numero de entradas.
struct ZipAbierto {
    ZipUnico zip;
    zip_int64_t entradas = 0;
};

Resultado<ZipAbierto, ErrorPaqueteZip> abrirZip(const QString& rutaZip, const LimitesLectorZip& limites)
{
    using R = Resultado<ZipAbierto, ErrorPaqueteZip>;
    const auto rechazo = [](const ResultadoLectura& f) { return R::fallo(f.error()); };
    const QFileInfo info(rutaZip);
    if (rutaZip.isEmpty() || !info.exists())
        return rechazo(fallo(ErrorPaqueteZip::Tipo::NoExiste, ErrorPaqueteZip::Causa::NoExiste));
    // libzip abre un archivo vacio como ZIP sin entradas; para un paquete del
    // SAT eso solo puede ser una descarga incompleta.
    if (info.isFile() && info.size() == 0)
        return rechazo(ilegible(ErrorPaqueteZip::Causa::Truncado));

    const QByteArray rutaNativa = QFile::encodeName(rutaZip);
    int codigoError = 0;
    ZipUnico zip(zip_open(rutaNativa.constData(), ZIP_RDONLY | ZIP_CHECKCONS, &codigoError));
    if (!zip) {
        if (codigoError == ZIP_ER_NOENT || codigoError == ZIP_ER_OPEN)
            return rechazo(fallo(ErrorPaqueteZip::Tipo::NoExiste, ErrorPaqueteZip::Causa::NoExiste));
        qCDebug(lcLectorZip) << "zip_open fallo; codigo libzip:" << codigoError;
        return rechazo(ilegible(clasificarIlegible(rutaZip)));
    }

    const zip_int64_t numEntradas = zip_get_num_entries(zip.get(), 0);
    if (numEntradas < 0)
        return rechazo(ilegible(ErrorPaqueteZip::Causa::Corrupto));
    if (numEntradas > limites.maxEntradas)
        return rechazo(ilegible(ErrorPaqueteZip::Causa::DemasiadasEntradas));
    return R::exito(ZipAbierto{std::move(zip), numEntradas});
}

} // namespace

LectorZipLibzip::LectorZipLibzip() : m_limites(LimitesLectorZip::productivos()) {}

LectorZipLibzip::LectorZipLibzip(const LimitesLectorZip& limites) : m_limites(limites) {}

Resultado<ResumenLecturaZip, ErrorPaqueteZip> LectorZipLibzip::recorrer(
    const QString& rutaZip, const std::function<bool(const EntradaXmlCfdi&)>& alXml)
{
    auto abierto = abrirZip(rutaZip, m_limites);
    if (!abierto.esExito())
        return ResultadoLectura::fallo(abierto.error());
    zip_t* zip = abierto.valor().zip.get();
    const zip_int64_t numEntradas = abierto.valor().entradas;

    ResumenLecturaZip resumen;
    qint64 totalDescomprimido = 0;

    for (zip_int64_t i = 0; i < numEntradas; ++i) {
        LecturaEntrada lectura = leerUna(zip, static_cast<zip_uint64_t>(i), m_limites, totalDescomprimido);
        if (lectura.excedeTotal)
            return ilegible(ErrorPaqueteZip::Causa::ExcedeTotal);
        if (lectura.motivo) {
            ++resumen.omitidas[*lectura.motivo];
            continue;
        }
        ++resumen.xmlEntregados;
        if (!alXml(EntradaXmlCfdi{static_cast<int>(i), std::move(*lectura.contenido)})) {
            qCDebug(lcLectorZip) << "lectura cancelada; xml entregados:" << resumen.xmlEntregados;
            return ResultadoLectura::exito(std::move(resumen));
        }
    }

    if (!resumen.omitidas.isEmpty()) {
        for (auto it = resumen.omitidas.cbegin(); it != resumen.omitidas.cend(); ++it)
            qCInfo(lcLectorZip).noquote() << "entradas omitidas;" << nombreMotivo(it.key()) << it.value();
    }
    qCDebug(lcLectorZip) << "lectura completa; xml entregados:" << resumen.xmlEntregados;
    return ResultadoLectura::exito(std::move(resumen));
}

Resultado<std::optional<QByteArray>, ErrorPaqueteZip> LectorZipLibzip::leerEntrada(const QString& rutaZip, int indice)
{
    using R = Resultado<std::optional<QByteArray>, ErrorPaqueteZip>;
    auto abierto = abrirZip(rutaZip, m_limites);
    if (!abierto.esExito())
        return R::fallo(abierto.error());
    if (indice < 0 || indice >= abierto.valor().entradas)
        return R::exito(std::nullopt);
    // Una sola entrada: el limite total no puede excederse antes que el de la
    // entrada (maxBytesEntrada < maxBytesTotal), pero se respeta igual.
    qint64 total = 0;
    LecturaEntrada lectura = leerUna(abierto.valor().zip.get(), static_cast<zip_uint64_t>(indice), m_limites, total);
    if (lectura.excedeTotal)
        return R::fallo(ilegible(ErrorPaqueteZip::Causa::ExcedeTotal).error());
    if (lectura.motivo) {
        qCInfo(lcLectorZip).noquote() << "entrada omitida;" << nombreMotivo(*lectura.motivo);
        return R::exito(std::nullopt);
    }
    return R::exito(std::move(lectura.contenido));
}

} // namespace satcfdi
