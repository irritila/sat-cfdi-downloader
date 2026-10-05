#include "TestCredencialesSat.h"

#include "FakesPersistencia.h"
#include "fakes/FakeSecretStore.h"

#include "application/persistence/PersistenceDispatcher.h"
#include "application/profiles/CredencialesSatServicePersistido.h"
#include "domain/common/UuidCanonico.h"

#include <QByteArray>
#include <QMutex>
#include <QSet>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QTest>
#include <QTimeZone>

#include <atomic>
#include <memory>
#include <type_traits>

using namespace satcfdi;
using fakes::Almacen;
using fakes::FakeSecretStore;
using namespace Qt::StringLiterals;
using Cat = ErrorSecretStore::Categoria;

// --- Comprobaciones de compilacion (DA1) -----------------------------------

template <typename T>
constexpr bool kMoveOnly = !std::is_copy_constructible_v<T> && !std::is_copy_assignable_v<T>
                           && std::is_nothrow_move_constructible_v<T>;

static_assert(kMoveOnly<BufferSecreto>);
static_assert(kMoveOnly<MaterialFirma>);
static_assert(kMoveOnly<CredencialPreparada>);
static_assert(!std::is_copy_constructible_v<EntradaEFirma> && std::is_move_constructible_v<EntradaEFirma>);
static_assert(!std::is_convertible_v<BufferSecreto, QByteArray>);
static_assert(!std::is_convertible_v<BufferSecreto, QString>);
static_assert(!std::is_constructible_v<QByteArray, MaterialFirma>);
static_assert(!std::is_constructible_v<QString, MaterialFirma>);
static_assert(!std::is_convertible_v<MaterialFirma, QVariant>);
static_assert(!std::is_constructible_v<QVariant, BufferSecreto>);
static_assert(std::is_copy_constructible_v<CredencialRef>);

namespace {

const QDateTime kAhora = QDateTime(QDate(2026, 10, 3), QTime(12, 0), QTimeZone(QTimeZone::UTC));
const QString kRfc = QStringLiteral("AAA010101AAA");
const QString kRutaCer = QStringLiteral("/Users/alguien/Documentos/efirma/aaa.cer");
const QString kRutaKey = QStringLiteral("/Users/alguien/Documentos/efirma/aaa.key");

// Grafo de prueba: fakes, dispatcher y servicio (orden de destruccion
// inverso).
struct Entorno {
    Almacen almacen;
    fakes::FakePerfiles perfiles{almacen};
    fakes::FakeCredenciales credenciales{almacen};
    fakes::FakeUnitOfWork uow{almacen};
    FakeSecretStore store;
    QMutex mutexRegistro;
    QStringList registro;
    QDateTime ahora = kAhora;
    PersistenceDispatcher dispatcher;
    CredencialesSatServicePersistido servicio{
        dispatcher, perfiles, credenciales, uow, store, [this] { return ahora; },
        [this](QStringView op, Cat c) {
            QMutexLocker l(&mutexRegistro);
            registro.append(op.toString() + u':' + claveEstable(c));
        }};

    PerfilId perfil = PerfilId::generar();
    PerfilId inactivo = PerfilId::generar();

    Entorno()
    {
        almacen.perfiles.append(PerfilSat{perfil, kRfc, QStringLiteral("Activo"), true, kAhora, kAhora,
                                          std::nullopt});
        almacen.perfiles.append(PerfilSat{inactivo, QStringLiteral("BBB020202BBB"),
                                          QStringLiteral("Inactivo"), false, kAhora, kAhora, std::nullopt});
        store.rfcCertificado = kRfc;
    }

    EntradaEFirma entrada(const QByteArray& contrasena = QByteArrayLiteral("clave-de-prueba")) const
    {
        EntradaEFirma e;
        e.rutaCertificado = kRutaCer;
        e.rutaLlavePrivada = kRutaKey;
        e.contrasena = BufferSecreto::desdeBytes(contrasena.constData(),
                                                 static_cast<std::size_t>(contrasena.size()));
        return e;
    }

    std::optional<CredencialSat> fila(const PerfilId& id) const
    {
        for (const CredencialSat& c : almacen.credenciales) {
            if (c.perfilSatId == id) {
                return c;
            }
        }
        return std::nullopt;
    }
};

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 30000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

CredencialRef refDe(const CredencialSat& c)
{
    return *CredencialRef::desdeReferencias(c.certificadoRef, c.llavePrivadaRef, c.contrasenaRef);
}

// El mensaje visible no expone RFC, rutas ni el texto del diagnostico.
bool mensajeLimpio(const QString& m)
{
    return !m.isEmpty() && !m.contains(kRfc) && !m.contains(u'/') && !m.contains(u"fallo inyectado");
}

} // namespace

// --- Tipos -------------------------------------------------------------------

void TestCredencialesSat::tiposSensiblesSonMoveOnlySinConversion()
{
    // Las aserciones estaticas viven arriba; aqui se ejercita el movimiento.
    MaterialFirma m(BufferSecreto::desdeBytes("c", 1), BufferSecreto::desdeBytes("ll", 2),
                    BufferSecreto::desdeBytes("pwd", 3));
    MaterialFirma movido = std::move(m);
    QCOMPARE(movido.llavePrivadaDer().tamano(), std::size_t(2));
    QVERIFY(m.llavePrivadaDer().vacio()); // NOLINT(bugprone-use-after-move)
    QVERIFY(movido.contrasena().igualA("pwd", 3));
}

