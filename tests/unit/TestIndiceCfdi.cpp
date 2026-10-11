#include "TestIndiceCfdi.h"

#include "application/cfdi/CalculoIndicePaquete.h"
#include "application/cfdi/CatalogoCfdiService.h"
#include "application/cfdi/IndexadorCfdi.h"
#include "application/persistence/PersistenceDispatcher.h"
#include "domain/cfdi/IndiceCfdi.h"
#include "ports/repositories/UnitOfWork.h"

#include "fakes/FakeArchivosZipCfdi.h"
#include "fakes/FakeCfdiArchiveReader.h"
#include "fakes/LectorCfdiCompuerta.h"

#include <QDeadlineTimer>
#include <QHash>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QThread>
#include <QTimeZone>
#include <QWaitCondition>

#include <atomic>

using namespace satcfdi;

namespace {

const QDateTime kAhora = QDateTime(QDate(2026, 10, 11), QTime(12, 0), QTimeZone::UTC);
const QString kShaA = QString(64, QLatin1Char('a'));
const QString kShaB = QString(64, QLatin1Char('b'));

// "I|<uuid>|<nombre>" -> Completo (Ingreso); "X" -> Ilegible; "N" -> NoEsCfdi;
// "V" -> VersionNoSoportada.
ResultadoParseoCfdi parserFalso(QByteArrayView xml)
{
    ResultadoParseoCfdi r;
    const QString texto = QString::fromUtf8(xml);
    if (texto.startsWith(QLatin1Char('I'))) {
        const QStringList partes = texto.split(QLatin1Char('|'));
        r.resumen.estado = EstadoParseo::Completo;
        r.resumen.tipo = TipoComprobante::Ingreso;
        r.resumen.uuid = partes.value(1);
        r.resumen.emisor.nombre = partes.value(2);
        r.resumen.emisor.rfc = QStringLiteral("XAXX010101000");
        r.resumen.fecha = QStringLiteral("2026-01-01T00:00:00");
    } else if (texto == QLatin1String("N")) {
        r.resumen.estado = EstadoParseo::NoEsCfdi;
    } else if (texto == QLatin1String("V")) {
        r.resumen.estado = EstadoParseo::VersionNoSoportada;
    } else {
        r.resumen.estado = EstadoParseo::Ilegible;
        r.causaIlegible = CausaIlegible::MalFormado;
    }
    return r;
}

PaqueteIndexable paquete(const QString& id, const QString& ruta, const SolicitudId& solicitud = SolicitudId::generar())
{
    PaqueteIndexable p;
    p.paqueteId = id;
    p.solicitudId = solicitud;
    p.rutaLocal = ruta;
    return p;
}

class UnidadNula final : public UnitOfWork {
public:
    Resultado<Exito, ErrorPersistencia> begin() override { return Resultado<Exito, ErrorPersistencia>::exito({}); }
    Resultado<Exito, ErrorPersistencia> commit() override { return Resultado<Exito, ErrorPersistencia>::exito({}); }
    Resultado<Exito, ErrorPersistencia> rollback() override { return Resultado<Exito, ErrorPersistencia>::exito({}); }
};

// Catalogo en memoria (se usa en el hilo del dispatcher).
class CatalogoMemoria final : public CfdiCatalogRepository {
public:
    void agregarPaquete(const PaqueteIndexable& p)
    {
        QMutexLocker b(&m_mutex);
        m_paquetes.append(p);
    }
    std::optional<IndicePaquete> indice(const QString& paqueteId) const
    {
        QMutexLocker b(&m_mutex);
        const auto it = m_indices.constFind(paqueteId);
        return it == m_indices.constEnd() ? std::nullopt : std::optional(*it);
    }
    int escrituras() const
    {
        QMutexLocker b(&m_mutex);
        return m_escrituras;
    }
    // Fallos programados (mensaje con datos que nunca deben ir a logs).
    void fallarLectura(bool fallar)
    {
        QMutexLocker b(&m_mutex);
        m_fallarLectura = fallar;
    }
    void fallarEscritura(bool fallar)
    {
        QMutexLocker b(&m_mutex);
        m_fallarEscritura = fallar;
    }
    static ErrorPersistencia errorSecreto()
    {
        return ErrorPersistencia::de(ErrorPersistencia::Tipo::Integridad,
                                     QStringLiteral("MENSAJE-SECRETO XAXX010101000 /Users/x/p.zip"),
                                     QStringLiteral("RESTRICCION-SECRETA"));
    }

    QThread* hiloEscritura() const
    {
        QMutexLocker b(&m_mutex);
        return m_hiloEscritura;
    }

