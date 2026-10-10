// satcfdi_perfiles_vm_tests (T005.1): Qt Test de PerfilesSatListModel,
// PerfilesSatViewModel, EFirmaFormViewModel y la navegacion a Perfiles, con
// los fakes de tests/fakes (promesas manuales, sin Keychain). Incluye
// centinelas: ni rutas ni contrasena en propiedades, roles ni senales
// expuestos a QML. Cualquier warning hace fallar la prueba.

#include "ServiciosAsincronosFake.h"
#include "fakes/FakeCredencialesSatService.h"
#include "fakes/FakePerfilesSatService.h"

#include "AppViewModel.h"
#include "EFirmaFormViewModel.h"
#include "NuevaSolicitudViewModel.h"
#include "PerfilesSatListModel.h"
#include "PerfilesSatViewModel.h"
#include "PresentacionViewModels.h"

#include <QCoreApplication>
#include <QMetaMethod>
#include <QMetaProperty>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

using namespace satcfdi;
using fakes::FakeCredencialesSatService;
using fakes::FakePerfilesSatService;
using Categoria = ErrorSecretStore::Categoria;
using EstadoLista = PerfilesSatViewModel::EstadoLista;
using Modo = PerfilesSatViewModel::Modo;
using Fase = EFirmaFormViewModel::Fase;
using ListaPerfiles = PerfilesSatService::ResultadoLista;

namespace {

// Centinelas: ninguno puede aparecer en lo expuesto a QML.
const QString kDirectorio = QStringLiteral("/private/tmp/CENTINELA_DIR_satcfdi");
const QString kRutaCer = kDirectorio + QStringLiteral("/efirma_CENTINELA.cer");
const QString kRutaKey = kDirectorio + QStringLiteral("/efirma_CENTINELA.key");
const QString kContrasena = QStringLiteral("CENTINELA-contrasena-7319");

void procesarEventos()
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
}

// Grafo completo de presentacion sobre fakes (como el composition root).
struct Grafo {
    FakePerfilesSatService perfiles;
    FakeCredencialesSatService credenciales;
    SolicitudesServiceAsincrono solicitudes;
    PresentacionViewModels vms{&solicitudes, &perfiles, &credenciales};

    PerfilesSatViewModel* p() const { return vms.perfiles(); }
    EFirmaFormViewModel* f() const { return vms.eFirma(); }

    // Resuelve la carga `indice` y espera las verificaciones que dispara.
    bool cargarCon(int indice, const QList<PerfilResumen>& lista)
    {
        const int base = credenciales.resumenes.size();
        perfiles.listas.resolver(indice, ListaPerfiles::exito(lista));
        return QTest::qWaitFor([&] { return credenciales.resumenes.size() == base + lista.size(); });
    }
};

// Valores de texto de todas las propiedades de un QObject.
QStringList valoresDePropiedades(const QObject* objeto)
{
    QStringList valores;
    const QMetaObject* mo = objeto->metaObject();
    for (int i = 0; i < mo->propertyCount(); ++i) {
        valores.append(mo->property(i).read(objeto).toString());
    }
    return valores;
}

QStringList nombresDePropiedades(const QObject* objeto)
{
    QStringList nombres;
    const QMetaObject* mo = objeto->metaObject();
    for (int i = mo->propertyOffset(); i < mo->propertyCount(); ++i) {
        nombres.append(QString::fromLatin1(mo->property(i).name()));
    }
    return nombres;
}

// Ninguna propiedad contiene centinelas; ningun nombre sugiere rutas/secretos.
void verificarSinSecretos(const QObject* objeto)
{
    const QRegularExpression nombreSensible(QStringLiteral("ruta|path|password|contrasena|secret|llavePrivada"),
                                            QRegularExpression::CaseInsensitiveOption);
    for (const QString& nombre : nombresDePropiedades(objeto)) {
        QVERIFY2(!nombreSensible.match(nombre).hasMatch(), qPrintable(nombre));
    }
    for (const QString& valor : valoresDePropiedades(objeto)) {
        QVERIFY2(!valor.contains(QStringLiteral("CENTINELA_DIR")), qPrintable(valor));
        QVERIFY2(!valor.contains(kContrasena), qPrintable(valor));
    }
    // Firmas de senales: sin parametros sensibles por nombre. (Solo enviar(),
    // invocable y sincrono, recibe la contrasena por diseno.)
    const QMetaObject* mo = objeto->metaObject();
    for (int i = mo->methodOffset(); i < mo->methodCount(); ++i) {
        const QMetaMethod m = mo->method(i);
        if (m.methodType() != QMetaMethod::Signal) {
            continue;
        }
        for (const QByteArray& parametro : m.parameterNames()) {
            QVERIFY2(!nombreSensible.match(QString::fromLatin1(parametro)).hasMatch(), m.methodSignature().constData());
        }
    }
}

} // namespace

class TestPerfilesViewModels : public QObject {
    Q_OBJECT

private slots:
    void init();

    void rolesExactosSinSecretos();
    void listaCargandoErrorVaciaConDatos();
    void verificacionYEstadoNoDisponibleConReintento();
    void listaDescartaRespuestasTardias();
    void crearPerfilYDobleEnvio();
    void rfcDuplicadoConservaFormularioYEnfocaRfc();
    void validacionPorCampo();
    void edicionSoloCambiaNombre();
    void cambioDePerfilDescartaGuardadoTardio();
    void inactivoNoDisponibleParaSolicitudes();