void TestCredencialesSat::bufferSecretoMueveYLimpia()
{
    BufferSecreto a = BufferSecreto::desdeBytes("secreto", 7);
    QCOMPARE(a.tamano(), std::size_t(7));
    QVERIFY(a.igualA("secreto", 7));
    QVERIFY(!a.igualA("secretx", 7));
    QVERIFY(!a.igualA("secret", 6));

    BufferSecreto b(std::move(a));
    QVERIFY(a.vacio()); // NOLINT(bugprone-use-after-move)
    QVERIFY(a.datos() == nullptr);
    QVERIFY(b.igualA("secreto", 7));

    BufferSecreto c = BufferSecreto::desdeBytes("x", 1);
    c = std::move(b);
    QVERIFY(c.igualA("secreto", 7));
    c.limpiar();
    QVERIFY(c.vacio());

    // limpiarMemoria sobrescribe con ceros.
    char bloque[4] = {'a', 'b', 'c', 'd'};
    secretos::limpiarMemoria(bloque, sizeof(bloque));
    for (char x : bloque) {
        QCOMPARE(x, '\0');
    }
    QCOMPARE(BufferSecreto(3).tamano(), std::size_t(3));
}

void TestCredencialesSat::bufferSecretoDesdeTextoUtf8()
{
    const QString texto = QStringLiteral("contraseña€") + QString::fromUcs4(U"\U0001F511");
    const QByteArray esperado = texto.toUtf8();
    const BufferSecreto b = BufferSecreto::desdeTexto(texto);
    QVERIFY(b.igualA(esperado.constData(), static_cast<std::size_t>(esperado.size())));
    QVERIFY(BufferSecreto::desdeTexto(u"").vacio());
    // Suplente suelto -> U+FFFD (EF BF BD).
    const QChar suelto(char16_t(0xD800));
    const BufferSecreto s = BufferSecreto::desdeTexto(QStringView(&suelto, 1));
    QVERIFY(s.igualA("\xEF\xBF\xBD", 3));
}

void TestCredencialesSat::credencialRefFormatoYValidacion()
{
    const CredencialRef ref = CredencialRef::generar();
    QVERIFY(!ref.esNula());
    QVERIFY(uuid::esCanonico(ref.uuid()));
    QCOMPARE(ref.referenciaCertificado(), u"scs1:"_s + ref.uuid() + u":cert"_s);
    QCOMPARE(ref.referenciaContenedor(), u"scs1:"_s + ref.uuid() + u":container"_s);
    QCOMPARE(ref.referenciaContrasena(), u"scs1:"_s + ref.uuid() + u":password"_s);
    for (const QString& r : {ref.referenciaCertificado(), ref.referenciaContenedor(),
                             ref.referenciaContrasena()}) {
        QVERIFY(!r.startsWith(u'/'));
        QVERIFY(!r.contains(u'/'));
        QVERIFY(!r.contains(u'\\'));
    }
    QCOMPARE(CredencialRef::desdeReferencias(ref.referenciaCertificado(), ref.referenciaContenedor(),
                                             ref.referenciaContrasena()),
             std::optional<CredencialRef>(ref));

    const CredencialRef otra = CredencialRef::generar();
    // UUID distinto entre roles, roles cruzados, esquema/uuid invalidos.
    QVERIFY(!CredencialRef::desdeReferencias(ref.referenciaCertificado(), otra.referenciaContenedor(),
                                             ref.referenciaContrasena()));
    QVERIFY(!CredencialRef::desdeReferencias(ref.referenciaContenedor(), ref.referenciaCertificado(),
                                             ref.referenciaContrasena()));
    QVERIFY(!CredencialRef::desdeReferencia(u"scs2:" + ref.uuid() + u":cert", RolReferencia::Certificado));
    QVERIFY(!CredencialRef::desdeReferencia(u"scs1:" + ref.uuid().toUpper() + u":cert",
                                            RolReferencia::Certificado));
    QVERIFY(!CredencialRef::desdeReferencia(u"/tmp/x.cer", RolReferencia::Certificado));
    QVERIFY(!CredencialRef::desdeReferencia(u"", RolReferencia::Certificado));
    QVERIFY(!CredencialRef::desdeUuid(u"no-es-uuid"));
    QVERIFY(CredencialRef().referenciaCertificado().isEmpty());
}

void TestCredencialesSat::credencialPreparadaDescartaSinConfirmar()
{
    int descartes = 0;
    auto descartador = [&](const CredencialRef&) { ++descartes; };
    {
        CredencialPreparada p(CredencialRef::generar(), {}, descartador);
    }
    QCOMPARE(descartes, 1);
    {
        CredencialPreparada p(CredencialRef::generar(), {}, descartador);
        p.confirmar();
        QVERIFY(p.confirmada());
    }
    QCOMPARE(descartes, 1);
    {
        CredencialPreparada a(CredencialRef::generar(), {}, descartador);
        CredencialPreparada b(std::move(a)); // el origen ya no descarta
        QVERIFY(a.vacia()); // NOLINT(bugprone-use-after-move)
    }
    QCOMPARE(descartes, 2);
    {
        CredencialPreparada a(CredencialRef::generar(), {}, descartador);
        CredencialPreparada b(CredencialRef::generar(), {}, descartador);
        a = std::move(b); // descarta la generacion previa de `a`
        QCOMPARE(descartes, 3);
        a.descartar();
        a.descartar(); // idempotente
        QCOMPARE(descartes, 4);
    }
    QCOMPARE(descartes, 4);
    {
        CredencialPreparada p(CredencialRef::generar(), {},
                              [](const CredencialRef&) { throw 1; }); // suprimida
    }
}