    Resultado<QList<PaqueteIndexable>, ErrorPersistencia>
    listarPaquetesIndexables(const std::optional<SolicitudId>& solicitud) override
    {
        QMutexLocker b(&m_mutex);
        if (m_fallarLectura) {
            return Resultado<QList<PaqueteIndexable>, ErrorPersistencia>::fallo(errorSecreto());
        }
        QList<PaqueteIndexable> lista;
        for (PaqueteIndexable p : std::as_const(m_paquetes)) {
            if (solicitud && p.solicitudId != *solicitud) {
                continue;
            }
            const auto it = m_indices.constFind(p.paqueteId);
            if (it != m_indices.constEnd()) {
                p.estado = it->estado;
                p.huella = it->huella;
                p.versionParser = it->versionParser;
            }
            lista.append(p);
        }
        return Resultado<QList<PaqueteIndexable>, ErrorPersistencia>::exito(lista);
    }
    Resultado<bool, ErrorPersistencia> reemplazarIndicePaquete(const IndicePaquete& indice) override
    {
        QMutexLocker b(&m_mutex);
        if (m_fallarEscritura) {
            return Resultado<bool, ErrorPersistencia>::fallo(errorSecreto());
        }
        m_indices.insert(indice.paqueteId, indice);
        ++m_escrituras;
        m_hiloEscritura = QThread::currentThread();
        return Resultado<bool, ErrorPersistencia>::exito(true);
    }
    Resultado<bool, ErrorPersistencia> actualizarMetadatosHuella(const QString& paqueteId, const HuellaZip& huella,
                                                                 const QDateTime&) override
    {
        QMutexLocker b(&m_mutex);
        auto it = m_indices.find(paqueteId);
        if (it == m_indices.end()) {
            return Resultado<bool, ErrorPersistencia>::exito(false);
        }
        it->huella = huella;
        ++m_escrituras;
        return Resultado<bool, ErrorPersistencia>::exito(true);
    }
    Resultado<QList<CfdiListado>, ErrorPersistencia> listarCfdi(const SolicitudId&, const FiltroListaCfdi&) override
    {
        return Resultado<QList<CfdiListado>, ErrorPersistencia>::exito({});
    }
    Resultado<ResumenIndiceSolicitud, ErrorPersistencia> resumirSolicitud(const SolicitudId&, int) override
    {
        ResumenIndiceSolicitud r;
        r.paquetesPendientes = 1;
        return Resultado<ResumenIndiceSolicitud, ErrorPersistencia>::exito(r);
    }

private:
    mutable QMutex m_mutex;
    QList<PaqueteIndexable> m_paquetes;
    QHash<QString, IndicePaquete> m_indices;
    int m_escrituras = 0;
    QThread* m_hiloEscritura = nullptr;
    bool m_fallarLectura = false;
    bool m_fallarEscritura = false;
};

struct EntornoIndexador {
    PersistenceDispatcher dispatcher;
    CatalogoMemoria catalogo;
    UnidadNula uow;
    fakes::FakeCfdiArchiveReader interno;
    fakes::LectorCfdiCompuerta lector{interno};
    fakes::FakeArchivosZipCfdi archivos;
    std::unique_ptr<IndexadorCfdi> indexador;

    EntornoIndexador()
    {
        indexador = std::make_unique<IndexadorCfdi>(PuertosIndexadorCfdi{
            dispatcher, catalogo, uow, lector, archivos, &parserFalso, 1, [] { return kAhora; }});
    }
    ~EntornoIndexador()
    {
        indexador.reset();
        dispatcher.cerrar();
    }
};

QStringList g_logs;
QMutex g_mutexLogs;

void capturarLog(QtMsgType, const QMessageLogContext&, const QString& mensaje)
{
    QMutexLocker b(&g_mutexLogs);
    g_logs.append(mensaje);
}

} // namespace

void TestIndiceCfdi::normalizarTexto()
{
    QCOMPARE(normalizarTextoBusqueda(u"  José   PEÑA Ávila "), QStringLiteral("jose pena avila"));
    QCOMPARE(normalizarTextoBusqueda(u"ÁÉÍÓÚÜ áéíóúü Ññ"), QStringLiteral("aeiouu aeiouu nn"));
    QCOMPARE(normalizarTextoBusqueda(u"AAA&010101AAA"), QStringLiteral("aaa&010101aaa"));
    QCOMPARE(normalizarTextoBusqueda(u"ÇA ÑÚ"), QStringLiteral("ca nu"));
    QCOMPARE(normalizarTextoBusqueda(u""), QString());
}