    void eFirmaBasenamesSinRutasNiContrasena();
    void eFirmaDobleEnvioYExito();
    void eFirmaErroresPorOrigen_data();
    void eFirmaErroresPorOrigen();
    void eFirmaValidacionLocal();
    void reemplazoValidoActualizaEstado();
    void reemplazoFallidoConservaEstadoReal();
    void eFirmaCambioDePerfilDescartaRespuestaTardia();

    void navegacionPerfilesYCentinelas();
};

void TestPerfilesViewModels::init()
{
    QTest::failOnWarning(QRegularExpression(QStringLiteral(".*")));
}

void TestPerfilesViewModels::rolesExactosSinSecretos()
{
    PerfilesSatListModel modelo;
    QStringList roles;
    for (const QByteArray& r : modelo.roleNames()) {
        roles.append(QString::fromLatin1(r));
    }
    roles.sort();
    QStringList esperados = {
        QStringLiteral("id"),          QStringLiteral("rfc"),
        QStringLiteral("nombre"),      QStringLiteral("activo"),
        QStringLiteral("preparacion"), QStringLiteral("listoParaSolicitudes"),
        QStringLiteral("verificando"), QStringLiteral("estadoTexto"),
        QStringLiteral("vigenteHasta"), QStringLiteral("diasParaVencer"), // T014.3 D2
    };
    esperados.sort();
    QCOMPARE(roles, esperados);

    // listoParaSolicitudes viene de aplicacion: activo && Lista.
    const PerfilResumen activo = FakePerfilesSatService::perfil("AAA010101AAA", "Activo");
    const PerfilResumen inactivo = FakePerfilesSatService::perfil("BBB010101BBB", "Inactivo", false);
    const QDateTime vigencia(QDate(2027, 1, 31), QTime(23, 59), QTimeZone::UTC);
    PerfilConPreparacion porVencer = PerfilConPreparacion::componer(activo, PreparacionPerfil::Lista, vigencia);
    porVencer.diasParaVencer = 7;
    modelo.reemplazar({porVencer, PerfilConPreparacion::componer(inactivo, PreparacionPerfil::Lista)});
    const QModelIndex i0 = modelo.index(0);
    const QModelIndex i1 = modelo.index(1);
    QCOMPARE(i0.data(PerfilesSatListModel::ListoParaSolicitudesRole).toBool(), true);
    QCOMPARE(i1.data(PerfilesSatListModel::ListoParaSolicitudesRole).toBool(), false);
    QCOMPARE(i0.data(PerfilesSatListModel::PreparacionRole).toString(), QStringLiteral("Lista"));
    QCOMPARE(i0.data(PerfilesSatListModel::VigenteHastaRole).toDateTime(), vigencia);
    QVERIFY(i1.data(PerfilesSatListModel::VigenteHastaRole).isNull());
    // T014.3 D2: dias para vencer (o -1 sin aviso).
    QCOMPARE(i0.data(PerfilesSatListModel::DiasParaVencerRole).toInt(), 7);
    QCOMPARE(i1.data(PerfilesSatListModel::DiasParaVencerRole).toInt(), -1);
    QVERIFY(!i0.data(PerfilesSatListModel::EstadoTextoRole).toString().isEmpty());
    QCOMPARE(modelo.filaDe(inactivo.id.texto()), 1);
}

void TestPerfilesViewModels::listaCargandoErrorVaciaConDatos()
{
    Grafo g;
    QCOMPARE(g.perfiles.listas.size(), 0); // no carga al construir
    g.p()->cargar();
    QCOMPARE(g.p()->estadoLista(), EstadoLista::Cargando);

    g.perfiles.listas.resolver(0, ListaPerfiles::fallo(ErrorPersistencia::de(
                                      ErrorPersistencia::Tipo::Almacenamiento, QStringLiteral("x"))));
    QTRY_COMPARE(g.p()->estadoLista(), EstadoLista::Error);
    QVERIFY(!g.p()->errorListaMessage().isEmpty());
    QVERIFY(g.p()->errorListaMessage() != QStringLiteral("x"));

    g.p()->cargar(); // reintento
    QCOMPARE(g.p()->estadoLista(), EstadoLista::Cargando);
    QVERIFY(g.cargarCon(1, {}));
    QTRY_COMPARE(g.p()->estadoLista(), EstadoLista::Vacia);

    // perfilesCambiaron recarga; con datos no regresa a Cargando.
    const PerfilResumen perfil = FakePerfilesSatService::perfil("AAA010101AAA", "Uno");
    g.perfiles.emitirCambio();
    QCOMPARE(g.perfiles.listas.size(), 3);
    QVERIFY(g.cargarCon(2, {perfil}));
    QTRY_COMPARE(g.p()->estadoLista(), EstadoLista::ConDatos);
    g.perfiles.emitirCambio();
    QVERIFY(g.p()->cargando());
    QCOMPARE(g.p()->estadoLista(), EstadoLista::ConDatos);
}

