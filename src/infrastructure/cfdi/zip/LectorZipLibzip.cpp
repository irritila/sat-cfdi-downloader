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
    qCWarning(lcLectorZip).noquote() << "paquete ZIP rechazado; causa:" << nombreCausa(causa);
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

enum class LecturaEntrada { Aceptada, Omitida, ExcedeTotal };

} // namespace

LectorZipLibzip::LectorZipLibzip() : m_limites(LimitesLectorZip::productivos()) {}

LectorZipLibzip::LectorZipLibzip(const LimitesLectorZip& limites) : m_limites(limites) {}

Resultado<ResumenLecturaZip, ErrorPaqueteZip> LectorZipLibzip::recorrer(
    const QString& rutaZip, const std::function<bool(const EntradaXmlCfdi&)>& alXml)
{
    const QFileInfo info(rutaZip);
    if (rutaZip.isEmpty() || !info.exists())
        return fallo(ErrorPaqueteZip::Tipo::NoExiste, ErrorPaqueteZip::Causa::NoExiste);
    // libzip abre un archivo vacio como ZIP sin entradas; para un paquete del
    // SAT eso solo puede ser una descarga incompleta.
    if (info.isFile() && info.size() == 0)
        return ilegible(ErrorPaqueteZip::Causa::Truncado);

    const QByteArray rutaNativa = QFile::encodeName(rutaZip);
    int codigoError = 0;
    ZipUnico zip(zip_open(rutaNativa.constData(), ZIP_RDONLY | ZIP_CHECKCONS, &codigoError));
    if (!zip) {
        if (codigoError == ZIP_ER_NOENT || codigoError == ZIP_ER_OPEN)
            return fallo(ErrorPaqueteZip::Tipo::NoExiste, ErrorPaqueteZip::Causa::NoExiste);
        qCDebug(lcLectorZip) << "zip_open fallo; codigo libzip:" << codigoError;
        return ilegible(clasificarIlegible(rutaZip));
    }

    const zip_int64_t numEntradas = zip_get_num_entries(zip.get(), 0);
    if (numEntradas < 0)
        return ilegible(ErrorPaqueteZip::Causa::Corrupto);
    if (numEntradas > m_limites.maxEntradas)
        return ilegible(ErrorPaqueteZip::Causa::DemasiadasEntradas);

    ResumenLecturaZip resumen;
    qint64 totalDescomprimido = 0;
    auto omitir = [&resumen](MotivoOmision motivo) { ++resumen.omitidas[motivo]; };

    for (zip_int64_t i = 0; i < numEntradas; ++i) {
        const auto indice = static_cast<zip_uint64_t>(i);

        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(zip.get(), indice, ZIP_FL_ENC_RAW, &st) != 0 || !(st.valid & ZIP_STAT_NAME)
            || st.name == nullptr) {
            omitir(MotivoOmision::ErrorLectura);
            continue;
        }
        const QByteArray nombre(st.name);

        if (esRutaInvalida(nombre)) {
            omitir(MotivoOmision::RutaInvalida);
            continue;
        }
        if (esEnlace(zip.get(), indice)) {
            omitir(MotivoOmision::Enlace);
            continue;
        }
        if ((st.valid & ZIP_STAT_ENCRYPTION_METHOD) && st.encryption_method != ZIP_EM_NONE) {
            omitir(MotivoOmision::Cifrada);
            continue;
        }
        if (!esXml(nombre)) {
            omitir(MotivoOmision::NoXml);
            continue;
        }

        const bool conTamano = (st.valid & ZIP_STAT_SIZE) != 0;
        const bool conComprimido = (st.valid & ZIP_STAT_COMP_SIZE) != 0;
        const zip_uint64_t comprimido = conComprimido ? st.comp_size : 0;
        // Atajo por tamanos declarados (un encabezado que miente a la baja lo
        // detecta la lectura acotada).
        if (conTamano && st.size > static_cast<zip_uint64_t>(m_limites.maxBytesEntrada)) {
            omitir(MotivoOmision::ExcedeTamano);
            continue;
        }
        if (conTamano && conComprimido && razonExcedida(static_cast<qint64>(st.size), comprimido, m_limites)) {
            omitir(MotivoOmision::RazonCompresion);
            continue;
        }

        EntradaUnica entrada(zip_fopen_index(zip.get(), indice, 0));
        if (!entrada) {
            omitir(MotivoOmision::ErrorLectura);
            continue;
        }

        // Lectura acotada: nunca mas de maxBytesEntrada + 1 bytes en memoria.
        QByteArray contenido;
        if (conTamano)
            contenido.reserve(static_cast<qsizetype>(st.size));
        qint64 leidos = 0;
        std::optional<MotivoOmision> motivo;
        bool excedeTotal = false;
        for (;;) {
            const qint64 permitido = m_limites.maxBytesEntrada + 1 - leidos;
            const qint64 pedir = std::min(m_limites.bytesPorTrozo, permitido);
            contenido.resize(static_cast<qsizetype>(leidos + pedir));
            const zip_int64_t n = zip_fread(entrada.get(), contenido.data() + leidos,
                                            static_cast<zip_uint64_t>(pedir));
            if (n < 0) {
                motivo = MotivoOmision::ErrorLectura;
                break;
            }
            if (n == 0)
                break;
            leidos += n;
            totalDescomprimido += n;
            if (totalDescomprimido > m_limites.maxBytesTotal) {
                excedeTotal = true;
                break;
            }
            if (leidos > m_limites.maxBytesEntrada) {
                motivo = MotivoOmision::ExcedeTamano;
                break;
            }
            if (razonExcedida(leidos, comprimido, m_limites)) {
                motivo = MotivoOmision::RazonCompresion;
                break;
            }
        }
        entrada.reset();

        if (excedeTotal)
            return ilegible(ErrorPaqueteZip::Causa::ExcedeTotal);
        if (motivo) {
            omitir(*motivo);
            continue;
        }

        contenido.resize(static_cast<qsizetype>(leidos));
        ++resumen.xmlEntregados;
        if (!alXml(EntradaXmlCfdi{static_cast<int>(i), std::move(contenido)})) {
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

} // namespace satcfdi