void TestCredencialesSat::catalogosCerrados()
{
    QSet<QString> claves;
    for (Cat c : kCategoriasErrorSecretStore) {
        claves.insert(claveEstable(c));
        QVERIFY(!mensajeVisible(c).isEmpty());
    }
    QCOMPARE(claves.size(), 17);
    QSet<QString> estados;
    for (EstadoCredencial e : kEstadosCredencial) {
        estados.insert(claveEstable(e));
    }
    QCOMPARE(estados.size(), 7);
    QVERIFY(!claves.contains(QString()) && !estados.contains(QString()));
}

// --- Servicio -------------------------------------------------------------

void TestCredencialesSat::importarValidoDejaListaYUnaGeneracion()
{
    Entorno e;
    QSignalSpy spy(&e.servicio, &CredencialesSatService::credencialCambio);
    // Barrera: la importacion se detiene en prepararEFirma mientras se
    // observa Validando (sin ella, la tarea puede terminar antes de que
    // importar() encadene su continuacion y el estado ya seria Lista).
    QSemaphore entro;
    QSemaphore seguir;
    e.store.alEntrar = [&](FakeSecretStore::Operacion op) {
        if (op == FakeSecretStore::Operacion::Preparar) {
            entro.release();
            seguir.acquire();
        }
    };
    struct Liberar {
        QSemaphore& s;
        ~Liberar() { s.release(); } // nunca deja bloqueado el dispatcher
    } liberar{seguir};
    auto f = e.servicio.importar(e.perfil, e.entrada());
    QVERIFY(entro.tryAcquire(1, 5000));
    // Mientras corre: Validando.
    const auto validando = esperar(e.servicio.obtenerEstado(e.perfil));
    QVERIFY(validando && validando->esExito());
    QCOMPARE(validando->valor(), EstadoCredencial::Validando);
    QVERIFY(!f.isFinished());
    seguir.release(); // alEntrar no se reasigna: el hilo del dispatcher puede estar dentro

    const auto r = esperar(f);
    QVERIFY(r && r->esExito());
    QCOMPARE(r->valor().estado, EstadoCredencial::Lista);
    QVERIFY(!r->valor().limpiezaPendiente);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), e.perfil.texto());

    const auto fila = e.fila(e.perfil);
    QVERIFY(fila);
    QVERIFY(uuid::esCanonico(fila->id));
    QVERIFY(fila->metadataCompleta());
    QCOMPARE(fila->registradaEn, kAhora);
    QVERIFY(e.store.existe(refDe(*fila)));
    QCOMPARE(e.store.generacionesVivas(), 1);
    QCOMPARE(e.store.descartes(), 0);
    // Sin rutas ni contrasena en la fila.
    for (const QString& v : {fila->certificadoRef, fila->llavePrivadaRef, fila->contrasenaRef}) {
        QVERIFY(!v.contains(u'/'));
        QVERIFY(!v.contains(u"clave-de-prueba"));
    }

    const auto estado = esperar(e.servicio.obtenerEstado(e.perfil));
    QVERIFY(estado && estado->esExito());
    QCOMPARE(estado->valor(), EstadoCredencial::Lista);
    QVERIFY(e.registro.isEmpty());
}

void TestCredencialesSat::importarErrorDelAlmacenPorCategoria()
{
    // Solo las categorias que el contrato permite a prepararEFirma.
    const QList<Cat> permitidas = FakeSecretStore::categoriasPermitidas(FakeSecretStore::Operacion::Preparar);
    QCOMPARE(permitidas.size(), 15);
    for (Cat c : permitidas) {
        Entorno e;
        QSignalSpy spy(&e.servicio, &CredencialesSatService::credencialCambio);
        e.store.fallos.insert(FakeSecretStore::Operacion::Preparar, c);
        const auto r = esperar(e.servicio.importar(e.perfil, e.entrada()));
        QVERIFY(r && !r->esExito());
        QCOMPARE(r->error().tipo, ErrorCredencialSat::Tipo::Almacen);
        QCOMPARE(*r->error().categoria, c);
        QVERIFY2(mensajeLimpio(r->error().mensaje), qPrintable(claveEstable(c)));
        QVERIFY(e.almacen.credenciales.isEmpty());
        QCOMPARE(e.store.generacionesVivas(), 0);
        QCOMPARE(spy.count(), 0);
        QCOMPARE(e.registro, QStringList{u"credencial.preparar:"_s + claveEstable(c)});
        // Sin transaccion abierta.
        QVERIFY(!e.almacen.eventos.contains(u"begin"_s));
    }
}