void TestIndiceCfdi::textoBusquedaDeResumen()
{
    ResumenCfdi r;
    r.emisor.rfc = QStringLiteral("PEÑ010101AAA");
    r.emisor.nombre = QStringLiteral("José Peña");
    r.receptor.rfc = QStringLiteral("XAXX010101000");
    r.uuid = QStringLiteral("ABCDEF00-0000-0000-0000-000000000000");
    r.serie = QStringLiteral("F");
    r.folio = QStringLiteral("12");
    QCOMPARE(textoBusquedaCfdi(r),
             QStringLiteral("pen010101aaa jose pena xaxx010101000 abcdef00-0000-0000-0000-000000000000 f 12"));
}

void TestIndiceCfdi::clavesPersistidas()
{
    for (EstadoIndicePaquete e : {EstadoIndicePaquete::Pendiente, EstadoIndicePaquete::Indexado,
                                  EstadoIndicePaquete::OrigenNoDisponible, EstadoIndicePaquete::Ilegible}) {
        QCOMPARE(estadoIndiceDesdeClave(claveEstadoIndice(e).toString()), std::optional(e));
    }
    for (EstadoParseo e : {EstadoParseo::Completo, EstadoParseo::Parcial, EstadoParseo::VersionNoSoportada,
                           EstadoParseo::Ilegible, EstadoParseo::NoEsCfdi}) {
        QCOMPARE(estadoParseoDesdeClave(claveEstadoParseo(e).toString()), std::optional(e));
    }
    QVERIFY(!estadoIndiceDesdeClave(u"otro"));
}

void TestIndiceCfdi::origenNoDisponible()
{
    fakes::FakeCfdiArchiveReader lector;
    fakes::FakeArchivosZipCfdi archivos;
    EntornoCalculoIndice entorno{lector, archivos, &parserFalso, 1};
    PaqueteIndexable p = paquete(QStringLiteral("p1"), QStringLiteral("r/p1.zip"));
    auto c = calcularIndicePaquete(p, entorno, {}, kAhora);
    QCOMPARE(c.tipo, CalculoPaquete::Tipo::Reemplazar);
    QCOMPARE(c.indice.estado, EstadoIndicePaquete::OrigenNoDisponible);
    QCOMPARE(c.indice.causa, QStringLiteral("no-existe"));
    QVERIFY(!c.indice.huella && c.indice.cfdis.isEmpty());
    QVERIFY(lector.rutasRecorridas().isEmpty());

    // Ya estaba asi con la version vigente: sin cambio. Con otra version se reescribe.
    p.estado = EstadoIndicePaquete::OrigenNoDisponible;
    p.versionParser = 1;
    QCOMPARE(calcularIndicePaquete(p, entorno, {}, kAhora).tipo, CalculoPaquete::Tipo::SinCambio);
    p.versionParser = 0;
    QCOMPARE(calcularIndicePaquete(p, entorno, {}, kAhora).tipo, CalculoPaquete::Tipo::Reemplazar);

    // Ruta no propia: tambien no disponible.
    fakes::FakeArchivosZipCfdi::Archivo invalido;
    invalido.estado = ArchivoZipLocal::Estado::Invalido;
    archivos.fijar(QStringLiteral("r/p1.zip"), invalido);
    p.estado.reset();
    QCOMPARE(calcularIndicePaquete(p, entorno, {}, kAhora).indice.causa, QStringLiteral("ruta-invalida"));
}

void TestIndiceCfdi::indexaConConteos()
{
    fakes::FakeCfdiArchiveReader lector;
    lector.agregarXml("I|U-1|Uno");                       // 0
    lector.agregarOmision(MotivoOmision::NoXml);           // 1
    lector.agregarXml("X");                                // 2 ilegible (se indexa con estado)
    lector.agregarXml("N");                                // 3 no es CFDI (se cuenta)
    lector.agregarXml("V");                                // 4 no soportado
    lector.agregarXml("I|U-2|Dos");                        // 5
    fakes::FakeArchivosZipCfdi archivos;
    archivos.fijar(QStringLiteral("r/p.zip"), {});
    EntornoCalculoIndice entorno{lector, archivos, &parserFalso, 3};
    const auto c = calcularIndicePaquete(paquete(QStringLiteral("p"), QStringLiteral("r/p.zip")), entorno, {}, kAhora);
    QCOMPARE(c.tipo, CalculoPaquete::Tipo::Reemplazar);
    const IndicePaquete& i = c.indice;
    QCOMPARE(i.estado, EstadoIndicePaquete::Indexado);
    QCOMPARE(i.versionParser, 3);
    QCOMPARE(i.huella, std::optional(HuellaZip{100, 1000, kShaA}));
    QCOMPARE(i.cfdis.size(), 4);
    QCOMPARE(i.cfdis.at(0).posicion, 0);
    QCOMPARE(i.cfdis.at(1).posicion, 2);
    QCOMPARE(i.cfdis.at(3).posicion, 5);
    QCOMPARE(i.cfdis.at(3).resumen.uuid, QStringLiteral("U-2"));
    QCOMPARE(i.cfdis.at(0).textoBusqueda, QStringLiteral("xaxx010101000 uno u-1"));
    QCOMPARE(i.conteos.xmlLeidos, 5);
    QCOMPARE(i.conteos.cfdiIlegibles, 1);
    QCOMPARE(i.conteos.cfdiNoSoportados, 1);
    QCOMPARE(i.conteos.xmlNoCfdi, 1);
    QCOMPARE(i.conteos.totalOmitidas(), 1);
    QVERIFY(i.causa.isEmpty());
    QCOMPARE(i.actualizadoEn, kAhora);
}