void TestPerfilesViewModels::verificacionYEstadoNoDisponibleConReintento()
{
    Grafo g;
    const PerfilResumen a = FakePerfilesSatService::perfil("AAA010101AAA", "A");
    const PerfilResumen b = FakePerfilesSatService::perfil("BBB010101BBB", "B");
    g.p()->cargar();
    QVERIFY(g.cargarCon(0, {a, b}));
    PerfilesSatListModel* m = g.p()->perfiles();

    // Primero se publican en Verificando.
    QCOMPARE(m->count(), 2);
    QCOMPARE(m->index(0).data(PerfilesSatListModel::PreparacionRole).toString(), QStringLiteral("Verificando"));
    QVERIFY(m->index(0).data(PerfilesSatListModel::VerificandoRole).toBool());

    g.credenciales.resolverResumen(0, EstadoCredencial::Lista);
    g.credenciales.fallarResumen(1, Categoria::AlmacenBloqueado);
    QTRY_COMPARE(m->index(0).data(PerfilesSatListModel::PreparacionRole).toString(), QStringLiteral("Lista"));
    QTRY_COMPARE(m->index(1).data(PerfilesSatListModel::PreparacionRole).toString(),
                 QStringLiteral("EstadoNoDisponible"));
    QCOMPARE(m->index(1).data(PerfilesSatListModel::EstadoTextoRole).toString(),
             QStringLiteral("Estado no disponible"));
    QVERIFY(m->index(0).data(PerfilesSatListModel::ListoParaSolicitudesRole).toBool());

    // Reintento por perfil: solo ese perfil vuelve a consultarse.
    g.p()->reintentarEstado(b.id.texto());
    QCOMPARE(g.credenciales.resumenes.size(), 3);
    QCOMPARE(g.credenciales.idsResumen.constLast(), b.id);
    QCOMPARE(m->index(1).data(PerfilesSatListModel::PreparacionRole).toString(), QStringLiteral("Verificando"));
    g.credenciales.resolverResumen(2, EstadoCredencial::SinCredencial);
    QTRY_COMPARE(m->index(1).data(PerfilesSatListModel::PreparacionRole).toString(),
                 QStringLiteral("SinCredencial"));
    QCOMPARE(m->index(0).data(PerfilesSatListModel::PreparacionRole).toString(), QStringLiteral("Lista"));
}

void TestPerfilesViewModels::listaDescartaRespuestasTardias()
{
    Grafo g;
    const PerfilResumen viejo = FakePerfilesSatService::perfil("OLD010101AAA", "Viejo");
    const PerfilResumen nuevo = FakePerfilesSatService::perfil("NEW010101AAA", "Nuevo");
    g.p()->cargar();
    QVERIFY(g.cargarCon(0, {viejo})); // verificacion 0 pendiente
    g.p()->cargar();
    QVERIFY(g.cargarCon(1, {nuevo})); // verificacion 1
    PerfilesSatListModel* m = g.p()->perfiles();
    QCOMPARE(m->filaDe(nuevo.id.texto()), 0);

    // La verificacion de la lista reemplazada llega tarde: no aplica.
    g.credenciales.resolverResumen(0, EstadoCredencial::Lista);
    procesarEventos();
    QCOMPARE(m->count(), 1);
    QCOMPARE(m->index(0).data(PerfilesSatListModel::PreparacionRole).toString(), QStringLiteral("Verificando"));

    // Dos reintentos del mismo perfil: la respuesta del primero se descarta.
    g.p()->reintentarEstado(nuevo.id.texto()); // verificacion 2
    g.credenciales.resolverResumen(2, EstadoCredencial::Vencida);
    QTRY_COMPARE(m->index(0).data(PerfilesSatListModel::PreparacionRole).toString(), QStringLiteral("Vencida"));
    g.credenciales.resolverResumen(1, EstadoCredencial::Lista);
    procesarEventos();
    QCOMPARE(m->index(0).data(PerfilesSatListModel::PreparacionRole).toString(), QStringLiteral("Vencida"));

    // Una carga anterior que responde despues de la vigente se descarta.
    g.p()->cargar(); // 2
    g.p()->cargar(); // 3
    QVERIFY(g.cargarCon(3, {nuevo}));
    g.perfiles.listas.resolver(2, ListaPerfiles::exito({viejo, nuevo}));
    procesarEventos();
    QCOMPARE(m->count(), 1);
}

void TestPerfilesViewModels::crearPerfilYDobleEnvio()
{
    Grafo g;
    PerfilesSatViewModel* p = g.p();
    QSignalSpy guardado(p, &PerfilesSatViewModel::guardado);
    p->nuevo();
    QCOMPARE(p->modo(), Modo::Nuevo);
    QVERIFY(p->rfcEditable());
    QVERIFY(!p->puedeGuardar());
    p->setRfc(QStringLiteral(" aaa010101aaa "));
    p->setNombre(QStringLiteral("Contribuyente"));
    QVERIFY(p->sucio());
    QVERIFY(p->puedeGuardar());

    p->guardar();
    p->guardar(); // doble envio: ignorado
    QCOMPARE(g.perfiles.llamadasCrear.size(), 1);
    QCOMPARE(g.perfiles.llamadasCrear.constFirst().rfc, QStringLiteral(" aaa010101aaa "));
    QVERIFY(p->guardando());
    QVERIFY(!p->puedeGuardar());

    // El servicio normaliza; el perfil queda en edicion, SinCredencial al verificar.
    PerfilResumen creado = FakePerfilesSatService::perfil("AAA010101AAA", "Contribuyente");
    g.perfiles.creaciones.resolver(0, PerfilesSatService::ResultadoCrear::exito(creado));
    QTRY_COMPARE(guardado.count(), 1);
    QCOMPARE(p->modo(), Modo::Edicion);
    QCOMPARE(p->perfilId(), creado.id.texto());
    QCOMPARE(p->rfc(), QStringLiteral("AAA010101AAA"));
    QVERIFY(!p->rfcEditable());
    QVERIFY(!p->sucio());
    QVERIFY(!p->guardando());

    p->cargar();
    QVERIFY(g.cargarCon(0, {creado}));
    g.credenciales.resolverResumen(0, EstadoCredencial::SinCredencial);
    QTRY_COMPARE(p->seleccionPreparacion(), QStringLiteral("SinCredencial"));
    QVERIFY(!p->seleccionListo()); // no se ofrece para solicitudes
    QVERIFY(!p->tieneCredencial());
    QVERIFY(p->puedeGestionarEFirma()); // Registrar e.firma
}