void TestCredencialesSat::importarErroresDeValidacionDelFake()
{
    struct Caso {
        const char* nombre;
        std::function<void(Entorno&)> preparar;
        QByteArray contrasena;
        Cat esperada;
    };
    const QList<Caso> casos{
        {"contrasena", [](Entorno&) {}, QByteArrayLiteral("otra"), Cat::ContrasenaIncorrecta},
        {"rfc", [](Entorno& e) { e.store.rfcCertificado = u"ZZZ010101ZZZ"_s; },
         QByteArrayLiteral("clave-de-prueba"), Cat::RfcNoCoincide},
        {"vencida", [](Entorno& e) { e.ahora = QDateTime(QDate(2030, 1, 1), QTime(0, 0), QTimeZone::UTC); },
         QByteArrayLiteral("clave-de-prueba"), Cat::Vencida},
        {"no vigente", [](Entorno& e) { e.ahora = QDateTime(QDate(2024, 1, 1), QTime(0, 0), QTimeZone::UTC); },
         QByteArrayLiteral("clave-de-prueba"), Cat::NoVigenteAun},
        {"limite notAfter", [](Entorno& e) { e.ahora = e.store.vigenteHasta; },
         QByteArrayLiteral("clave-de-prueba"), Cat::Vencida},
    };
    for (const Caso& caso : casos) {
        Entorno e;
        caso.preparar(e);
        const auto r = esperar(e.servicio.importar(e.perfil, e.entrada(caso.contrasena)));
        QVERIFY2(r && !r->esExito(), caso.nombre);
        QCOMPARE(*r->error().categoria, caso.esperada);
        QVERIFY(mensajeLimpio(r->error().mensaje));
        QVERIFY(e.almacen.credenciales.isEmpty());
        QCOMPARE(e.store.generacionesVivas(), 0);
    }
    // notBefore incluido: exactamente vigenteDesde es valida.
    Entorno e;
    e.ahora = e.store.vigenteDesde;
    const auto r = esperar(e.servicio.importar(e.perfil, e.entrada()));
    QVERIFY(r && r->esExito());
}

void TestCredencialesSat::importarPerfilInexistenteOInactivo()
{
    Entorno e;
    const auto inexistente = esperar(e.servicio.importar(PerfilId::generar(), e.entrada()));
    QVERIFY(inexistente && !inexistente->esExito());
    QCOMPARE(inexistente->error().tipo, ErrorCredencialSat::Tipo::PerfilInvalido);
    QCOMPARE(*inexistente->error().codigoPerfil, ErrorCredencialSat::CodigoPerfil::PerfilInexistente);

    const auto nulo = esperar(e.servicio.importar(PerfilId(), e.entrada()));
    QVERIFY(nulo && !nulo->esExito());
    QCOMPARE(*nulo->error().codigoPerfil, ErrorCredencialSat::CodigoPerfil::PerfilInexistente);

    const auto inactivo = esperar(e.servicio.importar(e.inactivo, e.entrada()));
    QVERIFY(inactivo && !inactivo->esExito());
    QCOMPARE(*inactivo->error().codigoPerfil, ErrorCredencialSat::CodigoPerfil::PerfilInactivo);
    QCOMPARE(e.store.preparadas(), 0);
}

void TestCredencialesSat::importarConCredencialExistenteFalla()
{
    Entorno e;
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    const auto segunda = esperar(e.servicio.importar(e.perfil, e.entrada()));
    QVERIFY(segunda && !segunda->esExito());
    QCOMPARE(segunda->error().tipo, ErrorCredencialSat::Tipo::CredencialExistente);
    QCOMPARE(e.store.preparadas(), 1);
    QCOMPARE(e.store.generacionesVivas(), 1);
}

void TestCredencialesSat::importarFalloDeCommitDescartaCandidata()
{
    Entorno e;
    e.almacen.fallos.insert(u"commit"_s, fakes::error(ErrorPersistencia::Tipo::Ocupado));
    const auto r = esperar(e.servicio.importar(e.perfil, e.entrada()));
    QVERIFY(r && !r->esExito());
    QCOMPARE(r->error().tipo, ErrorCredencialSat::Tipo::Persistencia);
    QCOMPARE(r->error().causa->tipo, ErrorPersistencia::Tipo::Ocupado);
    QVERIFY(e.almacen.credenciales.isEmpty());
    QCOMPARE(e.store.generacionesVivas(), 0);
    QCOMPARE(e.store.descartes(), 1);
    QVERIFY(e.almacen.eventos.contains(u"rollback"_s));
}

void TestCredencialesSat::reemplazoFallidoEnPreparacionConservaAnterior()
{
    Entorno e;
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    const CredencialSat antes = *e.fila(e.perfil);

    for (Cat c : {Cat::ContrasenaIncorrecta, Cat::AlmacenBloqueado, Cat::FalloEscritura}) {
        e.store.fallos.insert(FakeSecretStore::Operacion::Preparar, c);
        const auto r = esperar(e.servicio.reemplazar(e.perfil, e.entrada()));
        QVERIFY(r && !r->esExito());
        QCOMPARE(*r->error().categoria, c);
        QCOMPARE(*e.fila(e.perfil), antes);
        QCOMPARE(e.store.generacionesVivas(), 1);
        QVERIFY(e.store.existe(refDe(antes)));
    }
    e.store.fallos.clear();
    const auto estado = esperar(e.servicio.obtenerEstado(e.perfil));
    QCOMPARE(estado->valor(), EstadoCredencial::Lista);
}

void TestCredencialesSat::reemplazoFallidoEnEscrituraOCommitConservaAnterior()
{
    for (const char* op : {"begin", "reemplazarCredencial", "commit"}) {
        Entorno e;
        QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
        const CredencialSat antes = *e.fila(e.perfil);
        QSignalSpy spy(&e.servicio, &CredencialesSatService::credencialCambio);

        e.almacen.fallos.insert(QString::fromLatin1(op), fakes::error(ErrorPersistencia::Tipo::Almacenamiento));
        const auto r = esperar(e.servicio.reemplazar(e.perfil, e.entrada()));
        QVERIFY2(r && !r->esExito(), op);
        QCOMPARE(r->error().tipo, ErrorCredencialSat::Tipo::Persistencia);
        QCOMPARE(*e.fila(e.perfil), antes);
        QCOMPARE(e.store.generacionesVivas(), 1);
        QVERIFY(e.store.existe(refDe(antes)));
        QCOMPARE(e.store.descartes(), 1);
        QVERIFY(e.store.eliminadas().isEmpty());
        QCOMPARE(spy.count(), 0);
    }
}