void TestIndiceCfdi::revalidacionRapidaSinSha()
{
    fakes::FakeCfdiArchiveReader lector;
    fakes::FakeArchivosZipCfdi archivos;
    archivos.fijar(QStringLiteral("r/p.zip"), {});
    EntornoCalculoIndice entorno{lector, archivos, &parserFalso, 1};
    PaqueteIndexable p = paquete(QStringLiteral("p"), QStringLiteral("r/p.zip"));
    p.versionParser = 1;
    p.huella = HuellaZip{100, 1000, kShaA};
    for (EstadoIndicePaquete e : {EstadoIndicePaquete::Indexado, EstadoIndicePaquete::Ilegible}) {
        p.estado = e;
        QCOMPARE(calcularIndicePaquete(p, entorno, {}, kAhora).tipo, CalculoPaquete::Tipo::SinCambio);
    }
    QCOMPARE(archivos.llamadasSha(), 0);
    QVERIFY(lector.rutasRecorridas().isEmpty());
}

void TestIndiceCfdi::mismoShaSoloActualizaHuella()
{
    fakes::FakeCfdiArchiveReader lector;
    fakes::FakeArchivosZipCfdi archivos;
    fakes::FakeArchivosZipCfdi::Archivo tocado;
    tocado.modificadoMs = 5000; // "touch": mismo contenido
    archivos.fijar(QStringLiteral("r/p.zip"), tocado);
    EntornoCalculoIndice entorno{lector, archivos, &parserFalso, 1};
    PaqueteIndexable p = paquete(QStringLiteral("p"), QStringLiteral("r/p.zip"));
    p.versionParser = 1;
    p.estado = EstadoIndicePaquete::Indexado;
    p.huella = HuellaZip{100, 1000, kShaA};
    const auto c = calcularIndicePaquete(p, entorno, {}, kAhora);
    QCOMPARE(c.tipo, CalculoPaquete::Tipo::ActualizarHuella);
    QCOMPARE(c.huella, (HuellaZip{100, 5000, kShaA}));
    QCOMPARE(archivos.llamadasSha(), 1);
    QVERIFY(lector.rutasRecorridas().isEmpty());
}

void TestIndiceCfdi::contenidoDistintoReindexa()
{
    fakes::FakeCfdiArchiveReader lector;
    lector.agregarXml("I|U-9|Nueve");
    fakes::FakeArchivosZipCfdi archivos;
    fakes::FakeArchivosZipCfdi::Archivo reemplazado;
    reemplazado.tamano = 200;
    reemplazado.sha256 = kShaB;
    archivos.fijar(QStringLiteral("r/p.zip"), reemplazado);
    EntornoCalculoIndice entorno{lector, archivos, &parserFalso, 1};
    PaqueteIndexable p = paquete(QStringLiteral("p"), QStringLiteral("r/p.zip"));
    p.versionParser = 1;
    p.estado = EstadoIndicePaquete::Indexado;
    p.huella = HuellaZip{100, 1000, kShaA};
    const auto c = calcularIndicePaquete(p, entorno, {}, kAhora);
    QCOMPARE(c.tipo, CalculoPaquete::Tipo::Reemplazar);
    QCOMPARE(c.indice.estado, EstadoIndicePaquete::Indexado);
    QCOMPARE(c.indice.huella->sha256, kShaB);
    QCOMPARE(c.indice.cfdis.size(), 1);
    QCOMPARE(archivos.llamadasSha(), 1); // no se recalcula para indexar
}