void TestPerfilesViewModels::rfcDuplicadoConservaFormularioYEnfocaRfc()
{
    Grafo g;
    PerfilesSatViewModel* p = g.p();
    QSignalSpy enfocar(p, &PerfilesSatViewModel::enfocarCampo);
    p->nuevo();
    p->setRfc(QStringLiteral("AAA010101AAA"));
    p->setNombre(QStringLiteral("Otro"));
    p->guardar();
    g.perfiles.creaciones.resolver(0, PerfilesSatService::ResultadoCrear::fallo(FakePerfilesSatService::rfcDuplicado()));
    QTRY_COMPARE(p->errorKey(), QStringLiteral("RfcDuplicado"));
    QCOMPARE(p->campoConError(), QStringLiteral("rfc"));
    QVERIFY(!p->errorRfc().isEmpty());
    QVERIFY(p->errorNombre().isEmpty());
    QCOMPARE(enfocar.count(), 1);
    QCOMPARE(enfocar.at(0).at(0).toString(), QStringLiteral("rfc"));
    // Formulario conservado.
    QCOMPARE(p->modo(), Modo::Nuevo);
    QCOMPARE(p->rfc(), QStringLiteral("AAA010101AAA"));
    QCOMPARE(p->nombre(), QStringLiteral("Otro"));
    QVERIFY(!p->guardando());
    QVERIFY(!p->errorMessage().contains(QStringLiteral("ux_perfil")));

    // Editar el RFC limpia el error del campo.
    p->setRfc(QStringLiteral("AAA010101AAB"));
    QVERIFY(p->errorRfc().isEmpty());
    QVERIFY(p->campoConError().isEmpty());
}

void TestPerfilesViewModels::validacionPorCampo()
{
    Grafo g;
    PerfilesSatViewModel* p = g.p();
    QSignalSpy enfocar(p, &PerfilesSatViewModel::enfocarCampo);
    p->nuevo();
    p->setRfc(QStringLiteral("X"));
    p->setNombre(QStringLiteral(" "));
    p->setNombre(QStringLiteral("N"));
    p->guardar();
    g.perfiles.creaciones.resolver(0, PerfilesSatService::ResultadoCrear::fallo(FakePerfilesSatService::validacion(
                                          {CodigoValidacionPerfil::NombreRequerido, CodigoValidacionPerfil::RfcInvalido})));
    QTRY_VERIFY(!p->errorRfc().isEmpty());
    QVERIFY(!p->errorNombre().isEmpty());
    QCOMPARE(p->campoConError(), QStringLiteral("rfc")); // el primero en el formulario
    QCOMPARE(p->errorKey(), QStringLiteral("RfcInvalido"));
    QCOMPARE(enfocar.count(), 1);

    // Persistencia: error general sin campo.
    p->setRfc(QStringLiteral("AAA010101AAA"));
    p->guardar();
    g.perfiles.creaciones.resolver(1, PerfilesSatService::ResultadoCrear::fallo(FakePerfilesSatService::errorPersistencia()));
    QTRY_COMPARE(p->errorKey(), QStringLiteral("Persistencia"));
    QVERIFY(p->campoConError().isEmpty());
    QVERIFY(!p->errorMessage().isEmpty());
}

void TestPerfilesViewModels::edicionSoloCambiaNombre()
{
    Grafo g;
    PerfilesSatViewModel* p = g.p();
    const PerfilResumen perfil = FakePerfilesSatService::perfil("AAA010101AAA", "Original");
    p->cargar();
    QVERIFY(g.cargarCon(0, {perfil}));
    QVERIFY(!p->seleccionar(QStringLiteral("no-es-un-id")));
    QVERIFY(p->seleccionar(perfil.id.texto()));
    QCOMPARE(p->modo(), Modo::Edicion);
    QVERIFY(!p->rfcEditable());
    p->setRfc(QStringLiteral("ZZZ010101ZZZ")); // ignorado: identidad inmutable
    QCOMPARE(p->rfc(), QStringLiteral("AAA010101AAA"));

    p->setNombre(QStringLiteral("Renombrado"));
    QVERIFY(p->sucio());
    QVERIFY(!p->puedeGestionarEFirma()); // guardar antes de gestionar e.firma
    p->guardar();
    QCOMPARE(g.perfiles.llamadasActualizar.size(), 1);
    QCOMPARE(g.perfiles.llamadasActualizar.constFirst().id, perfil.id);
    QCOMPARE(g.perfiles.llamadasActualizar.constFirst().nombre, QStringLiteral("Renombrado"));
    QVERIFY(g.perfiles.llamadasCrear.isEmpty());
    PerfilResumen actualizado = perfil;
    actualizado.nombre = QStringLiteral("Renombrado");
    g.perfiles.actualizaciones.resolver(0, PerfilesSatService::ResultadoActualizar::exito(actualizado));
    QTRY_VERIFY(!p->sucio());

    // Descartar restaura el nombre guardado.
    p->setNombre(QStringLiteral("Temporal"));
    p->descartar();
    QCOMPARE(p->nombre(), QStringLiteral("Renombrado"));

    // Perfil inexistente al guardar.
    p->setNombre(QStringLiteral("Otro"));
    p->guardar();
    g.perfiles.actualizaciones.resolver(1, PerfilesSatService::ResultadoActualizar::fallo(FakePerfilesSatService::inexistente()));
    QTRY_COMPARE(p->errorKey(), QStringLiteral("PerfilInexistente"));
}