void TestCredencialesSat::reemplazoValidoDejaUnaGeneracion()
{
    Entorno e;
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    const CredencialSat antes = *e.fila(e.perfil);
    QSignalSpy spy(&e.servicio, &CredencialesSatService::credencialCambio);

    e.ahora = kAhora.addDays(1);
    const auto r = esperar(e.servicio.reemplazar(e.perfil, e.entrada()));
    QVERIFY(r && r->esExito());
    QVERIFY(!r->valor().limpiezaPendiente);
    QCOMPARE(spy.count(), 1);

    const CredencialSat despues = *e.fila(e.perfil);
    QCOMPARE(e.almacen.credenciales.size(), 1);
    QCOMPARE(despues.id, antes.id);                   // misma fila
    QCOMPARE(despues.registradaEn, antes.registradaEn);
    QCOMPARE(despues.actualizadaEn, kAhora.addDays(1));
    QVERIFY(!(refDe(despues) == refDe(antes)));
    QCOMPARE(e.store.generacionesVivas(), 1);
    QVERIFY(e.store.existe(refDe(despues)));
    QCOMPARE(e.store.eliminadas(), QList<CredencialRef>{refDe(antes)});
}

void TestCredencialesSat::reemplazoConLimpiezaFallidaNoRevierteYReconciliaDespues()
{
    Entorno e;
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    const CredencialSat antes = *e.fila(e.perfil);

    e.store.fallos.insert(FakeSecretStore::Operacion::Eliminar, Cat::AlmacenBloqueado);
    const auto r = esperar(e.servicio.reemplazar(e.perfil, e.entrada()));
    QVERIFY(r && r->esExito());
    QVERIFY(r->valor().limpiezaPendiente);
    const CredencialSat despues = *e.fila(e.perfil);
    QVERIFY(!(refDe(despues) == refDe(antes)));
    QCOMPARE(e.store.generacionesVivas(), 2); // residuo de la anterior
    QCOMPARE(e.registro, QStringList{u"credencial.limpiar_anterior:AlmacenBloqueado"_s});

    // Siguiente arranque: la reconciliacion borra solo la huerfana.
    e.store.fallos.clear();
    const auto rec = esperar(e.servicio.reconciliar());
    QVERIFY(rec && rec->esExito());
    QCOMPARE(rec->valor().generacionesEliminadas, 1);
    QCOMPARE(rec->valor().generacionesConservadas, 1);
    QCOMPARE(e.store.uuidsVivos(), QStringList{refDe(despues).uuid()});
}

void TestCredencialesSat::reemplazarSinCredencialFalla()
{
    Entorno e;
    const auto r = esperar(e.servicio.reemplazar(e.perfil, e.entrada()));
    QVERIFY(r && !r->esExito());
    QCOMPARE(*r->error().categoria, Cat::CredencialNoEncontrada);
    QCOMPARE(e.store.preparadas(), 0);
    const auto inactivo = esperar(e.servicio.reemplazar(e.inactivo, e.entrada()));
    QCOMPARE(*inactivo->error().codigoPerfil, ErrorCredencialSat::CodigoPerfil::PerfilInactivo);
}

void TestCredencialesSat::obtenerEstadoSinCredencialListaYVencida()
{
    Entorno e;
    QCOMPARE(esperar(e.servicio.obtenerEstado(e.perfil))->valor(), EstadoCredencial::SinCredencial);
    // Perfil inactivo tambien consulta estado.
    QCOMPARE(esperar(e.servicio.obtenerEstado(e.inactivo))->valor(), EstadoCredencial::SinCredencial);
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    e.ahora = e.store.vigenteHasta.addMSecs(1);
    QCOMPARE(esperar(e.servicio.obtenerEstado(e.perfil))->valor(), EstadoCredencial::Vencida);

    const CredencialRef ref = refDe(*e.fila(e.perfil));
    e.store.marcarDanada(ref);
    QCOMPARE(esperar(e.servicio.obtenerEstado(e.perfil))->valor(), EstadoCredencial::MaterialDanado);
    e.store.quitar(ref);
    QCOMPARE(esperar(e.servicio.obtenerEstado(e.perfil))->valor(), EstadoCredencial::MaterialFaltante);

    e.store.fallos.insert(FakeSecretStore::Operacion::Estado, Cat::AlmacenNoDisponible);
    const auto err = esperar(e.servicio.obtenerEstado(e.perfil));
    QCOMPARE(*err->error().categoria, Cat::AlmacenNoDisponible);
    const auto inexistente = esperar(e.servicio.obtenerEstado(PerfilId::generar()));
    QCOMPARE(inexistente->error().tipo, ErrorCredencialSat::Tipo::PerfilInvalido);
}

