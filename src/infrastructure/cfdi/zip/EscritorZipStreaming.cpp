#include "infrastructure/cfdi/zip/EscritorZipStreaming.h"

#include <QDate>
#include <QIODevice>
#include <QLoggingCategory>
#include <QSet>
#include <QTime>

#include <zlib.h>

#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <vector>

namespace satcfdi {

Q_LOGGING_CATEGORY(lcEscritorZip, "satcfdi.exportacion.zip")

using exportacion::ArchivoParcialExportacion;
using exportacion::ErrorZipExportacion;
using exportacion::ResumenZipExportacion;
using Codigo = ErrorZipExportacion::Codigo;

namespace {

constexpr quint32 kFirmaLocal = 0x04034b50;
constexpr quint32 kFirmaDescriptor = 0x08074b50;
constexpr quint32 kFirmaCentral = 0x02014b50;
constexpr quint32 kFirmaEocd = 0x06054b50;
constexpr quint32 kFirmaEocd64 = 0x06064b50;
constexpr quint32 kFirmaLocalizador64 = 0x07064b50;
constexpr quint16 kVersion20 = 20;
constexpr quint16 kVersion45 = 45;
constexpr quint16 kSistemaUnix = 3;
constexpr quint16 kBitDescriptor = 0x0008;
constexpr quint16 kMetodoDeflate = 8;
constexpr quint16 kExtraZip64 = 0x0001;
constexpr quint32 kAtributosExternos = static_cast<quint32>(S_IFREG | 0600) << 16;
constexpr quint32 kMax32 = 0xFFFFFFFFU;
constexpr quint16 kMax16 = 0xFFFF;
constexpr qsizetype kTrozoEntrada = 256 * 1024;
constexpr qsizetype kBuferSalida = 64 * 1024;
constexpr qsizetype kMaxNombre = 1024;
constexpr int kEntradasPorComprobacion = 256;

void le16(QByteArray& b, quint16 v)
{
    b.append(static_cast<char>(v & 0xFF));
    b.append(static_cast<char>((v >> 8) & 0xFF));
}
void le32(QByteArray& b, quint32 v)
{
    for (int i = 0; i < 4; ++i) {
        b.append(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}
void le64(QByteArray& b, quint64 v)
{
    for (int i = 0; i < 8; ++i) {
        b.append(static_cast<char>((v >> (8 * i)) & 0xFF));
    }
}

quint32 a32(quint64 v, bool desborda)
{
    return desborda ? kMax32 : static_cast<quint32>(v);
}

struct EntradaCentral {
    QByteArray nombre;
    quint32 crc = 0;
    quint64 comprimido = 0;
    quint64 descomprimido = 0;
    quint64 desplazamiento = 0;
    quint16 version = kVersion20;
    bool zip64Tamanos = false; // encabezado local con extra ZIP64 y descriptor de 8 bytes
};

} // namespace

struct EscritorZipStreaming::Estado {
    enum class Fase { Nuevo, Abierto, Cerrado, Terminal };

    Opciones opciones;
    Fase fase = Fase::Nuevo;
    ArchivoParcialExportacion* parcial = nullptr;
    std::function<bool()> continuar;
    quint16 horaDos = 0;
    quint16 fechaDos = 0;
    quint64 desplazamiento = 0; // bytes emitidos (incluido lo que esta en `salida`)
    QByteArray salida;
    std::vector<unsigned char> comprimido; // bufer de deflate
    std::vector<EntradaCentral> entradas;
    QSet<QByteArray> nombres;
    bool zip64 = false;

    bool sigue() const { return !continuar || continuar(); }

    Resultado<Exito, ErrorZipExportacion> fallar(ErrorZipExportacion e)
    {
        fase = Fase::Terminal;
        salida.clear();
        qCWarning(lcEscritorZip).noquote() << "falla; codigo:" << claveEstable(e.codigo)
                                           << "errno:" << e.errnoSistema;
        return Resultado<Exito, ErrorZipExportacion>::fallo(e);
    }

    ErrorZipExportacion errorEscritura() const
    {
        if (const auto e = parcial->errorEscritura()) {
            return ErrorZipExportacion::de(Codigo::Escritura, e->errnoSistema, e->codigo);
        }
        return ErrorZipExportacion::de(Codigo::Escritura, EIO);
    }

    // Vacia `salida` al dispositivo.
    bool vaciar()
    {
        qsizetype hecho = 0;
        while (hecho < salida.size()) {
            const qint64 n = parcial->dispositivo().write(salida.constData() + hecho, salida.size() - hecho);
            if (n <= 0) {
                return false;
            }
            hecho += static_cast<qsizetype>(n);
        }
        salida.clear();
        return true;
    }

    bool emitir(const char* datos, qsizetype n)
    {
        desplazamiento += static_cast<quint64>(n);
        while (n > 0) {
            const qsizetype cabe = std::min(n, kBuferSalida - salida.size());
            salida.append(datos, cabe);
            datos += cabe;
            n -= cabe;
            if (salida.size() >= kBuferSalida && !vaciar()) {
                return false;
            }
        }
        return true;
    }
    bool emitir(const QByteArray& b) { return emitir(b.constData(), b.size()); }
};

EscritorZipStreaming::EscritorZipStreaming() : EscritorZipStreaming(Opciones{}) {}

EscritorZipStreaming::EscritorZipStreaming(Opciones opciones) : m_estado(std::make_unique<Estado>())
{
    m_estado->opciones = opciones;
    m_estado->opciones.umbralZip64 = std::clamp<quint64>(opciones.umbralZip64, 1, kMax32);
}

EscritorZipStreaming::~EscritorZipStreaming() = default;

exportacion::FabricaEscritorZip EscritorZipStreaming::fabrica()
{
    return fabrica(Opciones{});
}

exportacion::FabricaEscritorZip EscritorZipStreaming::fabrica(Opciones opciones)
{
    return [opciones]() -> std::unique_ptr<exportacion::EscritorZipExportacion> {
        return std::make_unique<EscritorZipStreaming>(opciones);
    };
}

std::pair<quint16, quint16> EscritorZipStreaming::fechaHoraDos(const QDateTime& utc)
{
    QDateTime local = utc.toLocalTime();
    const QDate d = local.date();
    if (!local.isValid() || d.year() < 1980) {
        return {0, static_cast<quint16>((1 << 5) | 1)}; // 1980-01-01 00:00:00
    }
    if (d.year() > 2107) {
        return {static_cast<quint16>((23 << 11) | (59 << 5) | 29),
                static_cast<quint16>((127 << 9) | (12 << 5) | 31)}; // 2107-12-31 23:59:58
    }
    const QTime t = local.time();
    const auto hora = static_cast<quint16>((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
    const auto fecha = static_cast<quint16>(((d.year() - 1980) << 9) | (d.month() << 5) | d.day());
    return {hora, fecha};
}

bool EscritorZipStreaming::esNombreEntradaValido(const QString& nombre)
{
    if (nombre.isEmpty() || nombre.size() > kMaxNombre) {
        return false;
    }
    // Seguridad 8: solo [A-Za-z0-9._-] y '/' como separador (nada de
    // espacios, ':', '*', '?', '<', '>', '|', '"', '\\' ni controles).
    for (const QChar c : nombre) {
        const char16_t u = c.unicode();
        const bool permitido = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9')
            || u == u'.' || u == u'_' || u == u'-' || u == u'/';
        if (!permitido) {
            return false;
        }
    }
    const QStringList partes = nombre.split(QLatin1Char('/'));
    for (const QString& p : partes) {
        if (p.isEmpty() || p == QLatin1String(".") || p == QLatin1String("..") || p.startsWith(QLatin1String("._"))
            || p == QLatin1String("__MACOSX")) {
            return false; // tambien '/' inicial, final o doble
        }
    }
    return true;
}

Resultado<Exito, ErrorZipExportacion> EscritorZipStreaming::abrir(ArchivoParcialExportacion& parcial,
                                                                  QDateTime fechaExportacionUtc,
                                                                  std::function<bool()> continuar)
{
    using R = Resultado<Exito, ErrorZipExportacion>;
    Estado& s = *m_estado;
    if (s.fase != Estado::Fase::Nuevo) {
        return R::fallo(ErrorZipExportacion::de(Codigo::Estado));
    }
    QIODevice& d = parcial.dispositivo();
    if (!d.isOpen() || !d.isWritable() || d.pos() != 0 || d.size() != 0 || parcial.errorEscritura()) {
        return s.fallar(ErrorZipExportacion::de(Codigo::Estado));
    }
    s.parcial = &parcial;
    s.continuar = std::move(continuar);
    const auto [hora, fecha] = fechaHoraDos(fechaExportacionUtc);
    s.horaDos = hora;
    s.fechaDos = fecha;
    s.salida.reserve(kBuferSalida);
    s.comprimido.resize(static_cast<size_t>(kBuferSalida));
    s.fase = Estado::Fase::Abierto;
    return R::exito(Exito{});
}

Resultado<Exito, ErrorZipExportacion> EscritorZipStreaming::agregar(const QString& nombreEntrada,
                                                                    QByteArrayView bytesValidados)
{
    using R = Resultado<Exito, ErrorZipExportacion>;
    Estado& s = *m_estado;
    if (s.fase != Estado::Fase::Abierto) {
        return R::fallo(ErrorZipExportacion::de(Codigo::Estado));
    }
    if (!esNombreEntradaValido(nombreEntrada)) {
        return R::fallo(ErrorZipExportacion::de(Codigo::NombreInvalido)); // no consume el escritor
    }
    const QByteArray nombre = nombreEntrada.toLatin1();
    if (s.nombres.contains(nombre)) {
        return R::fallo(ErrorZipExportacion::de(Codigo::NombreDuplicado));
    }
    if (!s.sigue()) {
        return s.fallar(ErrorZipExportacion::de(Codigo::Cancelado));
    }
    const quint64 umbral = s.opciones.umbralZip64;
    const auto tamano = static_cast<quint64>(bytesValidados.size());
    const quint64 cota = ::deflateBound(nullptr, static_cast<uLong>(tamano));
    if (cota < tamano) {
        return s.fallar(ErrorZipExportacion::de(Codigo::EntradaGrande));
    }

    EntradaCentral e;
    e.nombre = nombre;
    e.desplazamiento = s.desplazamiento;
    e.descomprimido = tamano;
    e.zip64Tamanos = tamano >= umbral || cota >= umbral;
    e.version = (e.zip64Tamanos || e.desplazamiento >= umbral) ? kVersion45 : kVersion20;

    // Encabezado local: CRC y tamanos en el descriptor (bit 3).
    QByteArray h;
    h.reserve(30 + nombre.size() + 20);
    le32(h, kFirmaLocal);
    le16(h, e.version);
    le16(h, kBitDescriptor);
    le16(h, kMetodoDeflate);
    le16(h, s.horaDos);
    le16(h, s.fechaDos);
    le32(h, 0);
    le32(h, e.zip64Tamanos ? kMax32 : 0);
    le32(h, e.zip64Tamanos ? kMax32 : 0);
    le16(h, static_cast<quint16>(nombre.size()));
    le16(h, e.zip64Tamanos ? 20 : 0);
    h.append(nombre);
    if (e.zip64Tamanos) {
        le16(h, kExtraZip64);
        le16(h, 16);
        le64(h, 0); // descomprimido (en el descriptor)
        le64(h, 0); // comprimido (en el descriptor)
    }
    if (!s.emitir(h)) {
        return s.fallar(s.errorEscritura());
    }

    // Datos: deflate raw por trozos, sin copiar la vista.
    z_stream zs{};
    if (::deflateInit2(&zs, s.opciones.nivelCompresion, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return s.fallar(ErrorZipExportacion::de(Codigo::Compresion));
    }
    struct Fin {
        z_stream& z;
        ~Fin() { ::deflateEnd(&z); }
    } fin{zs};
    uLong crc = ::crc32(0L, Z_NULL, 0);
    quint64 comprimido = 0;
    const auto* p = reinterpret_cast<const Bytef*>(bytesValidados.data());
    qsizetype resta = bytesValidados.size();
    int ret = Z_OK;
    int flush = Z_NO_FLUSH;
    do {
        if (!s.sigue()) {
            return s.fallar(ErrorZipExportacion::de(Codigo::Cancelado));
        }
        const qsizetype trozo = std::min(resta, kTrozoEntrada);
        flush = trozo == resta ? Z_FINISH : Z_NO_FLUSH;
        if (trozo > 0) {
            crc = ::crc32_z(crc, p, static_cast<z_size_t>(trozo));
        }
        zs.next_in = const_cast<Bytef*>(p);
        zs.avail_in = static_cast<uInt>(trozo);
        do {
            zs.next_out = s.comprimido.data();
            zs.avail_out = static_cast<uInt>(s.comprimido.size());
            ret = ::deflate(&zs, flush);
            if (ret == Z_STREAM_ERROR) {
                return s.fallar(ErrorZipExportacion::de(Codigo::Compresion));
            }
            const auto producido = static_cast<qsizetype>(s.comprimido.size() - zs.avail_out);
            comprimido += static_cast<quint64>(producido);
            if (producido > 0 && !s.emitir(reinterpret_cast<const char*>(s.comprimido.data()), producido)) {
                return s.fallar(s.errorEscritura());
            }
        } while (zs.avail_out == 0);
        p += trozo;
        resta -= trozo;
    } while (flush != Z_FINISH);
    if (ret != Z_STREAM_END) {
        return s.fallar(ErrorZipExportacion::de(Codigo::Compresion));
    }
    e.crc = static_cast<quint32>(crc);
    e.comprimido = comprimido;
    if (!e.zip64Tamanos && comprimido >= umbral) {
        return s.fallar(ErrorZipExportacion::de(Codigo::Interno)); // imposible: cota >= comprimido
    }

    // Descriptor de datos.
    QByteArray dd;
    le32(dd, kFirmaDescriptor);
    le32(dd, e.crc);
    if (e.zip64Tamanos) {
        le64(dd, e.comprimido);
        le64(dd, e.descomprimido);
    } else {
        le32(dd, static_cast<quint32>(e.comprimido));
        le32(dd, static_cast<quint32>(e.descomprimido));
    }
    if (!s.emitir(dd)) {
        return s.fallar(s.errorEscritura());
    }
    s.zip64 = s.zip64 || e.version == kVersion45;
    s.nombres.insert(nombre);
    s.entradas.push_back(std::move(e));
    return R::exito(Exito{});
}

Resultado<ResumenZipExportacion, ErrorZipExportacion> EscritorZipStreaming::cerrar()
{
    using R = Resultado<ResumenZipExportacion, ErrorZipExportacion>;
    Estado& s = *m_estado;
    const auto fallar = [&](ErrorZipExportacion e) { return R::fallo(s.fallar(e).error()); };
    if (s.fase != Estado::Fase::Abierto) {
        return R::fallo(ErrorZipExportacion::de(Codigo::Estado));
    }
    if (!s.sigue()) {
        return fallar(ErrorZipExportacion::de(Codigo::Cancelado));
    }
    const quint64 umbral = s.opciones.umbralZip64;
    const quint64 inicioCentral = s.desplazamiento;
    int i = 0;
    for (const EntradaCentral& e : s.entradas) {
        if (++i % kEntradasPorComprobacion == 0 && !s.sigue()) {
            return fallar(ErrorZipExportacion::de(Codigo::Cancelado));
        }
        const bool d64 = e.zip64Tamanos || e.descomprimido >= umbral;
        const bool c64 = e.zip64Tamanos || e.comprimido >= umbral;
        const bool o64 = e.desplazamiento >= umbral;
        const quint16 extra = static_cast<quint16>((d64 || c64 || o64) ? 4 + 8 * (int{d64} + int{c64} + int{o64}) : 0);
        QByteArray c;
        c.reserve(46 + e.nombre.size() + extra);
        le32(c, kFirmaCentral);
        le16(c, static_cast<quint16>((kSistemaUnix << 8) | e.version));
        le16(c, e.version);
        le16(c, kBitDescriptor);
        le16(c, kMetodoDeflate);
        le16(c, s.horaDos);
        le16(c, s.fechaDos);
        le32(c, e.crc);
        le32(c, a32(e.comprimido, c64));
        le32(c, a32(e.descomprimido, d64));
        le16(c, static_cast<quint16>(e.nombre.size()));
        le16(c, extra);
        le16(c, 0); // comentario
        le16(c, 0); // disco
        le16(c, 0); // atributos internos
        le32(c, kAtributosExternos);
        le32(c, a32(e.desplazamiento, o64));
        c.append(e.nombre);
        if (extra > 0) {
            le16(c, kExtraZip64);
            le16(c, static_cast<quint16>(extra - 4));
            if (d64) {
                le64(c, e.descomprimido);
            }
            if (c64) {
                le64(c, e.comprimido);
            }
            if (o64) {
                le64(c, e.desplazamiento);
            }
        }
        if (!s.emitir(c)) {
            return fallar(s.errorEscritura());
        }
    }
    const quint64 tamanoCentral = s.desplazamiento - inicioCentral;
    const auto total = static_cast<quint64>(s.entradas.size());
    const bool n64 = total >= kMax16;
    const bool t64 = tamanoCentral >= umbral;
    const bool i64 = inicioCentral >= umbral;
    QByteArray fin;
    if (n64 || t64 || i64) {
        s.zip64 = true;
        const quint64 inicioEocd64 = s.desplazamiento;
        le32(fin, kFirmaEocd64);
        le64(fin, 44);
        le16(fin, static_cast<quint16>((kSistemaUnix << 8) | kVersion45));
        le16(fin, kVersion45);
        le32(fin, 0);
        le32(fin, 0);
        le64(fin, total);
        le64(fin, total);
        le64(fin, tamanoCentral);
        le64(fin, inicioCentral);
        le32(fin, kFirmaLocalizador64);
        le32(fin, 0);
        le64(fin, inicioEocd64);
        le32(fin, 1);
    }
    le32(fin, kFirmaEocd);
    le16(fin, 0);
    le16(fin, 0);
    le16(fin, n64 ? kMax16 : static_cast<quint16>(total));
    le16(fin, n64 ? kMax16 : static_cast<quint16>(total));
    le32(fin, a32(tamanoCentral, t64));
    le32(fin, a32(inicioCentral, i64));
    le16(fin, 0); // comentario del archivo
    if (!s.emitir(fin) || !s.vaciar()) {
        return fallar(s.errorEscritura());
    }
    if (!s.sigue()) {
        return fallar(ErrorZipExportacion::de(Codigo::Cancelado));
    }
    ResumenZipExportacion r;
    r.entradas = static_cast<int>(s.entradas.size());
    r.bytes = s.desplazamiento;
    r.zip64 = s.zip64;
    s.fase = Estado::Fase::Cerrado;
    s.entradas.clear();
    s.entradas.shrink_to_fit();
    s.nombres.clear();
    qCInfo(lcEscritorZip).noquote() << "zip cerrado; entradas:" << r.entradas << "bytes:" << r.bytes
                                    << "zip64:" << r.zip64;
    return R::exito(r);
}

void EscritorZipStreaming::abortar() noexcept
{
    Estado& s = *m_estado;
    if (s.fase == Estado::Fase::Abierto) {
        qCInfo(lcEscritorZip).noquote() << "zip abortado; entradas:" << static_cast<int>(s.entradas.size());
    }
    s.fase = Estado::Fase::Terminal;
    s.salida.clear();
}

} // namespace satcfdi