void TestPerfilesViewModels::cambioDePerfilDescartaGuardadoTardio()
{
    Grafo g;
    PerfilesSatViewModel* p = g.p();
    const PerfilResumen a = FakePerfilesSatService::perfil("AAA010101AAA", "A");
    const PerfilResumen b = FakePerfilesSatService::perfil("BBB010101BBB", "B");
    p->cargar();
    QVERIFY(g.cargarCon(0, {a, b}));
    QSignalSpy guardado(p, &PerfilesSatViewModel::guardado);

    QVERIFY(p->seleccionar(a.id.texto()));
    p->setNombre(QStringLiteral("A2"));
    p->guardar();
    QVERIFY(p->seleccionar(b.id.texto())); // cambia de perfil con el guardado pendiente
    QVERIFY(!p->guardando());
    PerfilResumen a2 = a;
    a2.nombre = QStringLiteral("A2");
    g.perfiles.actualizaciones.resolver(0, PerfilesSatService::ResultadoActualizar::exito(a2));
    procesarEventos();
    QCOMPARE(guardado.count(), 0);
    QCOMPARE(p->perfilId(), b.id.texto());
    QCOMPARE(p->nombre(), QStringLiteral("B"));

    // Mismo caso con error tardio: no contamina el formulario de B.
    QVERIFY(p->seleccionar(a.id.texto()));
    p->setNombre(QStringLiteral("A3"));
    p->guardar();
    p->nuevo();
    g.perfiles.actualizaciones.resolver(1, PerfilesSatService::ResultadoActualizar::fallo(FakePerfilesSatService::inexistente()));
    procesarEventos();
    QVERIFY(p->errorKey().isEmpty());
    QCOMPARE(p->modo(), Modo::Nuevo);
}

void TestPerfilesViewModels::inactivoNoDisponibleParaSolicitudes()
{
    Grafo g;
    const PerfilResumen inactivo = FakePerfilesSatService::perfil("AAA010101AAA", "Inactivo", false);
    g.p()->cargar();
    QVERIFY(g.cargarCon(0, {inactivo}));
    g.credenciales.resolverResumen(0, EstadoCredencial::Lista);
    PerfilesSatListModel* m = g.p()->perfiles();
    QTRY_COMPARE(m->index(0).data(PerfilesSatListModel::PreparacionRole).toString(), QStringLiteral("Lista"));
    // Conserva RFC y estado de credencial, pero no esta listo.
    QCOMPARE(m->index(0).data(PerfilesSatListModel::RfcRole).toString(), QStringLiteral("AAA010101AAA"));
    QCOMPARE(m->index(0).data(PerfilesSatListModel::ActivoRole).toBool(), false);
    QCOMPARE(m->index(0).data(PerfilesSatListModel::ListoParaSolicitudesRole).toBool(), false);
    QVERIFY(g.p()->seleccionar(inactivo.id.texto()));
    QVERIFY(!g.p()->seleccionActiva());
    QVERIFY(!g.p()->seleccionListo());
    QVERIFY(!g.p()->puedeGestionarEFirma()); // importar/reemplazar exigen perfil activo

    // Tampoco lo ofrece el selector de nueva solicitud.
    NuevaSolicitudViewModel* nueva = g.vms.nuevaSolicitud();
    nueva->reiniciar();
    QVERIFY(g.cargarCon(1, {inactivo}));
    g.credenciales.resolverResumen(1, EstadoCredencial::Lista);
    QTRY_VERIFY(nueva->sinPerfiles());
    QCOMPARE(nueva->perfilesDisponibles()->count(), 0);
}

void TestPerfilesViewModels::eFirmaBasenamesSinRutasNiContrasena()
{
    Grafo g;
    EFirmaFormViewModel* f = g.f();
    const PerfilId id = PerfilId::generar();
    f->iniciar(id.texto(), QStringLiteral("AAA010101AAA"), false);
    QVERIFY(!f->puedeEnviar());
    f->seleccionarCertificado(QUrl::fromLocalFile(kRutaCer));
    f->seleccionarLlave(QUrl::fromLocalFile(kRutaKey));
    QCOMPARE(f->nombreCertificado(), QStringLiteral("efirma_CENTINELA.cer"));
    QCOMPARE(f->nombreLlave(), QStringLiteral("efirma_CENTINELA.key"));
    // Registrar exige tambien la contrasena (QML solo informa si la hay).
    QVERIFY(!f->puedeEnviar());
    QCOMPARE(f->faltante(), QStringLiteral("Escribe la contraseña de la llave privada."));
    f->setClaveEscrita(true);
    QVERIFY(f->puedeEnviar());
    QVERIFY(f->faltante().isEmpty());
    verificarSinSecretos(f);

    QSignalSpy cambio(f, &EFirmaFormViewModel::cambio);
    QSignalSpy terminada(f, &EFirmaFormViewModel::operacionTerminada);
    QVERIFY(f->enviar(QUrl(), QUrl(), kContrasena));
    // El servicio recibio rutas y contrasena (sin conservarlas el fake).
    QCOMPARE(g.credenciales.registros.size(), 1);
    QVERIFY(g.credenciales.registros.constFirst().conCertificado);
    QVERIFY(g.credenciales.registros.constFirst().conLlave);
    QCOMPARE(g.credenciales.registros.constFirst().largoContrasena, std::size_t(kContrasena.size()));
    QCOMPARE(g.credenciales.registros.constFirst().perfilId, id);
    verificarSinSecretos(f);

    g.credenciales.importaciones.resolver(
        0, CredencialesSatService::ResultadoImportacion::fallo(
               FakeCredencialesSatService::errorAlmacen(Categoria::ContrasenaIncorrecta, OrigenErrorEFirma::Contrasena)));
    QTRY_COMPARE(f->fase(), Fase::Error);
    verificarSinSecretos(f);
    QVERIFY(!f->errorMessage().contains(QStringLiteral("CENTINELA")));
    // Las senales emitidas no llevan rutas ni contrasena.
    for (const QList<QVariant>& args : std::as_const(terminada)) {
        for (const QVariant& v : args) {
            QVERIFY(!v.toString().contains(QStringLiteral("CENTINELA")));
            QVERIFY(!v.toString().contains(kContrasena));
        }
    }
    QVERIFY(cambio.count() > 0);
    for (const QList<QVariant>& args : std::as_const(cambio)) {
        QVERIFY(args.isEmpty());
    }

    // Una URL no local se rechaza (no es un archivo del equipo).
    f->seleccionarCertificado(QUrl(QStringLiteral("https://example.invalid/a.cer")));
    QVERIFY(f->nombreCertificado().isEmpty());
}