void TestIndiceCfdi::versionDistintaReindexa()
{
    fakes::FakeCfdiArchiveReader lector;
    lector.agregarXml("I|U-1|Uno");
    fakes::FakeArchivosZipCfdi archivos;
    archivos.fijar(QStringLiteral("r/p.zip"), {});
    EntornoCalculoIndice entorno{lector, archivos, &parserFalso, 2};
    PaqueteIndexable p = paquete(QStringLiteral("p"), QStringLiteral("r/p.zip"));
    p.versionParser = 1;
    p.estado = EstadoIndicePaquete::Indexado;
    p.huella = HuellaZip{100, 1000, kShaA};
    const auto c = calcularIndicePaquete(p, entorno, {}, kAhora);
    QCOMPARE(c.tipo, CalculoPaquete::Tipo::Reemplazar);
    QCOMPARE(c.indice.versionParser, 2);
    QCOMPARE(lector.rutasRecorridas().size(), 1);
    // Pendiente explicito tambien reindexa.
    p.versionParser = 2;
    p.estado = EstadoIndicePaquete::Pendiente;
    QCOMPARE(calcularIndicePaquete(p, entorno, {}, kAhora).tipo, CalculoPaquete::Tipo::Reemplazar);
}

void TestIndiceCfdi::zipIlegibleSinCfdi()
{
    fakes::FakeCfdiArchiveReader lector;
    lector.agregarXml("I|U-1|Uno");
    lector.programarError({ErrorPaqueteZip::Tipo::Ilegible, ErrorPaqueteZip::Causa::Truncado});
    fakes::FakeArchivosZipCfdi archivos;
    archivos.fijar(QStringLiteral("r/p.zip"), {});
    EntornoCalculoIndice entorno{lector, archivos, &parserFalso, 1};
    auto c = calcularIndicePaquete(paquete(QStringLiteral("p"), QStringLiteral("r/p.zip")), entorno, {}, kAhora);
    QCOMPARE(c.indice.estado, EstadoIndicePaquete::Ilegible);
    QCOMPARE(c.indice.causa, QStringLiteral("truncado"));
    QVERIFY(c.indice.cfdis.isEmpty());
    QVERIFY(c.indice.huella.has_value());

    lector.programarError({ErrorPaqueteZip::Tipo::NoExiste, ErrorPaqueteZip::Causa::NoExiste});
    c = calcularIndicePaquete(paquete(QStringLiteral("p"), QStringLiteral("r/p.zip")), entorno, {}, kAhora);
    QCOMPARE(c.indice.estado, EstadoIndicePaquete::OrigenNoDisponible);
    QVERIFY(!c.indice.huella.has_value());

    // SHA-256 ilegible (permiso, E/S): origen no disponible.
    fakes::FakeArchivosZipCfdi::Archivo fallo;
    fallo.shaFalla = true;
    archivos.fijar(QStringLiteral("r/p.zip"), fallo);
    c = calcularIndicePaquete(paquete(QStringLiteral("p"), QStringLiteral("r/p.zip")), entorno, {}, kAhora);
    QCOMPARE(c.indice.estado, EstadoIndicePaquete::OrigenNoDisponible);
    QCOMPARE(c.indice.causa, QStringLiteral("error-lectura"));
}

void TestIndiceCfdi::cambioDuranteLecturaQuedaPendiente()
{
    fakes::FakeCfdiArchiveReader lector;
    lector.agregarXml("I|U-1|Uno");
    fakes::FakeArchivosZipCfdi archivos;
    archivos.fijar(QStringLiteral("r/p.zip"), {});
    archivos.cambiarTrasConsultas(QStringLiteral("r/p.zip"), 1); // la segunda consulta ve otro tamano
    EntornoCalculoIndice entorno{lector, archivos, &parserFalso, 1};
    const auto c = calcularIndicePaquete(paquete(QStringLiteral("p"), QStringLiteral("r/p.zip")), entorno, {}, kAhora);
    QCOMPARE(c.tipo, CalculoPaquete::Tipo::Reemplazar);
    QCOMPARE(c.indice.estado, EstadoIndicePaquete::Pendiente);
    QVERIFY(c.indice.cfdis.isEmpty());
}

