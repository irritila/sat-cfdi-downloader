#pragma once

// T026.2: piezas comunes de las pruebas de ExportacionFacturasService.
// ZIP de paquetes sinteticos reales (libzip) bajo una raiz de paquetes con la
// forma de T008 D5, planes construidos a mano con la huella real del ZIP,
// puertos del servicio con adaptadores reales (LectorZipLibzip,
// ArchivosZipCfdiFs, parsearCfdi, EscritorZipStreaming) y lectura del ZIP
// exportado con libzip. Datos sinteticos: RFC genericos del SAT
// (XAXX010101000, XEXX010101000) y UUID inventados.

#include "application/cfdi/ExportacionFacturasService.h"
#include "domain/cfdi/ExportacionFacturas.h"
#include "domain/cfdi/UuidCfdi.h"
#include "fakes/ZipCfdiSintetico.h"
#include "infrastructure/cfdi/xml/ParserCfdi.h"
#include "infrastructure/cfdi/zip/ArchivosZipCfdiFs.h"
#include "infrastructure/cfdi/zip/EscritorZipStreaming.h"
#include "infrastructure/cfdi/zip/LectorZipLibzip.h"
#include "ports/storage/RutaPaquete.h"

#include <zip.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QFuture>

#include <memory>
#include <optional>

namespace exportacionpruebas {

using namespace satcfdi;

inline const QString kRfc = QStringLiteral("XAXX010101000");
inline const MesCalendario kAgosto = *MesCalendario::crear(2026, 8);
inline const MesCalendario kSeptiembre = *MesCalendario::crear(2026, 9);
inline const QDateTime kInstante = QDateTime(QDate(2026, 10, 20), QTime(18, 0), QTimeZone(QTimeZone::UTC));

inline QString uuidN(int n)
{
    return QStringLiteral("E0000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0'));
}

inline QByteArray sha256(const QByteArray& datos)
{
    return QCryptographicHash::hash(datos, QCryptographicHash::Sha256);
}

// CFDI 4.0 sintetico con el UUID `n`.
inline QByteArray xml40(int n, const QString& rfcEmisor = kRfc)
{
    fakes::CfdiSintetico c;
    c.uuid = uuidN(n);
    c.folio = QString::number(n);
    c.rfcEmisor = rfcEmisor;
    return fakes::xmlCfdi40(c);
}

// CFDI 3.2 (VersionNoSoportada, T015 D5) con el UUID `n` en el timbre.
inline QByteArray xml32(int n)
{
    QByteArray x = xml40(n);
    x.replace("http://www.sat.gob.mx/cfd/4", "http://www.sat.gob.mx/cfd/3");
    x.replace("Version=\"4.0\" Serie", "version=\"3.2\" Serie");
    return x;
}

template <typename T>
std::optional<T> esperarFuturo(QFuture<T> f, int ms = 30000)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, ms) || f.isCanceled() || f.resultCount() == 0) {
        return std::nullopt;
    }
    return f.result();
}

// Entradas de un ZIP (nombre -> contenido), verificando consistencia.
inline std::optional<QMap<QString, QByteArray>> leerZipBytes(const QByteArray& bytes)
{
    zip_error_t error;
    zip_error_init(&error);
    zip_source_t* fuente = zip_source_buffer_create(bytes.constData(), zip_uint64_t(bytes.size()), 0, &error);
    if (!fuente) {
        zip_error_fini(&error);
        return std::nullopt;
    }
    zip_t* zip = zip_open_from_source(fuente, ZIP_RDONLY | ZIP_CHECKCONS, &error);
    zip_error_fini(&error);
    if (!zip) {
        zip_source_free(fuente);
        return std::nullopt;
    }
    QMap<QString, QByteArray> entradas;
    const zip_int64_t n = zip_get_num_entries(zip, 0);
    for (zip_int64_t i = 0; i < n; ++i) {
        zip_stat_t st;
        zip_stat_init(&st);
        if (zip_stat_index(zip, zip_uint64_t(i), 0, &st) != 0) {
            zip_discard(zip);
            return std::nullopt;
        }
        QByteArray contenido(qsizetype(st.size), Qt::Uninitialized);
        zip_file_t* f = zip_fopen_index(zip, zip_uint64_t(i), 0);
        if (!f || zip_fread(f, contenido.data(), st.size) != zip_int64_t(st.size)) {
            if (f) {
                zip_fclose(f);
            }
            zip_discard(zip);
            return std::nullopt;
        }
        zip_fclose(f);
        entradas.insert(QString::fromUtf8(st.name), contenido);
    }
    zip_discard(zip);
    return entradas;
}

inline std::optional<QMap<QString, QByteArray>> leerZipArchivo(const QString& ruta)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    return leerZipBytes(f.readAll());
}

// Paquetes fuente reales bajo <tmp>/paquetes y facturas planeadas con su huella.
struct Banco {
    QTemporaryDir dir;
    QString raiz;
    std::unique_ptr<ArchivosZipCfdiFs> archivos;
    LectorZipLibzip lector;
    int siguientePaquete = 1;

    struct Paquete {
        QString id;
        QString relativa;
        QString absoluta;
        SolicitudId solicitud;
    };