void TestPerfilesViewModels::eFirmaDobleEnvioYExito()
{
    Grafo g;
    EFirmaFormViewModel* f = g.f();
    f->iniciar(PerfilId::generar().texto(), QStringLiteral("AAA010101AAA"), false);
    QVERIFY(f->enviar(QUrl::fromLocalFile(kRutaCer), QUrl::fromLocalFile(kRutaKey), kContrasena));
    QCOMPARE(f->fase(), Fase::Validando);
    QVERIFY(!f->puedeEnviar());
    QVERIFY(!f->enviar(QUrl::fromLocalFile(kRutaCer), QUrl::fromLocalFile(kRutaKey), kContrasena));
    QCOMPARE(g.credenciales.registros.size(), 1); // sin doble envio
    f->cancelar();                                // ignorado durante la validacion
    QCOMPARE(f->fase(), Fase::Validando);

    QSignalSpy terminada(f, &EFirmaFormViewModel::operacionTerminada);
    g.credenciales.importaciones.resolver(
        0, CredencialesSatService::ResultadoImportacion::exito(FakeCredencialesSatService::importada()));
    QTRY_COMPARE(f->fase(), Fase::Exito);
    QCOMPARE(terminada.count(), 1);
    QCOMPARE(terminada.at(0).at(1).toBool(), true);
    QVERIFY(f->errorKey().isEmpty());
}

void TestPerfilesViewModels::eFirmaErroresPorOrigen_data()
{
    QTest::addColumn<int>("categoria");
    QTest::addColumn<int>("origen");
    QTest::addColumn<QString>("campo");
    QTest::addColumn<QString>("clave");

    const auto fila = [](const char* nombre, Categoria c, OrigenErrorEFirma o, const char* campo) {
        QTest::newRow(nombre) << int(c) << int(o) << QString::fromLatin1(campo) << claveEstable(c);
    };
    fila("certificado-ilegible", Categoria::ArchivoIlegible, OrigenErrorEFirma::Certificado, "certificado");
    fila("llave-formato", Categoria::FormatoInvalido, OrigenErrorEFirma::Llave, "llave");
    fila("contrasena", Categoria::ContrasenaIncorrecta, OrigenErrorEFirma::Contrasena, "contrasena");
    fila("pareja", Categoria::ParejaIncompatible, OrigenErrorEFirma::Ninguno, "");
    fila("rfc-distinto", Categoria::RfcNoCoincide, OrigenErrorEFirma::Ninguno, "");
    fila("vencida", Categoria::Vencida, OrigenErrorEFirma::Ninguno, "");
    fila("no-vigente", Categoria::NoVigenteAun, OrigenErrorEFirma::Ninguno, "");
    fila("no-efirma", Categoria::NoEsEFirma, OrigenErrorEFirma::Ninguno, "");
    fila("bloqueado", Categoria::AlmacenBloqueado, OrigenErrorEFirma::Ninguno, "");
    fila("denegado", Categoria::AccesoDenegado, OrigenErrorEFirma::Ninguno, "");
    fila("cancelado", Categoria::CanceladoPorUsuario, OrigenErrorEFirma::Ninguno, "");
    fila("no-disponible", Categoria::AlmacenNoDisponible, OrigenErrorEFirma::Ninguno, "");
}