void TestIndiceCfdi::cancelacionNoEscribe()
{
    fakes::FakeCfdiArchiveReader lector;
    for (int i = 0; i < 10; ++i) {
        lector.agregarXml(QStringLiteral("I|U-%1|N").arg(i).toUtf8());
    }
    fakes::FakeArchivosZipCfdi archivos;
    archivos.fijar(QStringLiteral("r/p.zip"), {});
    EntornoCalculoIndice entorno{lector, archivos, &parserFalso, 1};
    int consultas = 0;
    // Se cancela despues de 4 entradas (+ consultas previas: inicio y SHA).
    const auto continuar = [&consultas] { return ++consultas < 7; };
    const auto c =
        calcularIndicePaquete(paquete(QStringLiteral("p"), QStringLiteral("r/p.zip")), entorno, continuar, kAhora);
    QCOMPARE(c.tipo, CalculoPaquete::Tipo::Cancelado);
    // Cancelado antes de empezar.
    QCOMPARE(calcularIndicePaquete(paquete(QStringLiteral("p"), QStringLiteral("r/p.zip")), entorno,
                                   [] { return false; }, kAhora)
                 .tipo,
             CalculoPaquete::Tipo::Cancelado);
}

void TestIndiceCfdi::indexadorUsaHiloPropioYUnaSenalPorSolicitud()
{
    EntornoIndexador e;
    e.interno.agregarXml("I|U-1|Uno");
    const SolicitudId s1 = SolicitudId::generar();
    const SolicitudId s2 = SolicitudId::generar();
    for (int i = 0; i < 3; ++i) {
        e.catalogo.agregarPaquete(paquete(QStringLiteral("a%1").arg(i), QStringLiteral("r/a%1.zip").arg(i), s1));
        e.archivos.fijar(QStringLiteral("r/a%1.zip").arg(i), {});
    }
    e.catalogo.agregarPaquete(paquete(QStringLiteral("b0"), QStringLiteral("r/b0.zip"), s2));
    e.archivos.fijar(QStringLiteral("r/b0.zip"), {});

    QSignalSpy cambios(e.indexador.get(), &IndexadorCfdi::indiceCambiado);
    QSignalSpy ocupado(e.indexador.get(), &IndexadorCfdi::ocupadoCambiado);
    e.indexador->iniciar();
    e.indexador->iniciar(); // idempotente
    QVERIFY(e.indexador->ocupado());
    QVERIFY(e.indexador->cubre(s1));
    QTRY_VERIFY(!e.indexador->ocupado());
    QCOMPARE(e.catalogo.escrituras(), 4);
    // Una senal por solicitud, despues de sus paquetes.
    QCOMPARE(cambios.count(), 2);
    // (el orden entre solicitudes no esta garantizado: se agrupan por id)
    QCOMPARE((QSet<QString>{cambios.at(0).at(0).value<SolicitudId>().texto(),
                            cambios.at(1).at(0).value<SolicitudId>().texto()}),
             (QSet<QString>{s1.texto(), s2.texto()}));
    QCOMPARE(ocupado.count(), 2); // true y false
    // Lectura del ZIP en el hilo propio; escrituras en el del dispatcher.
    QCOMPARE(e.lector.hilo(), e.indexador->hilo());
    QVERIFY(e.lector.hilo() != QThread::currentThread());
    QVERIFY(e.lector.hilo() != e.dispatcher.hilo());
    QCOMPARE(e.catalogo.hiloEscritura(), e.dispatcher.hilo());
    QCOMPARE(e.catalogo.indice(QStringLiteral("a0"))->cfdis.size(), 1);

    // Segunda pasada sobre lo vigente: sin lecturas ni escrituras ni senales.
    const int recorridos = e.lector.recorridos();
    auto f = e.indexador->sincronizarSolicitud(s1);
    QTRY_VERIFY(f.isFinished());
    QVERIFY(!f.isCanceled());
    QCOMPARE(e.lector.recorridos(), recorridos);
    QCOMPARE(e.catalogo.escrituras(), 4);
    QCOMPARE(cambios.count(), 2);
}

void TestIndiceCfdi::indexadorCoalesceYDetener()
{
    EntornoIndexador e;
    e.interno.agregarXml("I|U-1|Uno");
    const SolicitudId s1 = SolicitudId::generar();
    e.catalogo.agregarPaquete(paquete(QStringLiteral("a0"), QStringLiteral("r/a0.zip"), s1));
    e.archivos.fijar(QStringLiteral("r/a0.zip"), {});
    e.lector.cerrar();
    auto primero = e.indexador->sincronizarSolicitud(s1);
    QVERIFY(e.lector.esperarBloqueo());
    // Mientras corre el primero, varias peticiones iguales se coalescen.
    auto segundo = e.indexador->sincronizarSolicitud(s1);
    auto tercero = e.indexador->revisarSolicitud(s1);
    e.lector.liberar();
    QTRY_VERIFY(primero.isFinished() && segundo.isFinished() && tercero.isFinished());
    QVERIFY(!segundo.isCanceled() && !tercero.isCanceled());
    QCOMPARE(e.lector.recorridos(), 1); // el segundo trabajo ve el indice vigente

    // Detenido: nada nuevo se acepta.
    e.indexador->detener();
    QVERIFY(e.indexador->detenido());
    auto tarde = e.indexador->sincronizarSolicitud(s1);
    QVERIFY(tarde.isCanceled());
    QVERIFY(!e.indexador->ocupado());
}

