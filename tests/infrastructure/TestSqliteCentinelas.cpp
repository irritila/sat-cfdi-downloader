#include "TestSqliteCentinelas.h"

#include "SqlitePruebasComun.h"

#include "application/logging/RegexLogSanitizer.h"
#include "domain/logs/LogSolicitud.h"
#include "domain/perfiles/PerfilSat.h"
#include "domain/solicitudes/SolicitudCanonica.h"
#include "domain/solicitudes/SolicitudPersistida.h"
#include "ports/repositories/LogSolicitudRepository.h"
#include "ports/repositories/PerfilSatRepository.h"
#include "ports/repositories/SolicitudMasivaRepository.h"
#include "ports/repositories/UnitOfWork.h"

#include <QFileInfo>
#include <QTest>

#include <memory>

using namespace satcfdi;
using namespace satcfdi::pruebas;

namespace {

const QByteArray kControl = "CONTROL_VISIBLE_7731";

// Secretos centinela: cada uno solo aparece en texto que el sanitizer debe
// redactar.
const QList<QByteArray> kSecretos = {
    "CENTINELAPWD9f8e7d6c",
    "CENTINELATOKENa1b2c3",
    "CENTINELABEARERz9y8x7",
    "CENTINELAPEMLLAVE4455",
    "CENTINELAFIRMA6677",
    "CENTINELAPAQUETE8899",
    // base64 huerfano (>= 64 caracteres, no hex).
    "Q0VOVElORUxBQkFTRTY0U0VDUkVUT1FVRU5PREVCRUFQQVJFQ0VSRU5MQUJBU0VERURBVE9T",
};

struct Archivos {
    QByteArray principal;
    QByteArray wal;
    QByteArray shm;
};

Archivos leer(const QString& ruta)
{
    return {leerArchivo(ruta), leerArchivo(ruta + QStringLiteral("-wal")),
            leerArchivo(ruta + QStringLiteral("-shm"))};
}

QString secretoEncontrado(const Archivos& a)
{
    for (const QByteArray& s : kSecretos) {
        if (a.principal.contains(s)) {
            return QStringLiteral(".sqlite3 contiene ") + QString::fromLatin1(s);
        }
        if (a.wal.contains(s)) {
            return QStringLiteral("-wal contiene ") + QString::fromLatin1(s);
        }
        if (a.shm.contains(s)) {
            return QStringLiteral("-shm contiene ") + QString::fromLatin1(s);
        }
    }
    return {};
}

} // namespace

void TestSqliteCentinelas::secretosNoLleganAArchivosSqlite()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString ruta = rutaBase(dir);
    QVERIFY(inicializarBaseSqlite(ruta));

    auto persistencia = std::make_unique<SqlitePersistencia>(ruta);
    const RegexLogSanitizer sanitizer;

    NuevoPerfilSat perfil;
    perfil.id = PerfilId::generar();
    perfil.rfc = kRfcPerfil;
    perfil.nombre = QStringLiteral("Perfil centinela");
    perfil.creadoEn = timestamp::ahoraUtc();
    perfil.actualizadoEn = perfil.creadoEn;

    EntradaSolicitudCanonica entrada;
    entrada.rfcPerfil = kRfcPerfil;
    entrada.fechaInicial = QDate(2026, 1, 1);
    entrada.fechaFinal = QDate(2026, 1, 31);
    auto canonica = SolicitudCanonica::normalizar(entrada);
    QVERIFY(canonica);
    const SolicitudNuevaPersistida solicitud{SolicitudId::generar(), perfil.id, canonica.valor(),
                                             timestamp::ahoraUtc()};

    LogEntradaCruda cruda;
    cruda.id = uuid::generarCanonico();
    cruda.solicitudId = solicitud.id;
    cruda.tipoEvento = TipoEventoLog::SolicitudCreada;
    cruda.origen = OrigenLog::Usuario;
    cruda.creadoEn = timestamp::ahoraUtc();
    cruda.origenCodigoSat = OrigenCodigoSat::Creacion;
    cruda.codigoSat = QStringLiteral("5000");
    cruda.mensajeSat = QStringLiteral("Authorization: Bearer %1").arg(QString::fromLatin1(kSecretos[2]));
    cruda.dedupKey = canonica.valor().dedupKey();
    cruda.mensaje = QStringLiteral("%1 password=%2 token: %3")
                        .arg(QString::fromLatin1(kControl), QString::fromLatin1(kSecretos[0]),
                             QString::fromLatin1(kSecretos[1]));
    cruda.detalle = QStringLiteral("-----BEGIN PRIVATE KEY-----\n%1\n-----END PRIVATE KEY-----\n"
                                   "<SignatureValue>%2</SignatureValue>\n"
                                   "<Paquete>%3</Paquete>\nblob %4")
                        .arg(QString::fromLatin1(kSecretos[3]), QString::fromLatin1(kSecretos[4]),
                             QString::fromLatin1(kSecretos[5]), QString::fromLatin1(kSecretos[6]));
    const LogEntradaSaneada saneada = sanitizer.sanitizar(cruda);
    QVERIFY(saneada.payloadResumenJson.has_value());
    QVERIFY(saneada.payloadResumenJson->contains(QStringLiteral("[REDACTED:")));

    UnitOfWork& uow = persistencia->unidadDeTrabajo();
    QVERIFY(uow.begin());
    QVERIFY(persistencia->perfiles().insertar(perfil));
    QVERIFY(persistencia->solicitudes().insertarCreada(solicitud));
    auto agregado = persistencia->logs().agregar(saneada);
    QVERIFY2(agregado, agregado ? "" : qPrintable(agregado.error().mensaje));
    QVERIFY(uow.commit());
    // Releer mantiene la conexion abierta con -wal/-shm activos.
    auto logs = persistencia->logs().listarVisiblesPorSolicitud(solicitud.id);
    QVERIFY(logs);
    QCOMPARE(logs.valor().size(), 1);

    // Antes del cierre: con WAL activo existen -wal y -shm.
    QVERIFY(QFileInfo::exists(ruta + QStringLiteral("-wal")));
    QVERIFY(QFileInfo::exists(ruta + QStringLiteral("-shm")));
    const Archivos abiertos = leer(ruta);
    QVERIFY((abiertos.principal + abiertos.wal).contains(kControl));
    const QString fugaAbierta = secretoEncontrado(abiertos);
    QVERIFY2(fugaAbierta.isEmpty(), qPrintable(fugaAbierta));

    // Despues del cierre: -wal/-shm pueden no existir (o quedar vacios); el
    // archivo principal queda limpio y contiene los datos.
    persistencia->cerrarConexionDelHiloActual();
    persistencia.reset();
    const Archivos cerrados = leer(ruta);
    QVERIFY(cerrados.wal.isEmpty());
    QVERIFY(cerrados.principal.contains(kControl));
    const QString fugaCerrada = secretoEncontrado(cerrados);
    QVERIFY2(fugaCerrada.isEmpty(), qPrintable(fugaCerrada));
}