void TestPerfilesViewModels::eFirmaErroresPorOrigen()
{
    QFETCH(int, categoria);
    QFETCH(int, origen);
    QFETCH(QString, campo);
    QFETCH(QString, clave);

    Grafo g;
    EFirmaFormViewModel* f = g.f();
    f->iniciar(PerfilId::generar().texto(), QStringLiteral("AAA010101AAA"), false);
    QVERIFY(f->enviar(QUrl::fromLocalFile(kRutaCer), QUrl::fromLocalFile(kRutaKey), kContrasena));
    g.credenciales.importaciones.resolver(
        0, CredencialesSatService::ResultadoImportacion::fallo(FakeCredencialesSatService::errorAlmacen(
               Categoria(categoria), OrigenErrorEFirma(origen))));
    QTRY_COMPARE(f->fase(), Fase::Error);
    QCOMPARE(f->errorKey(), clave);
    QCOMPARE(f->campoConError(), campo);
    QVERIFY(!f->errorMessage().isEmpty());
    QVERIFY(!f->errorMessage().contains(QStringLiteral("CENTINELA")));
    QVERIFY(!f->errorMessage().contains(QStringLiteral("AAA010101AAA")));

    // Tras el fallo la seleccion se descarta por completo y se pide de nuevo.
    QVERIFY(f->nombreCertificado().isEmpty());
    QVERIFY(f->nombreLlave().isEmpty());
    QVERIFY(f->requiereNuevaSeleccion());
    QVERIFY(!f->puedeEnviar());
    QCOMPARE(f->faltante(), QStringLiteral("Elige el certificado (.cer)."));
    QCOMPARE(f->errorKey(), clave); // el error especifico se conserva
    QVERIFY(!f->enviar(QUrl(), QUrl(), kContrasena)); // sin rutas retenidas
    QCOMPARE(f->errorKey(), QStringLiteral("CertificadoRequerido"));

    // Reintento con una seleccion nueva.
    f->seleccionarCertificado(QUrl::fromLocalFile(kRutaCer));
    f->seleccionarLlave(QUrl::fromLocalFile(kRutaKey));
    f->setClaveEscrita(true);
    QVERIFY(f->puedeEnviar());
    QVERIFY(f->enviar(QUrl(), QUrl(), kContrasena));
    QCOMPARE(f->fase(), Fase::Validando);
    QVERIFY(!f->requiereNuevaSeleccion());
    QCOMPARE(g.credenciales.importaciones.size(), 2);
}

void TestPerfilesViewModels::eFirmaValidacionLocal()
{
    Grafo g;
    EFirmaFormViewModel* f = g.f();
    QVERIFY(!f->enviar(QUrl(), QUrl(), kContrasena)); // sin perfil
    f->iniciar(PerfilId::generar().texto(), QStringLiteral("AAA010101AAA"), false);
    QVERIFY(!f->enviar(QUrl(), QUrl::fromLocalFile(kRutaKey), kContrasena));
    QCOMPARE(f->campoConError(), QStringLiteral("certificado"));
    f->cancelar(); // descarta la llave ya recibida (URL vacia = seleccion previa)
    QVERIFY(!f->enviar(QUrl::fromLocalFile(kRutaCer), QUrl(), kContrasena));
    QCOMPARE(f->campoConError(), QStringLiteral("llave"));
    QVERIFY(!f->enviar(QUrl::fromLocalFile(kRutaCer), QUrl::fromLocalFile(kRutaKey), QString()));
    QCOMPARE(f->campoConError(), QStringLiteral("contrasena"));
    QVERIFY(g.credenciales.registros.isEmpty());

    // Otros errores del servicio: perfil inactivo, credencial existente.
    QVERIFY(f->enviar(QUrl::fromLocalFile(kRutaCer), QUrl::fromLocalFile(kRutaKey), kContrasena));
    g.credenciales.importaciones.resolver(0, CredencialesSatService::ResultadoImportacion::fallo(
                                                 FakeCredencialesSatService::perfilInvalido(
                                                     ErrorCredencialSat::CodigoPerfil::PerfilInactivo)));
    QTRY_COMPARE(f->errorKey(), QStringLiteral("PerfilInactivo"));
    QVERIFY(f->campoConError().isEmpty());
    QVERIFY(f->enviar(QUrl::fromLocalFile(kRutaCer), QUrl::fromLocalFile(kRutaKey), kContrasena));
    g.credenciales.importaciones.resolver(
        1, CredencialesSatService::ResultadoImportacion::fallo(FakeCredencialesSatService::existente()));
    QTRY_COMPARE(f->errorKey(), QStringLiteral("CredencialExistente"));
}

void TestPerfilesViewModels::reemplazoValidoActualizaEstado()
{
    Grafo g;
    const PerfilResumen perfil = FakePerfilesSatService::perfil("AAA010101AAA", "Con e.firma");
    g.p()->cargar();
    QVERIFY(g.cargarCon(0, {perfil}));
    g.credenciales.resolverResumen(0, EstadoCredencial::Vencida);
    QVERIFY(g.p()->seleccionar(perfil.id.texto()));
    QTRY_VERIFY(g.p()->tieneCredencial()); // -> Reemplazar e.firma
    QVERIFY(g.p()->puedeGestionarEFirma());

    EFirmaFormViewModel* f = g.f();
    f->iniciar(perfil.id.texto(), perfil.rfc, true);
    QVERIFY(f->esReemplazo());
    QVERIFY(f->enviar(QUrl::fromLocalFile(kRutaCer), QUrl::fromLocalFile(kRutaKey), kContrasena));
    QCOMPARE(g.credenciales.historial.constLast(), QStringLiteral("reemplazar"));
    QVERIFY(g.credenciales.registros.constFirst().esReemplazo);

    // Commit: el servicio emite credencialCambio y completa con exito.
    g.credenciales.emitirCambio(perfil.id);
    g.credenciales.reemplazos.resolver(
        0, CredencialesSatService::ResultadoImportacion::exito(FakeCredencialesSatService::importada()));
    QTRY_COMPARE(f->fase(), Fase::Exito);
    QTRY_VERIFY(g.credenciales.resumenes.size() >= 2);
    g.credenciales.resolverResumen(g.credenciales.resumenes.size() - 1, EstadoCredencial::Lista);
    QTRY_COMPARE(g.p()->seleccionPreparacion(), QStringLiteral("Lista"));
    QVERIFY(g.p()->seleccionListo());
}