void TestIndiceCfdi::indexadorDetenidoCancelaPaqueteEnCurso()
{
    EntornoIndexador e;
    for (int i = 0; i < 5; ++i) {
        e.interno.agregarXml(QStringLiteral("I|U-%1|N").arg(i).toUtf8());
    }
    const SolicitudId s1 = SolicitudId::generar();
    e.catalogo.agregarPaquete(paquete(QStringLiteral("a0"), QStringLiteral("r/a0.zip"), s1));
    e.catalogo.agregarPaquete(paquete(QStringLiteral("a1"), QStringLiteral("r/a1.zip"), s1));
    e.archivos.fijar(QStringLiteral("r/a0.zip"), {});
    e.archivos.fijar(QStringLiteral("r/a1.zip"), {});
    QSignalSpy cambios(e.indexador.get(), &IndexadorCfdi::indiceCambiado);
    e.lector.cerrar();
    auto f = e.indexador->sincronizarSolicitud(s1);
    QVERIFY(e.lector.esperarBloqueo());
    e.indexador->detener(); // salida: cancela entre entradas
    e.lector.liberar();
    QTRY_VERIFY(f.isFinished());
    QVERIFY(f.isCanceled());
    QTRY_VERIFY(!e.indexador->ocupado());
    QTest::qWait(20);
    QCOMPARE(e.catalogo.escrituras(), 0); // ni el paquete en curso ni el siguiente
    QVERIFY(!e.catalogo.indice(QStringLiteral("a0")));
    QCOMPARE(cambios.count(), 0);
}

void TestIndiceCfdi::logsSinDatosFiscales()
{
    QLoggingCategory::setFilterRules(QStringLiteral("satcfdi.cfdi.*=true"));
    {
        QMutexLocker b(&g_mutexLogs);
        g_logs.clear();
    }
    const auto anterior = qInstallMessageHandler(capturarLog);
    {
        EntornoIndexador e;
        e.interno.agregarXml("I|UUID-SECRETO|NOMBRE SECRETO");
        e.interno.agregarOmision(MotivoOmision::RutaInvalida);
        const SolicitudId s1 = SolicitudId::generar();
        const QString idPaquete = QStringLiteral("0f0f0f0f-1111-4222-8333-444444444444");
        e.catalogo.agregarPaquete(paquete(idPaquete, QStringLiteral("RFCSECRETO/2026-01/x/p.zip"), s1));
        e.archivos.fijar(QStringLiteral("RFCSECRETO/2026-01/x/p.zip"), {});
        const SolicitudId s2 = SolicitudId::generar();
        e.catalogo.agregarPaquete(paquete(QStringLiteral("aaaaaaaa-1111-4222-8333-444444444444"),
                                          QStringLiteral("RFCSECRETO/2026-01/y/q.zip"), s2)); // no existe
        e.indexador->iniciar();
        QTRY_VERIFY(!e.indexador->ocupado());
        QCOMPARE(e.catalogo.escrituras(), 2);
    }
    qInstallMessageHandler(anterior);
    QLoggingCategory::setFilterRules(QString());

    QMutexLocker b(&g_mutexLogs);
    QVERIFY(!g_logs.isEmpty());
    bool vioConteos = false;
    for (const QString& linea : std::as_const(g_logs)) {
        QVERIFY2(!linea.contains(QStringLiteral("SECRETO")), qPrintable(linea));
        QVERIFY2(!linea.contains(QStringLiteral("XAXX")), qPrintable(linea));
        QVERIFY2(!linea.contains(QStringLiteral("444444444444")), qPrintable(linea)); // Id completo
        QVERIFY2(!linea.contains(QStringLiteral(".zip")), qPrintable(linea));
        vioConteos = vioConteos || (linea.contains(QStringLiteral("0f0f0f0f...")) && linea.contains(QStringLiteral("cfdi: 1")));
    }
    QVERIFY(vioConteos);
}