void TestCredencialesSat::eliminarBorraFilaYGeneracion()
{
    Entorno e;
    const auto vacio = esperar(e.servicio.eliminar(e.perfil));
    QVERIFY(vacio && vacio->esExito() && !vacio->valor().habiaCredencial);

    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    QSignalSpy spy(&e.servicio, &CredencialesSatService::credencialCambio);
    const auto r = esperar(e.servicio.eliminar(e.perfil));
    QVERIFY(r && r->esExito());
    QVERIFY(r->valor().habiaCredencial);
    QVERIFY(!r->valor().limpiezaPendiente);
    QVERIFY(e.almacen.credenciales.isEmpty());
    QCOMPARE(e.store.generacionesVivas(), 0);
    QCOMPARE(spy.count(), 1);

    // Limpieza fallida: la fila se borra igual y queda para reconciliar.
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    e.store.fallos.insert(FakeSecretStore::Operacion::Eliminar, Cat::AccesoDenegado);
    const auto pendiente = esperar(e.servicio.eliminar(e.perfil));
    QVERIFY(pendiente && pendiente->esExito() && pendiente->valor().limpiezaPendiente);
    QVERIFY(e.almacen.credenciales.isEmpty());
    e.store.fallos.clear();
    QCOMPARE(esperar(e.servicio.reconciliar())->valor().generacionesEliminadas, 1);
    QCOMPARE(e.store.generacionesVivas(), 0);

    // Fallo de commit: la fila sigue.
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    e.almacen.fallos.insert(u"commit"_s, fakes::error(ErrorPersistencia::Tipo::Ocupado));
    const auto fallo = esperar(e.servicio.eliminar(e.perfil));
    QVERIFY(fallo && !fallo->esExito());
    QVERIFY(e.fila(e.perfil));
    QCOMPARE(e.store.generacionesVivas(), 1);
}

void TestCredencialesSat::obtenerMaterialFirmaEnHiloDeTrabajo()
{
    Entorno e;
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    CredencialesSatServicePersistido* servicio = &e.servicio;
    const PerfilId perfil = e.perfil;
    // El material vive y muere dentro de la tarea; solo sale un booleano.
    const auto ok = esperar(e.dispatcher.despachar<bool>([servicio, perfil]() {
        auto material = servicio->obtenerMaterialFirma(perfil);
        if (!material.esExito()) {
            return false;
        }
        MaterialFirma m = std::move(material).valor();
        return !m.certificadoDer().vacio() && !m.llavePrivadaDer().vacio()
               && m.contrasena().igualA("clave-de-prueba", 15);
    }));
    QVERIFY(ok && *ok);

    const PerfilId sin = PerfilId::generar();
    e.almacen.perfiles.append(PerfilSat{sin, u"CCC030303CCC"_s, u"Sin"_s, true, kAhora, kAhora, std::nullopt});
    const auto categoria = esperar(e.dispatcher.despachar<int>([servicio, sin]() {
        auto r = servicio->obtenerMaterialFirma(sin);
        return r.esExito() ? -1 : static_cast<int>(*r.error().categoria);
    }));
    QCOMPARE(*categoria, static_cast<int>(Cat::CredencialNoEncontrada));

    e.store.fallos.insert(FakeSecretStore::Operacion::Material, Cat::AlmacenBloqueado);
    const auto bloqueado = esperar(e.dispatcher.despachar<int>([servicio, perfil]() {
        auto r = servicio->obtenerMaterialFirma(perfil);
        return r.esExito() ? -1 : static_cast<int>(*r.error().categoria);
    }));
    QCOMPARE(*bloqueado, static_cast<int>(Cat::AlmacenBloqueado));
}

void TestCredencialesSat::reconciliarConListaIlegibleNoBorra()
{
    Entorno e;
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    e.store.sembrar(); // huerfana
    QCOMPARE(e.store.generacionesVivas(), 2);

    // Fila con referencia ilegible: no hay lista confiable -> no se borra nada.
    e.almacen.credenciales.first().contrasenaRef = u"scs1:roto:password"_s;
    const auto r = esperar(e.servicio.reconciliar());
    QVERIFY(r && !r->esExito());
    QCOMPARE(r->error().tipo, ErrorCredencialSat::Tipo::Persistencia);
    QCOMPARE(e.store.reconciliaciones(), 0);
    QCOMPARE(e.store.generacionesVivas(), 2);

    // Error del almacen: tampoco borra.
    e.almacen.credenciales.clear();
    e.store.fallos.insert(FakeSecretStore::Operacion::Reconciliar, Cat::AlmacenBloqueado);
    const auto bloqueado = esperar(e.servicio.reconciliar());
    QCOMPARE(*bloqueado->error().categoria, Cat::AlmacenBloqueado);
    QCOMPARE(e.store.generacionesVivas(), 2);
}

void TestCredencialesSat::fakeSoloAceptaCategoriasDelContrato()
{
    using Op = FakeSecretStore::Operacion;
    QCOMPARE(FakeSecretStore::categoriasPermitidas(Op::Estado).size(), 5);
    QCOMPARE(FakeSecretStore::categoriasPermitidas(Op::Reconciliar).size(), 5);
    QCOMPARE(FakeSecretStore::categoriasPermitidas(Op::Material).size(), 7);
    QCOMPARE(FakeSecretStore::categoriasPermitidas(Op::Eliminar).size(), 6);
    QVERIFY(!FakeSecretStore::categoriasPermitidas(Op::Preparar).contains(Cat::CredencialNoEncontrada));
    QVERIFY(!FakeSecretStore::categoriasPermitidas(Op::Estado).contains(Cat::ContrasenaIncorrecta));
    QVERIFY(FakeSecretStore::categoriasPermitidas(Op::Eliminar).contains(Cat::FalloEscritura));

    // Limpieza parcial en reconciliacion: exito con fallosLimpieza.
    Entorno e;
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    const CredencialRef terca = e.store.sembrar();
    e.store.sembrar();
    e.store.noBorrables.insert(terca.uuid());
    const auto r = esperar(e.servicio.reconciliar());
    QVERIFY(r && r->esExito());
    QCOMPARE(r->valor(), (ResumenReconciliacion{1, 1, 1}));
    QVERIFY(e.store.existe(terca));
    QCOMPARE(e.registro, QStringList{u"credencial.reconciliar:FalloEscritura"_s});
}