void TestPerfilesViewModels::reemplazoFallidoConservaEstadoReal()
{
    Grafo g;
    const PerfilResumen perfil = FakePerfilesSatService::perfil("AAA010101AAA", "Con e.firma");
    g.p()->cargar();
    QVERIFY(g.cargarCon(0, {perfil}));
    g.credenciales.resolverResumen(0, EstadoCredencial::Vencida);
    QVERIFY(g.p()->seleccionar(perfil.id.texto()));
    QTRY_COMPARE(g.p()->seleccionPreparacion(), QStringLiteral("Vencida"));

    EFirmaFormViewModel* f = g.f();
    f->iniciar(perfil.id.texto(), perfil.rfc, true);
    QVERIFY(f->enviar(QUrl::fromLocalFile(kRutaCer), QUrl::fromLocalFile(kRutaKey), kContrasena));
    g.credenciales.reemplazos.resolver(
        0, CredencialesSatService::ResultadoImportacion::fallo(
               FakeCredencialesSatService::errorAlmacen(Categoria::ParejaIncompatible)));
    QTRY_COMPARE(f->fase(), Fase::Error);
    QVERIFY(f->errorMessage().contains(QStringLiteral("anterior sigue registrada")));

    // Sin credencialCambio, el fallo provoca reverificar el estado real.
    QTRY_COMPARE(g.credenciales.resumenes.size(), 2);
    QCOMPARE(g.credenciales.idsResumen.constLast(), perfil.id);
    QCOMPARE(g.p()->seleccionPreparacion(), QStringLiteral("Verificando"));
    g.credenciales.resolverResumen(1, EstadoCredencial::Vencida);
    QTRY_COMPARE(g.p()->seleccionPreparacion(), QStringLiteral("Vencida"));
    QVERIFY(g.p()->tieneCredencial());
}

void TestPerfilesViewModels::eFirmaCambioDePerfilDescartaRespuestaTardia()
{
    Grafo g;
    EFirmaFormViewModel* f = g.f();
    const PerfilId a = PerfilId::generar();
    const PerfilId b = PerfilId::generar();
    f->iniciar(a.texto(), QStringLiteral("AAA010101AAA"), false);
    QVERIFY(f->enviar(QUrl::fromLocalFile(kRutaCer), QUrl::fromLocalFile(kRutaKey), kContrasena));
    QSignalSpy terminada(f, &EFirmaFormViewModel::operacionTerminada);

    f->iniciar(b.texto(), QStringLiteral("BBB010101BBB"), false); // otro perfil
    QCOMPARE(f->fase(), Fase::Capturando);
    QVERIFY(f->nombreCertificado().isEmpty());
    g.credenciales.importaciones.resolver(
        0, CredencialesSatService::ResultadoImportacion::fallo(
               FakeCredencialesSatService::errorAlmacen(Categoria::ContrasenaIncorrecta, OrigenErrorEFirma::Contrasena)));
    procesarEventos();
    QCOMPARE(f->fase(), Fase::Capturando);
    QVERIFY(f->errorKey().isEmpty());
    QCOMPARE(f->perfilId(), b.texto());
    QCOMPARE(terminada.count(), 0);

    // cancelar() descarta basenames y rutas privadas.
    f->seleccionarCertificado(QUrl::fromLocalFile(kRutaCer));
    f->cancelar();
    QVERIFY(f->nombreCertificado().isEmpty());
    QVERIFY(!f->enviar(QUrl(), QUrl::fromLocalFile(kRutaKey), kContrasena));
    QCOMPARE(f->campoConError(), QStringLiteral("certificado"));
}

void TestPerfilesViewModels::navegacionPerfilesYCentinelas()
{
    Grafo g;
    AppViewModel* app = g.vms.app();
    QVERIFY(g.vms.initialProperties().contains(QStringLiteral("perfilesViewModel")));
    QVERIFY(g.vms.initialProperties().contains(QStringLiteral("eFirmaViewModel")));

    QVERIFY(QMetaObject::invokeMethod(app, "mostrarPerfiles"));
    QCOMPARE(app->pagina(), AppViewModel::Pagina::Perfiles);
    QCOMPARE(g.perfiles.listas.size(), 1); // abrir Perfiles carga la lista

    // Salir de Perfiles descarta la captura de e.firma y el formulario.
    g.f()->iniciar(PerfilId::generar().texto(), QStringLiteral("AAA010101AAA"), false);
    g.f()->seleccionarCertificado(QUrl::fromLocalFile(kRutaCer));
    g.p()->nuevo();
    app->mostrarLista();
    QCOMPARE(app->pagina(), AppViewModel::Pagina::Lista);
    QVERIFY(g.f()->nombreCertificado().isEmpty());
    QCOMPARE(g.p()->modo(), Modo::Ninguno);

    // Centinelas sobre todos los view models expuestos a QML.
    for (const QObject* vm : {static_cast<QObject*>(app), static_cast<QObject*>(g.p()),
                              static_cast<QObject*>(g.f()), static_cast<QObject*>(g.vms.nuevaSolicitud()),
                              static_cast<QObject*>(g.p()->perfiles())}) {
        verificarSinSecretos(vm);
    }
    // Ningun rol expuesto sugiere rutas o secretos.
    const QRegularExpression sensible(QStringLiteral("ruta|path|password|contrasena|secret"),
                                      QRegularExpression::CaseInsensitiveOption);
    for (const QByteArray& rol : g.p()->perfiles()->roleNames()) {
        QVERIFY2(!sensible.match(QString::fromLatin1(rol)).hasMatch(), rol.constData());
    }
    for (const QByteArray& rol : g.vms.nuevaSolicitud()->perfilesDisponibles()->roleNames()) {
        QVERIFY2(!sensible.match(QString::fromLatin1(rol)).hasMatch(), rol.constData());
    }
}

QTEST_MAIN(TestPerfilesViewModels)

#include "TestPerfilesViewModels.moc"