void TestIndiceCfdi::indexadorAgrupaPorSolicitudSinDependerDelOrden()
{
    EntornoIndexador e;
    e.interno.agregarXml("I|U-1|Uno");
    const SolicitudId s1 = SolicitudId::generar();
    const SolicitudId s2 = SolicitudId::generar();
    // El catalogo devuelve los paquetes intercalados: s1, s2, s1, s2.
    const QList<SolicitudId> orden{s1, s2, s1, s2};
    for (qsizetype i = 0; i < orden.size(); ++i) {
        const QString ruta = QStringLiteral("r/i%1.zip").arg(i);
        e.catalogo.agregarPaquete(paquete(QStringLiteral("i%1").arg(i), ruta, orden.at(i)));
        e.archivos.fijar(ruta, {});
    }
    QSignalSpy cambios(e.indexador.get(), &IndexadorCfdi::indiceCambiado);
    e.indexador->iniciar();
    QTRY_VERIFY(!e.indexador->ocupado());
    QCOMPARE(e.catalogo.escrituras(), 4);
    // Exactamente una senal por solicitud.
    QCOMPARE(cambios.count(), 2);
    QSet<QString> emitidas;
    for (const QList<QVariant>& args : std::as_const(cambios)) {
        emitidas.insert(args.at(0).value<SolicitudId>().texto());
    }
    QCOMPARE(emitidas, (QSet<QString>{s1.texto(), s2.texto()}));
}

void TestIndiceCfdi::calculoTerminadoTrasDetenerNoSeEscribe()
{
    EntornoIndexador e;
    const SolicitudId s1 = SolicitudId::generar();
    // ZIP ausente: el calculo no vuelve a consultar la cancelacion despues de
    // consultar() y termina con Reemplazar(OrigenNoDisponible).
    e.catalogo.agregarPaquete(paquete(QStringLiteral("a0"), QStringLiteral("r/a0.zip"), s1));
    QSignalSpy cambios(e.indexador.get(), &IndexadorCfdi::indiceCambiado);
    e.archivos.cerrarConsultas();
    auto f = e.indexador->sincronizarSolicitud(s1);
    QVERIFY(e.archivos.esperarConsultaBloqueada());
    e.indexador->detener();      // salida mientras el calculo esta en curso
    e.archivos.abrirConsultas(); // el calculo termina DESPUES de detener
    QTRY_VERIFY(f.isFinished());
    QVERIFY(f.isCanceled());
    QTRY_VERIFY(!e.indexador->ocupado());
    QTest::qWait(50);
    QCOMPARE(e.catalogo.escrituras(), 0);
    QVERIFY(!e.catalogo.indice(QStringLiteral("a0")));
    QCOMPARE(cambios.count(), 0);
}

void TestIndiceCfdi::logsDeErrorDePersistenciaSinMensaje()
{
    {
        QMutexLocker b(&g_mutexLogs);
        g_logs.clear();
    }
    const auto anterior = qInstallMessageHandler(capturarLog);
    {
        EntornoIndexador e;
        const SolicitudId s1 = SolicitudId::generar();
        e.catalogo.agregarPaquete(paquete(QStringLiteral("0f0f0f0f-1111-4222-8333-444444444444"),
                                          QStringLiteral("r/a0.zip"), s1));
        // Fallo al escribir el indice del paquete.
        e.catalogo.fallarEscritura(true);
        QVERIFY(e.indexador->sincronizarSolicitud(s1).isValid());
        QTRY_VERIFY(!e.indexador->ocupado());
        // Fallo al leer los paquetes a indexar.
        e.catalogo.fallarLectura(true);
        auto f = e.indexador->sincronizarSolicitud(s1);
        QTRY_VERIFY(f.isFinished());
        QTRY_VERIFY(!e.indexador->ocupado());
        QCOMPARE(e.catalogo.escrituras(), 0);
    }
    qInstallMessageHandler(anterior);

    QMutexLocker b(&g_mutexLogs);
    bool vioEscritura = false;
    bool vioLectura = false;
    for (const QString& linea : std::as_const(g_logs)) {
        QVERIFY2(!linea.contains(QStringLiteral("SECRET")), qPrintable(linea));
        QVERIFY2(!linea.contains(QStringLiteral("XAXX")), qPrintable(linea));
        QVERIFY2(!linea.contains(QStringLiteral("/Users")), qPrintable(linea));
        QVERIFY2(!linea.contains(QStringLiteral("444444444444")), qPrintable(linea));
        vioEscritura = vioEscritura
                       || (linea.contains(QStringLiteral("no se pudo escribir el indice del paquete 0f0f0f0f..."))
                           && linea.contains(QStringLiteral("error: integridad")));
        vioLectura = vioLectura
                     || (linea.contains(QStringLiteral("no se pudieron leer los paquetes"))
                         && linea.contains(QStringLiteral("error: integridad")));
    }
    QVERIFY(vioEscritura);
    QVERIFY(vioLectura);
}