void TestCredencialesSat::obtenerMaterialFirmaDesdeHiloGraficoFalla()
{
    Entorno e;
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    const qsizetype eventosAntes = e.almacen.eventos.size();
    auto r = e.servicio.obtenerMaterialFirma(e.perfil); // hilo grafico
    QVERIFY(!r.esExito());
    QCOMPARE(r.error().tipo, ErrorCredencialSat::Tipo::HiloNoPermitido);
    QVERIFY(!r.error().mensaje.isEmpty());
    QCOMPARE(e.almacen.eventos.size(), eventosAntes); // no toco repositorios
    QVERIFY(e.store.materialesEntregados().isEmpty());
}

void TestCredencialesSat::materialConcurrenteEsperaAReemplazoEnCurso()
{
    // reemplazar se detiene DESPUES del commit, al limpiar la anterior; un
    // obtenerMaterialFirma desde otro hilo espera y ve la nueva completa.
    Entorno e;
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    QSemaphore entro;
    QSemaphore seguir;
    e.store.alEntrar = [&](FakeSecretStore::Operacion op) {
        if (op == FakeSecretStore::Operacion::Eliminar) {
            entro.release();
            seguir.acquire();
        }
    };
    auto reemplazo = e.servicio.reemplazar(e.perfil, e.entrada());
    QVERIFY(entro.tryAcquire(1, 5000));

    std::atomic<bool> terminado{false};
    bool exito = false;
    std::unique_ptr<QThread> hilo(QThread::create([&] {
        exito = e.servicio.obtenerMaterialFirma(e.perfil).esExito();
        terminado = true;
    }));
    hilo->start();
    QTest::qWait(100);
    QVERIFY(!terminado); // bloqueado por la exclusion del servicio
    QVERIFY(e.store.materialesEntregados().isEmpty());

    seguir.release();
    QVERIFY(hilo->wait(5000));
    QVERIFY(exito);
    const auto r = esperar(reemplazo);
    QVERIFY(r && r->esExito() && !r->valor().limpiezaPendiente);
    const CredencialRef nueva = refDe(*e.fila(e.perfil));
    QCOMPARE(e.store.materialesEntregados(), QList<CredencialRef>{nueva});
    QCOMPARE(e.store.generacionesVivas(), 1);
}

void TestCredencialesSat::reemplazoEsperaAMaterialEnCurso()
{
    // obtenerMaterialFirma ya leyo la fila y se detiene en el SecretStore; un
    // reemplazo encolado no puede borrar la generacion anterior bajo sus pies.
    Entorno e;
    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    const CredencialRef anterior = refDe(*e.fila(e.perfil));
    QSemaphore entro;
    QSemaphore seguir;
    e.store.alEntrar = [&](FakeSecretStore::Operacion op) {
        if (op == FakeSecretStore::Operacion::Material) {
            entro.release();
            seguir.acquire();
        }
    };
    bool exito = false;
    std::unique_ptr<QThread> hilo(QThread::create([&] {
        exito = e.servicio.obtenerMaterialFirma(e.perfil).esExito();
    }));
    hilo->start();
    QVERIFY(entro.tryAcquire(1, 5000));

    auto reemplazo = e.servicio.reemplazar(e.perfil, e.entrada());
    QTest::qWait(100);
    QVERIFY(!reemplazo.isFinished());
    QCOMPARE(e.store.preparadas(), 1); // el reemplazo no empezo
    QVERIFY(e.store.existe(anterior));

    seguir.release();
    QVERIFY(hilo->wait(5000));
    QVERIFY(exito);
    QCOMPARE(e.store.materialesEntregados(), QList<CredencialRef>{anterior});
    const auto r = esperar(reemplazo);
    QVERIFY(r && r->esExito());
    QVERIFY(!e.store.existe(anterior));
    QCOMPARE(e.store.generacionesVivas(), 1);
}

void TestCredencialesSat::reconciliarTerminaAntesDePrepararEncolado()
{
    Entorno e;
    QMutex mutexOrden;
    QStringList orden;
    QSemaphore entro;
    QSemaphore seguir;
    e.store.alEntrar = [&](FakeSecretStore::Operacion op) {
        {
            QMutexLocker l(&mutexOrden);
            orden.append(op == FakeSecretStore::Operacion::Reconciliar ? u"reconciliar"_s
                         : op == FakeSecretStore::Operacion::Preparar  ? u"preparar"_s
                                                                       : u"otra"_s);
        }
        if (op == FakeSecretStore::Operacion::Reconciliar) {
            entro.release();
            seguir.acquire();
        }
    };
    auto reconciliacion = e.servicio.reconciliar();
    auto importacion = e.servicio.importar(e.perfil, e.entrada());
    QVERIFY(entro.tryAcquire(1, 5000));
    QTest::qWait(100);
    QCOMPARE(e.store.preparadas(), 0);
    {
        QMutexLocker l(&mutexOrden);
        QCOMPARE(orden, QStringList{u"reconciliar"_s});
    }
    seguir.release();
    QVERIFY(esperar(reconciliacion)->esExito());
    QVERIFY(esperar(importacion)->esExito());
    QMutexLocker l(&mutexOrden);
    QCOMPARE(orden, (QStringList{u"reconciliar"_s, u"preparar"_s}));
}

// --- T005.1 ------------------------------------------------------------------