    bool preparar()
    {
        if (!dir.isValid()) {
            return false;
        }
        raiz = dir.filePath(QStringLiteral("paquetes"));
        archivos = std::make_unique<ArchivosZipCfdiFs>(raiz);
        return QDir().mkpath(raiz);
    }

    // ZIP con los XML (entradas "<n>.xml"); vacio el id si fallo.
    Paquete paquete(const QList<QByteArray>& xmls, SolicitudId solicitud = SolicitudId::generar(),
                    MesCalendario mes = kSeptiembre)
    {
        Paquete p;
        p.solicitud = solicitud;
        const QString idSat = QStringLiteral("PAQ_%1").arg(siguientePaquete++);
        const auto ruta = rutapaquete::derivarRutaRelativa({kRfc, mes.inicioSat(), solicitud.texto(), idSat});
        if (!ruta) {
            return p;
        }
        p.relativa = ruta.valor();
        p.absoluta = QDir(raiz).filePath(p.relativa);
        if (!QDir().mkpath(QFileInfo(p.absoluta).absolutePath()) || !reescribir(p, xmls)) {
            return p;
        }
        p.id = QStringLiteral("paquete-") + idSat;
        return p;
    }

    bool reescribir(const Paquete& p, const QList<QByteArray>& xmls) const
    {
        QList<std::pair<QByteArray, QByteArray>> entradas;
        for (qsizetype i = 0; i < xmls.size(); ++i) {
            entradas.append({QByteArray::number(i) + ".xml", xmls.at(i)});
        }
        return fakes::escribirArchivo(p.absoluta, fakes::crearZipSintetico(entradas));
    }

    // Huella real del ZIP como la guardaria el indice.
    HuellaZip huella(const Paquete& p) const
    {
        const ArchivoZipLocal a = archivos->consultar(p.relativa);
        HuellaZip h;
        h.tamano = a.tamano;
        h.modificadoMs = a.modificadoMs;
        h.sha256 = archivos->sha256(a.rutaAbsoluta, [] { return true; }).value_or(QString());
        return h;
    }

    // Factura planeada en (paquete, posicion) con nombre base = UUID canonico.
    FacturaPlaneada factura(const Paquete& p, int posicion, int uuid, TipoDescarga d = TipoDescarga::Emitidos,
                            MesCalendario mes = kSeptiembre) const
    {
        FacturaPlaneada f;
        f.origen = ReferenciaFactura{p.id, posicion};
        f.mes = mes;
        f.direccion = d;
        f.uuidCanonico = cfdi::normalizarUuid(uuidN(uuid));
        f.nombre = NombreEntradaFactura{QString(carpetaDireccion(d)) + QLatin1Char('/') + mes.texto(),
                                        f.uuidCanonico.value_or(QString())};
        f.rutaPaquete = p.relativa;
        f.huellaPaquete = huella(p);
        f.solicitud = p.solicitud;
        return f;
    }
};

inline ConsultaPaqueteExportableFn siempreVisible()
{
    return [](const SolicitudId&, const QString&) {
        return QtFuture::makeReadyValueFuture(Resultado<bool, ErrorPersistencia>::exito(true));
    };
}

inline LimpiezaInicialExportacionFn limpiezaHecha(int borrados = 0)
{
    return [borrados] { return QtFuture::makeReadyValueFuture(borrados); };
}

inline exportacion::FabricaEscritorZip fabricaEscritorReal()
{
    return []() -> std::unique_ptr<exportacion::EscritorZipExportacion> {
        return std::make_unique<EscritorZipStreaming>();
    };
}

// Puertos con propiedad compartida (C7): el servicio los retiene mientras
// viva su hilo.
inline PuertosExportacionFacturas puertos(std::shared_ptr<exportacion::DestinoExportacion> destino,
                                          std::shared_ptr<CfdiArchiveReader> lector,
                                          std::shared_ptr<ArchivosZipCfdi> archivos, FabricaRendererPdf renderer,
                                          ConsultaPaqueteExportableFn visible = siempreVisible(),
                                          LimpiezaInicialExportacionFn limpieza = limpiezaHecha())
{
    return PuertosExportacionFacturas{
        {},
        std::move(visible),
        std::move(limpieza),
        std::move(destino),
        fabricaEscritorReal(),
        std::move(lector),
        std::move(archivos),
        &parsearCfdi,
        std::move(renderer),
        [] { return kInstante; },
        std::chrono::seconds(10),
        {},
    };
}

// Prestamo sin propiedad: solo para objetos de la prueba que viven mas que el
// servicio Y su hilo (servicio destruido con el hilo ya unido). Una prueba que
// abandona el hilo usa la sobrecarga compartida.
template <typename T>
std::shared_ptr<T> prestado(T& objeto)
{
    return std::shared_ptr<T>(std::shared_ptr<T>(), &objeto); // alias vacio: no libera
}

inline PuertosExportacionFacturas puertos(exportacion::DestinoExportacion& destino, CfdiArchiveReader& lector,
                                          ArchivosZipCfdi& archivos, FabricaRendererPdf renderer,
                                          ConsultaPaqueteExportableFn visible = siempreVisible(),
                                          LimpiezaInicialExportacionFn limpieza = limpiezaHecha())
{
    return puertos(prestado(destino), prestado(lector), prestado(archivos), std::move(renderer), std::move(visible),
                   std::move(limpieza));
}

} // namespace exportacionpruebas