void TestCredencialesSat::origenDeErrorNormalizado()
{
    using O = OrigenErrorEFirma;
    QCOMPARE(ErrorCredencialSat::almacen(Cat::ContrasenaIncorrecta).origen, O::Contrasena);
    QCOMPARE(ErrorCredencialSat::almacen(Cat::ContrasenaIncorrecta, O::Llave).origen, O::Contrasena);
    QCOMPARE(ErrorCredencialSat::almacen(Cat::FormatoInvalido, O::Certificado).origen, O::Certificado);
    QCOMPARE(ErrorCredencialSat::almacen(Cat::ArchivoIlegible, O::Llave).origen, O::Llave);
    QCOMPARE(ErrorCredencialSat::almacen(Cat::FormatoInvalido, O::Contrasena).origen, O::Ninguno);
    QCOMPARE(ErrorCredencialSat::almacen(Cat::FormatoInvalido).origen, O::Ninguno);
    for (Cat c : kCategoriasErrorSecretStore) {
        if (c != Cat::ContrasenaIncorrecta && c != Cat::FormatoInvalido && c != Cat::ArchivoIlegible) {
            QCOMPARE(ErrorCredencialSat::almacen(c, O::Llave).origen, O::Ninguno);
        }
    }
    QCOMPARE(ErrorCredencialSat::existente().origen, O::Ninguno);
    QCOMPARE(ErrorCredencialSat::perfil(ErrorCredencialSat::CodigoPerfil::PerfilInactivo).origen, O::Ninguno);
    QCOMPARE(ErrorCredencialSat::hiloNoPermitido().origen, O::Ninguno);
    QSet<QString> claves;
    for (O o : {O::Ninguno, O::Certificado, O::Llave, O::Contrasena}) {
        claves.insert(claveEstable(o));
    }
    QCOMPARE(claves.size(), 4);
}

void TestCredencialesSat::importarPropagaOrigenDelAlmacen()
{
    {
        Entorno e; // validaciones propias del fake
        const auto pwd = esperar(e.servicio.importar(e.perfil, e.entrada(QByteArrayLiteral("mala"))));
        QCOMPARE(pwd->error().origen, OrigenErrorEFirma::Contrasena);
        EntradaEFirma sinCer = e.entrada();
        sinCer.rutaCertificado.clear();
        QCOMPARE(esperar(e.servicio.importar(e.perfil, std::move(sinCer)))->error().origen,
                 OrigenErrorEFirma::Certificado);
        EntradaEFirma sinKey = e.entrada();
        sinKey.rutaLlavePrivada.clear();
        QCOMPARE(esperar(e.servicio.importar(e.perfil, std::move(sinKey)))->error().origen, OrigenErrorEFirma::Llave);
    }
    {
        Entorno e; // fallo inyectado con origen configurado
        e.store.fallos.insert(FakeSecretStore::Operacion::Preparar, Cat::FormatoInvalido);
        e.store.origenFalloPreparar = OrigenErrorEFirma::Llave;
        const auto r = esperar(e.servicio.reemplazar(e.perfil, e.entrada()));
        QVERIFY(r && !r->esExito()); // sin credencial previa: no llega al store
        const auto imp = esperar(e.servicio.importar(e.perfil, e.entrada()));
        QCOMPARE(*imp->error().categoria, Cat::FormatoInvalido);
        QCOMPARE(imp->error().origen, OrigenErrorEFirma::Llave);
        // Categoria sin campo: el servicio descarta el origen.
        e.store.fallos.insert(FakeSecretStore::Operacion::Preparar, Cat::AlmacenBloqueado);
        QCOMPARE(esperar(e.servicio.importar(e.perfil, e.entrada()))->error().origen, OrigenErrorEFirma::Ninguno);
    }
}

void TestCredencialesSat::obtenerResumenIncluyeVigenciaSinDescifrar()
{
    Entorno e;
    const auto sin = esperar(e.servicio.obtenerResumen(e.perfil));
    QVERIFY(sin && sin->esExito());
    QCOMPARE(sin->valor().estado, EstadoCredencial::SinCredencial);
    QVERIFY(!sin->valor().vigenteHasta);

    QVERIFY(esperar(e.servicio.importar(e.perfil, e.entrada()))->esExito());
    const auto lista = esperar(e.servicio.obtenerResumen(e.perfil));
    QVERIFY(lista && lista->esExito());
    QCOMPARE(lista->valor().estado, EstadoCredencial::Lista);
    QCOMPARE(lista->valor().vigenteDesde, std::optional<QDateTime>(e.store.vigenteDesde));
    QCOMPARE(lista->valor().vigenteHasta, std::optional<QDateTime>(e.store.vigenteHasta));
    QVERIFY(e.store.materialesEntregados().isEmpty()); // no descifro material

    // Fila previa a 002: estado sin vigencia.
    e.almacen.credenciales.first().vigenteHasta.reset();
    e.almacen.credenciales.first().vigenteDesde.reset();
    QVERIFY(!esperar(e.servicio.obtenerResumen(e.perfil))->valor().vigenteHasta);

    e.store.fallos.insert(FakeSecretStore::Operacion::Estado, Cat::AlmacenBloqueado);
    const auto bloqueado = esperar(e.servicio.obtenerResumen(e.perfil));
    QCOMPARE(*bloqueado->error().categoria, Cat::AlmacenBloqueado);
    QCOMPARE(*esperar(e.servicio.obtenerEstado(e.perfil))->error().categoria, Cat::AlmacenBloqueado);
}
