// satcfdi_respaldo_vm_tests (T025.6): RespaldoViewModel con fakes de los
// tres puertos de presentacion y el catalogo de mensajes del respaldo.
// Datos sinteticos (/Users/usuario, contrasenas y claves inventadas).

#include "RespaldoFakes.h"

#include "CatalogoMensajes.h"
#include "RespaldoViewModel.h"

#include <QMetaProperty>
#include <QSignalSpy>
#include <QTest>

using namespace satcfdi;
using fakes::FakeControlRespaldo;
using fakes::FakeControlRestauracion;
using fakes::FakeDatosPrevios;
using EstadoR = respaldo::ResultadoRespaldo::Estado;

namespace {

const char* const kContrasena = "contrasena-sintetica-1";
const char* const kClave = "ABCD-EFGH-JKMN-PQRS";

// Ningun Q_PROPERTY del view model contiene `texto`.
bool propiedadesContienen(const QObject& objeto, const QString& texto)
{
    const QMetaObject* mo = objeto.metaObject();
    for (int i = 0; i < mo->propertyCount(); ++i) {
        const QVariant v = mo->property(i).read(&objeto);
        if (v.toString().contains(texto) || v.toStringList().join(QLatin1Char(' ')).contains(texto)) {
            return true;
        }
    }
    return false;
}

// Textos visibles sin "subido", rutas ni diagnosticos.
void verificarSaneado(const QString& texto)
{
    const QString t = texto.toLower();
    QVERIFY2(!t.contains(QStringLiteral("subid")), qPrintable(texto));
    QVERIFY2(!t.contains(QStringLiteral("/users")), qPrintable(texto));
    QVERIFY2(!t.contains(QLatin1Char('/')), qPrintable(texto));
    QVERIFY2(!t.contains(QStringLiteral("xaxx")), qPrintable(texto));
    QVERIFY2(!t.contains(QLatin1Char('_')), qPrintable(texto)); // claves estables
}

ResultadoRespaldoPresentacion resultado(EstadoR estado)
{
    ResultadoRespaldoPresentacion r;
    r.estado = estado;
    r.fechaUtc = QDateTime(QDate(2026, 10, 21), QTime(15, 0), QTimeZone::UTC);
    return r;
}

std::shared_ptr<BufferSecreto> buffer(const char* texto)
{
    return std::make_shared<BufferSecreto>(BufferSecreto::desdeTexto(QString::fromUtf8(texto)));
}

} // namespace

class TestRespaldoViewModel : public QObject {
    Q_OBJECT

private slots:
    void sinPuertosNoDisponible();
    void carpetaSoloNombreYErroresSaneados();
    void crearContrasenaEntregaClaveUnaVez();
    void cambiarContrasenaConClave();
    void imprimirClaveSinReentrada();
    void contrasenaMinimaDe12();
    void clavePendienteHastaConfirmar();
    void destruirConOperacionesPendientes();
    void respaldarAhoraProgresoYAdvertencias();
    void respaldoDiarioYYaEnCurso();
    void erroresDeRespaldo();
    void restaurarCancelarYConfirmar();
    void restaurarRechazadaYValidaciones();
    void datosPreviosYArranque();
    void catalogoSinSubidoNiRutas();
};

void TestRespaldoViewModel::sinPuertosNoDisponible()
{
    RespaldoViewModel vm;
    QVERIFY(!vm.disponible());
    QVERIFY(!vm.cargando());
    QVERIFY(!vm.puedeRespaldar());
    QVERIFY(!vm.restauracionDisponible());
    QVERIFY(!vm.puedeImprimir());
    vm.respaldarAhora();
    vm.elegirCarpeta(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Respaldos")));
    vm.crearContrasena(QString::fromUtf8(kContrasena), QString::fromUtf8(kContrasena));
    QVERIFY(!vm.respaldando());
    QVERIFY(vm.mensaje().isEmpty());
    QVERIFY(!vm.hayDatosPrevios());
    QVERIFY(!vm.avisoArranqueActivo());

    FakeControlRespaldo control;
    control.estadoActual.disponible = false;
    control.estadoActual.cargado = true;
    vm.setControlRespaldo(&control);
    QVERIFY(!vm.disponible());
}

void TestRespaldoViewModel::carpetaSoloNombreYErroresSaneados()
{
    FakeControlRespaldo control;
    control.estadoActual.cargado = true;
    RespaldoViewModel vm;
    vm.setControlRespaldo(&control);
    QVERIFY(vm.disponible());
    QVERIFY(!vm.carpetaConfigurada());
    QCOMPARE(vm.textoUltimoExito(), QStringLiteral("Aún no hay respaldos."));

    // URL no local: sin llamada.
    vm.elegirCarpeta(QUrl(QStringLiteral("https://example.invalid/carpeta")));
    QCOMPARE(control.rutas.size(), 0);
    QCOMPARE(vm.varianteMensaje(), QStringLiteral("error"));

    vm.elegirCarpeta(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Respaldos")));
    QCOMPARE(control.rutas, QStringList{QStringLiteral("/Users/usuario/Respaldos")});
    QVERIFY(vm.ocupado());
    vm.elegirCarpeta(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Otra")));
    QCOMPARE(control.rutas.size(), 1); // una a la vez

    control.carpeta.resolverUltima(FakeControlRespaldo::fallo(respaldo::ErrorGestorRespaldo::Categoria::Carpeta));
    QTRY_VERIFY(!vm.ocupado());
    QCOMPARE(vm.varianteMensaje(), QStringLiteral("error"));
    verificarSaneado(vm.mensaje());

    vm.elegirCarpeta(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Respaldos")));
    control.estadoActual = FakeControlRespaldo::listo();
    control.carpeta.resolverUltima(FakeControlRespaldo::ok());
    QTRY_VERIFY(!vm.ocupado());
    QCOMPARE(vm.varianteMensaje(), QStringLiteral("exito"));
    QCOMPARE(vm.nombreCarpeta(), QStringLiteral("Respaldos"));
    QVERIFY(!propiedadesContienen(vm, QStringLiteral("/Users/usuario")));
}

void TestRespaldoViewModel::crearContrasenaEntregaClaveUnaVez()
{
    FakeControlRespaldo control;
    control.estadoActual.cargado = true;
    RespaldoViewModel vm;
    vm.setControlRespaldo(&control);
    QSignalSpy clave(&vm, &RespaldoViewModel::claveRecuperacionLista);

    vm.crearContrasena({}, {});
    QVERIFY(!vm.errorFormulario().isEmpty());
    vm.crearContrasena(QString::fromUtf8(kContrasena), QStringLiteral("otra"));
    QCOMPARE(vm.errorFormulario(), QStringLiteral("Las contraseñas no coinciden."));
    QCOMPARE(control.contrasena.size(), 0);

    vm.crearContrasena(QString::fromUtf8(kContrasena), QString::fromUtf8(kContrasena));
    QVERIFY(vm.procesandoFormulario());
    QVERIFY(vm.errorFormulario().isEmpty());
    QVERIFY(fakes::mismoSecreto(control.ultimaContrasena, kContrasena));
    QVERIFY(!propiedadesContienen(vm, QString::fromUtf8(kContrasena)));

    control.estadoActual.contrasenaConfigurada = true;
    control.contrasena.resolverUltima(FakeControlRespaldo::ResultadoClave::exito(buffer(kClave)));
    QTRY_COMPARE(clave.size(), 1);
    QVERIFY(clave.at(0).isEmpty()); // la senal no lleva la clave
    QVERIFY(vm.hayClavePendiente());
    QCOMPARE(vm.tomarClaveRecuperacion(), QString::fromUtf8(kClave));
    QVERIFY(!vm.procesandoFormulario());
    QVERIFY(vm.contrasenaConfigurada());
    // C3: la clave nunca es una propiedad.
    QVERIFY(!propiedadesContienen(vm, QString::fromUtf8(kClave)));

    // H1: sin CopiaTextoSecreto no se copia (nunca QClipboard).
    QVERIFY(!vm.puedeCopiar());
    QVERIFY(!vm.copiarClaveRecuperacion());
    QString copiado;
    vm.setCopiaSecreta([&copiado](const QString& t) {
        copiado = t;
        return true;
    });
    QVERIFY(vm.puedeCopiar());
    QVERIFY(vm.copiarClaveRecuperacion());
    QCOMPARE(copiado, QString::fromUtf8(kClave));
    vm.olvidarClaveRecuperacion();
    QVERIFY(!vm.hayClavePendiente());
    QVERIFY(vm.tomarClaveRecuperacion().isEmpty());
    QVERIFY(!vm.copiarClaveRecuperacion());

    // Error del gestor: mensaje saneado en el formulario.
    vm.crearContrasena(QString::fromUtf8(kContrasena), QString::fromUtf8(kContrasena));
    control.contrasena.resolverUltima(FakeControlRespaldo::ResultadoClave::fallo(
        respaldo::ErrorGestorRespaldo::de(respaldo::ErrorGestorRespaldo::Categoria::Almacen, QStringLiteral("kek.x"))));
    QTRY_VERIFY(!vm.procesandoFormulario());
    QVERIFY(!vm.errorFormulario().isEmpty());
    verificarSaneado(vm.errorFormulario());
    QCOMPARE(clave.size(), 1);
}

void TestRespaldoViewModel::cambiarContrasenaConClave()
{
    FakeControlRespaldo control;
    control.estadoActual = FakeControlRespaldo::listo();
    RespaldoViewModel vm;
    vm.setControlRespaldo(&control);
    QSignalSpy cambiada(&vm, &RespaldoViewModel::contrasenaCambiada);

    vm.cambiarContrasena({}, RespaldoViewModel::Verificacion::VerificarConClave, QStringLiteral("n"),
                         QStringLiteral("n"));
    QCOMPARE(vm.errorFormulario(), QStringLiteral("Escribe la clave de recuperación."));
    vm.cambiarContrasena(QString::fromUtf8(kClave), RespaldoViewModel::Verificacion::VerificarConClave,
                         QStringLiteral("contrasena-nueva-1"), QStringLiteral("contrasena-nueva-2"));
    QCOMPARE(vm.errorFormulario(), QStringLiteral("Las contraseñas nuevas no coinciden."));
    QCOMPARE(control.cambio.size(), 0);

    vm.cambiarContrasena(QString::fromUtf8(kClave), RespaldoViewModel::Verificacion::VerificarConClave,
                         QString::fromUtf8(kContrasena), QString::fromUtf8(kContrasena));
    QCOMPARE(control.ultimoTipo, respaldo::VerificacionCambio::Tipo::ClaveRecuperacion);
    QVERIFY(fakes::mismoSecreto(control.ultimaVerificacion, kClave));
    QVERIFY(fakes::mismoSecreto(control.ultimaContrasena, kContrasena));
    control.cambio.resolverUltima(
        FakeControlRespaldo::fallo(respaldo::ErrorGestorRespaldo::Categoria::VerificacionIncorrecta));
    QTRY_VERIFY(!vm.procesandoFormulario());
    QCOMPARE(vm.errorFormulario(), QStringLiteral("La contraseña anterior o la clave de recuperación no es correcta."));
    QCOMPARE(cambiada.size(), 0);

    vm.cambiarContrasena(QStringLiteral("anterior"), RespaldoViewModel::Verificacion::VerificarConContrasena,
                         QString::fromUtf8(kContrasena), QString::fromUtf8(kContrasena));
    QCOMPARE(control.ultimoTipo, respaldo::VerificacionCambio::Tipo::ContrasenaAnterior);
    control.cambio.resolverUltima(FakeControlRespaldo::ok());
    QTRY_COMPARE(cambiada.size(), 1);
    QVERIFY(vm.errorFormulario().isEmpty());
    QVERIFY(vm.mensaje().contains(QStringLiteral("clave de recuperación sigue siendo la misma")));
}

void TestRespaldoViewModel::imprimirClaveSinReentrada()
{
    FakeControlRespaldo control;
    control.estadoActual.cargado = true;
    RespaldoViewModel vm;
    vm.setControlRespaldo(&control);
    QVERIFY(!vm.puedeImprimir());
    QVERIFY(!vm.imprimirClaveRecuperacion());

    QString titulo;
    QString texto;
    bool reentradaRechazada = false;
    bool copiaRechazada = false;
    bool imprimiendoDurante = false;
    vm.setImpresion([&](const QString& t, const QString& x) {
        titulo = t;
        texto = x;
        imprimiendoDurante = vm.imprimiendo();
        // Panel app-modal: el bucle de eventos sigue vivo.
        reentradaRechazada = !vm.imprimirClaveRecuperacion();
        copiaRechazada = !vm.copiarClaveRecuperacion();
        vm.olvidarClaveRecuperacion(); // se difiere
        return false;                  // cancelado
    });
    QVERIFY(vm.puedeImprimir());
    vm.setCopiaSecreta([](const QString&) { return true; });

    QSignalSpy clave(&vm, &RespaldoViewModel::claveRecuperacionLista);
    vm.crearContrasena(QString::fromUtf8(kContrasena), QString::fromUtf8(kContrasena));
    control.contrasena.resolverUltima(FakeControlRespaldo::ResultadoClave::exito(buffer(kClave)));
    QTRY_COMPARE(clave.size(), 1);

    QVERIFY(!vm.imprimirClaveRecuperacion());
    QVERIFY(imprimiendoDurante);
    QVERIFY(reentradaRechazada);
    QVERIFY(copiaRechazada);
    QVERIFY(!vm.imprimiendo());
    QVERIFY(texto.startsWith(QString::fromUtf8(kClave)));
    QVERIFY(!titulo.contains(QString::fromUtf8(kClave)));
    // El olvido diferido se aplico al terminar.
    QVERIFY(!vm.hayClavePendiente());
    QVERIFY(!vm.copiarClaveRecuperacion());
}

void TestRespaldoViewModel::contrasenaMinimaDe12()
{
    FakeControlRespaldo control;
    control.estadoActual = FakeControlRespaldo::listo();
    RespaldoViewModel vm;
    vm.setControlRespaldo(&control);
    QCOMPARE(RespaldoViewModel::kMinimoContrasena, 12);

    vm.crearContrasena(QStringLiteral("corta-11car"), QStringLiteral("corta-11car"));
    QCOMPARE(vm.errorFormulario(), QStringLiteral("La contraseña debe tener al menos 12 caracteres."));
    QCOMPARE(control.contrasena.size(), 0);
    // 12 caracteres con acentos y un emoji (par suplente cuenta como uno).
    const QString doce = QStringLiteral("ñandú-ábaco") + QString::fromUtf8("\xF0\x9F\x94\x91");
    QCOMPARE(doce.size(), 13);
    vm.crearContrasena(doce, doce);
    QCOMPARE(control.contrasena.size(), 1);
    control.contrasena.resolverUltima(FakeControlRespaldo::ResultadoClave::fallo(
        respaldo::ErrorGestorRespaldo::de(respaldo::ErrorGestorRespaldo::Categoria::YaConfigurada)));
    QTRY_VERIFY(!vm.procesandoFormulario());

    // Cambiar: la nueva exige 12; la verificacion (anterior o clave) no.
    vm.cambiarContrasena(QStringLiteral("corta"), RespaldoViewModel::Verificacion::VerificarConContrasena,
                         QStringLiteral("nueva-corta"), QStringLiteral("nueva-corta"));
    QCOMPARE(vm.errorFormulario(), QStringLiteral("La contraseña nueva debe tener al menos 12 caracteres."));
    QCOMPARE(control.cambio.size(), 0);
    vm.cambiarContrasena(QStringLiteral("corta"), RespaldoViewModel::Verificacion::VerificarConContrasena,
                         QString::fromUtf8(kContrasena), QString::fromUtf8(kContrasena));
    QCOMPARE(control.cambio.size(), 1);

    // Restaurar no exige minimo.
    FakeControlRestauracion restauracion;
    vm.setControlRestauracion(&restauracion);
    vm.prepararRestauracion(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/a.afbak")), QStringLiteral("x"),
                            RespaldoViewModel::Credencial::CredencialAutomatica);
    QCOMPARE(restauracion.archivos.size(), 1);
}

void TestRespaldoViewModel::clavePendienteHastaConfirmar()
{
    FakeControlRespaldo control;
    control.estadoActual.cargado = true;
    control.url = QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Respaldos"));
    RespaldoViewModel vm;
    vm.setControlRespaldo(&control);
    QCOMPARE(vm.carpetaInicialRestauracion(), control.url);
    QVERIFY(!propiedadesContienen(vm, QStringLiteral("/Users/usuario")));

    QSignalSpy pendiente(&vm, &RespaldoViewModel::clavePendienteChanged);
    vm.crearContrasena(QString::fromUtf8(kContrasena), QString::fromUtf8(kContrasena));
    control.contrasena.resolverUltima(FakeControlRespaldo::ResultadoClave::exito(buffer(kClave)));
    QTRY_VERIFY(vm.hayClavePendiente());
    QCOMPARE(pendiente.size(), 1);
    // Se puede pedir otra vez (la pagina se descargo y volvio) hasta confirmar.
    QCOMPARE(vm.tomarClaveRecuperacion(), QString::fromUtf8(kClave));
    QCOMPARE(vm.tomarClaveRecuperacion(), QString::fromUtf8(kClave));
    vm.olvidarClaveRecuperacion();
    QVERIFY(!vm.hayClavePendiente());
    QCOMPARE(pendiente.size(), 2);
    vm.olvidarClaveRecuperacion();
    QCOMPARE(pendiente.size(), 2);
}

// Calidad: destruir el view model con operaciones pendientes y resolverlas
// despues no debe tocar el objeto destruido.
void TestRespaldoViewModel::destruirConOperacionesPendientes()
{
    FakeControlRespaldo control;
    control.estadoActual = FakeControlRespaldo::listo();
    FakeControlRestauracion restauracion;
    FakeDatosPrevios datos;
    auto* vm = new RespaldoViewModel;
    vm->setControlRespaldo(&control);
    vm->setControlRestauracion(&restauracion);
    vm->setDatosPrevios(&datos);

    vm->respaldarAhora();
    vm->crearContrasena(QString::fromUtf8(kContrasena), QString::fromUtf8(kContrasena));
    vm->prepararRestauracion(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/a.afbak")), QStringLiteral("x"),
                             RespaldoViewModel::Credencial::CredencialAutomatica);
    datos.consultas.resolverUltima(true);
    QTRY_VERIFY(vm->hayDatosPrevios());
    vm->descartarDatosPrevios();
    QCOMPARE(control.respaldos.size(), 1);
    QCOMPARE(restauracion.preparaciones.size(), 1);
    QCOMPARE(datos.descartes.size(), 1);
    delete vm;

    control.respaldos.resolverUltima(resultado(EstadoR::Exito));
    control.contrasena.resolverUltima(FakeControlRespaldo::ResultadoClave::exito(buffer(kClave)));
    restauracion.preparaciones.resolverUltima(
        FakeControlRestauracion::ResultadoPreparacion::exito(FakeControlRestauracion::resumen()));
    datos.descartes.resolverUltima({});
    control.emitirProgreso(respaldo::RespaldoService::Fase::Escritura, 1, 2);
    restauracion.emitirProgreso(respaldo::RestauracionService::Fase::Validando, 1, 2);
    control.emitirTerminado(resultado(EstadoR::Exito));
    QTest::qWait(20); // continuaciones encoladas
}

void TestRespaldoViewModel::respaldarAhoraProgresoYAdvertencias()
{
    FakeControlRespaldo control;
    control.estadoActual = FakeControlRespaldo::listo();
    RespaldoViewModel vm;
    vm.setControlRespaldo(&control);
    QVERIFY(vm.puedeRespaldar());
    QVERIFY(!vm.hayResultado());

    vm.respaldarAhora();
    QVERIFY(vm.respaldando());
    QVERIFY(!vm.puedeRespaldar());
    QCOMPARE(vm.fraccionRespaldo(), -1.0);
    vm.respaldarAhora();
    QCOMPARE(control.respaldos.size(), 1);

    control.emitirProgreso(respaldo::RespaldoService::Fase::Escritura, 50, 100);
    QCOMPARE(vm.textoFaseRespaldo(), QStringLiteral("Cifrando y copiando a la carpeta…"));
    QCOMPARE(vm.fraccionRespaldo(), 0.5);
    control.emitirProgreso(respaldo::RespaldoService::Fase::Captura, 0, 0);
    QCOMPARE(vm.fraccionRespaldo(), -1.0);

    ResultadoRespaldoPresentacion r = resultado(EstadoR::ExitoConAdvertencias);
    r.bytes = 2'500'000;
    r.faltantes = 2;
    r.avisoDepuracion = true;
    r.copiasSinRegistro = 3;
    control.respaldos.resolverUltima(r);
    QTRY_VERIFY(!vm.respaldando());
    QVERIFY(vm.hayResultado());
    QCOMPARE(vm.varianteResultado(), QStringLiteral("advertencia"));
    QVERIFY(vm.tituloResultado().contains(QStringLiteral("copiado a la carpeta")));
    QVERIFY(vm.descripcionResultado().contains(QStringLiteral("2.5 MB")));
    QCOMPARE(vm.advertenciasResultado().size(), 2);
    QVERIFY(vm.advertenciasResultado().at(0).startsWith(QStringLiteral("2 paquetes")));
    QVERIFY(vm.avisoCopiasSinRegistro().contains(QStringLiteral("Hay 3 copias anteriores")));
    QVERIFY(vm.avisoCopiasSinRegistro().contains(QStringLiteral("bórralas a mano")));
    QVERIFY(vm.textoFaseRespaldo().isEmpty());

    // Exito limpio.
    vm.respaldarAhora();
    control.respaldos.resolverUltima(resultado(EstadoR::Exito));
    QTRY_COMPARE(vm.varianteResultado(), QStringLiteral("exito"));
    QCOMPARE(vm.tituloResultado(), QStringLiteral("Respaldo copiado a la carpeta"));
    QVERIFY(vm.avisoCopiasSinRegistro().isEmpty());
    QVERIFY(vm.advertenciasResultado().isEmpty());

    // Q1: el resumen no se pudo guardar (exito y fallo).
    ResultadoRespaldoPresentacion sinGuardar = resultado(EstadoR::Exito);
    sinGuardar.avisoPersistencia = true;
    control.emitirTerminado(sinGuardar);
    QCOMPARE(vm.varianteResultado(), QStringLiteral("advertencia"));
    QCOMPARE(vm.advertenciasResultado(),
             QStringList{QStringLiteral("La copia se hizo, pero no se pudo guardar este resultado; al reabrir la "
                                        "aplicación no se mostrará.")});
    ResultadoRespaldoPresentacion falloSinGuardar = resultado(EstadoR::ErrorFuente);
    falloSinGuardar.avisoPersistencia = true;
    control.emitirTerminado(falloSinGuardar);
    QCOMPARE(vm.varianteResultado(), QStringLiteral("error"));
    QCOMPARE(vm.advertenciasResultado(),
             QStringList{QStringLiteral("No se pudo guardar este resultado; al reabrir la aplicación no se mostrará.")});
    for (const QString& t : vm.advertenciasResultado()) {
        QVERIFY(!t.contains(QStringLiteral("quedó guardado")));
    }
}

void TestRespaldoViewModel::respaldoDiarioYYaEnCurso()
{
    FakeControlRespaldo control;
    control.estadoActual = FakeControlRespaldo::listo();
    control.ultimo = resultado(EstadoR::Exito); // persistido (C6)
    RespaldoViewModel vm;
    vm.setControlRespaldo(&control);
    QVERIFY(vm.hayResultado());

    // Diario en curso: el estado del puerto deshabilita "Respaldar ahora".
    EstadoRespaldoPresentacion e = FakeControlRespaldo::listo();
    e.enCurso = true;
    control.cambiarEstado(e);
    QVERIFY(vm.respaldando());
    QVERIFY(!vm.puedeRespaldar());
    ResultadoRespaldoPresentacion fallo = resultado(EstadoR::ErrorDestino);
    fallo.errorDestino = respaldo::ErrorDestino::Categoria::VolumenNoDisponible;
    control.emitirTerminado(fallo);
    control.cambiarEstado(FakeControlRespaldo::listo());
    QCOMPARE(vm.varianteResultado(), QStringLiteral("error"));
    QCOMPARE(vm.tituloResultado(), QStringLiteral("No se creó el respaldo"));

    // YaEnCurso: mensaje neutro, no reemplaza el ultimo resultado.
    vm.respaldarAhora();
    control.respaldos.resolverUltima(resultado(EstadoR::YaEnCurso));
    QTRY_VERIFY(!vm.respaldando());
    QCOMPARE(vm.varianteMensaje(), QStringLiteral("neutro"));
    QCOMPARE(vm.varianteResultado(), QStringLiteral("error"));

    // Diario: activar y pendiente.
    vm.activarDiario(true);
    QCOMPARE(control.diarios, QList<bool>{true});
    QVERIFY(vm.ocupado());
    e = FakeControlRespaldo::listo();
    e.diarioActivado = true;
    e.contrasenaConfigurada = false;
    e.pendienteDiario = EstadoRespaldoPresentacion::PendienteDiario::FaltaContrasena;
    control.estadoActual = e;
    control.diario.resolverUltima(FakeControlRespaldo::ok());
    QTRY_VERIFY(!vm.ocupado());
    QVERIFY(vm.diarioActivado());
    QCOMPARE(vm.textoPendienteDiario(), QStringLiteral("El respaldo diario empezará cuando crees una contraseña."));
}

void TestRespaldoViewModel::erroresDeRespaldo()
{
    using CD = respaldo::ErrorDestino::Categoria;
    ResultadoRespaldoPresentacion r = resultado(EstadoR::ErrorDestino);
    r.errorDestino = CD::ExclusivoNoSoportado;
    QVERIFY(catalogo::causaRespaldo(r).contains(QStringLiteral("exFAT")));
    r.errorDestino = CD::SinDescargar;
    QVERIFY(catalogo::causaRespaldo(r).startsWith(QStringLiteral("La carpeta elegida no está disponible")));
    r.errorDestino = CD::EspacioInsuficiente;
    QVERIFY(catalogo::causaRespaldo(r).contains(QStringLiteral("espacio")));
    r = resultado(EstadoR::ErrorFuente);
    QVERIFY(catalogo::causaRespaldo(r).contains(QStringLiteral("cambiaron")));
    r = resultado(EstadoR::ErrorContenedor); // sin categoria (KEK previa)
    QVERIFY(!catalogo::causaRespaldo(r).isEmpty());
    r = resultado(EstadoR::Cancelado);
    QCOMPARE(catalogo::mensajeRespaldo(r).variante, QStringLiteral("neutro"));
}

void TestRespaldoViewModel::restaurarCancelarYConfirmar()
{
    FakeControlRestauracion control;
    RespaldoViewModel vm;
    vm.setControlRestauracion(&control);
    QVERIFY(vm.restauracionDisponible());
    QCOMPARE(vm.estadoRestauracion(), RespaldoViewModel::EstadoRestauracion::Inactiva);
    const QUrl archivo = QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Respaldos/ArchivoFiscal-prueba.afbak"));

    vm.prepararRestauracion(archivo, QString::fromUtf8(kContrasena), RespaldoViewModel::Credencial::CredencialAutomatica);
    QCOMPARE(vm.estadoRestauracion(), RespaldoViewModel::EstadoRestauracion::Preparando);
    QCOMPARE(control.archivos.size(), 1);
    QCOMPARE(control.credenciales.at(0), respaldo::PeticionRestauracion::Credencial::Automatica);
    QVERIFY(fakes::mismoSecreto(control.ultimoSecreto, kContrasena));
    QCOMPARE(vm.nombreArchivoRestauracion(), QStringLiteral("ArchivoFiscal-prueba.afbak"));
    QVERIFY(!propiedadesContienen(vm, QStringLiteral("/Users/usuario")));
    QVERIFY(!propiedadesContienen(vm, QString::fromUtf8(kContrasena)));

    control.emitirProgreso(respaldo::RestauracionService::Fase::Extrayendo, 1, 4);
    QCOMPARE(vm.textoFaseRestauracion(), QStringLiteral("Extrayendo y verificando el respaldo…"));
    QCOMPARE(vm.fraccionRestauracion(), 0.25);

    // Cancelar: sin error visible.
    vm.cancelarRestauracion();
    QCOMPARE(control.cancelaciones, 1);
    QTRY_COMPARE(vm.estadoRestauracion(), RespaldoViewModel::EstadoRestauracion::Inactiva);
    QVERIFY(vm.errorRestauracion().isEmpty());
    QVERIFY(vm.textoFaseRestauracion().isEmpty());

    // Preparar y mostrar el resumen autenticado.
    vm.prepararRestauracion(archivo, QString::fromUtf8(kClave), RespaldoViewModel::Credencial::CredencialClave);
    QCOMPARE(control.credenciales.at(1), respaldo::PeticionRestauracion::Credencial::ClaveRecuperacion);
    control.estaPreparada = true;
    control.preparaciones.resolverUltima(
        FakeControlRestauracion::ResultadoPreparacion::exito(FakeControlRestauracion::resumen()));
    QTRY_COMPARE(vm.estadoRestauracion(), RespaldoViewModel::EstadoRestauracion::Preparada);
    QVERIFY(!vm.fechaRespaldoRestauracion().isEmpty());
    QCOMPARE(vm.idRespaldoRestauracion(), QStringLiteral("0011-2233-4455-6677-8899-AABB-CCDD-EEFF"));
    QCOMPARE(vm.conteosRestauracion(),
             QStringLiteral("2 perfiles, 5 solicitudes y 1 paquete. 1 paquete ya faltaba cuando se hizo el respaldo."));
    QCOMPARE(vm.versionAppRestauracion(), QStringLiteral("Creado con Archivo Fiscal 1.0.0"));
    QVERIFY(vm.avisoMigracionRestauracion().isEmpty());

    // Confirmar: SinRelanzamiento (C5).
    vm.confirmarRestauracion();
    QCOMPARE(vm.estadoRestauracion(), RespaldoViewModel::EstadoRestauracion::Solicitando);
    respaldo::ResultadoSolicitudRestauracion s;
    s.estado = respaldo::ResultadoSolicitudRestauracion::Estado::SinRelanzamiento;
    control.solicitudes.resolverUltima(s);
    QTRY_COMPARE(vm.estadoRestauracion(), RespaldoViewModel::EstadoRestauracion::SinRelanzamiento);
    QCOMPARE(vm.tituloSolicitud(), QStringLiteral("Abre de nuevo la aplicación"));
    QCOMPARE(vm.varianteSolicitud(), QStringLiteral("advertencia"));
    // Ya no acepta otra preparacion.
    vm.prepararRestauracion(archivo, QString::fromUtf8(kContrasena), RespaldoViewModel::Credencial::CredencialAutomatica);
    QCOMPARE(control.archivos.size(), 2);
}

void TestRespaldoViewModel::restaurarRechazadaYValidaciones()
{
    FakeControlRestauracion control;
    RespaldoViewModel vm;
    vm.setControlRestauracion(&control);

    vm.prepararRestauracion(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/datos.zip")), QStringLiteral("x"),
                            RespaldoViewModel::Credencial::CredencialAutomatica);
    QVERIFY(vm.errorRestauracion().contains(QStringLiteral(".afbak")));
    vm.prepararRestauracion(QUrl::fromLocalFile(QStringLiteral("/Users/usuario/a.afbak")), {},
                            RespaldoViewModel::Credencial::CredencialAutomatica);
    QVERIFY(!vm.errorRestauracion().isEmpty());
    QCOMPARE(control.archivos.size(), 0);

    const QUrl archivo = QUrl::fromLocalFile(QStringLiteral("/Users/usuario/a.afbak"));
    vm.prepararRestauracion(archivo, QStringLiteral("incorrecta"), RespaldoViewModel::Credencial::CredencialAutomatica);
    control.preparaciones.resolverUltima(FakeControlRestauracion::ResultadoPreparacion::fallo(
        respaldo::ErrorRestauracion::de(respaldo::ErrorRestauracion::Categoria::CredencialIncorrecta,
                                        QStringLiteral("kek.incorrecta"))));
    QTRY_COMPARE(vm.estadoRestauracion(), RespaldoViewModel::EstadoRestauracion::Inactiva);
    QCOMPARE(vm.errorRestauracion(), QStringLiteral("La contraseña o la clave de recuperación no es correcta."));
    QVERIFY(vm.nombreArchivoRestauracion().isEmpty());

    // Esquema mas nuevo.
    vm.prepararRestauracion(archivo, QStringLiteral("x"), RespaldoViewModel::Credencial::CredencialAutomatica);
    control.preparaciones.resolverUltima(FakeControlRestauracion::ResultadoPreparacion::fallo(
        respaldo::ErrorRestauracion::de(respaldo::ErrorRestauracion::Categoria::EsquemaMasNuevo)));
    QTRY_VERIFY(vm.errorRestauracion().contains(QStringLiteral("versión más reciente")));

    // Rechazada (el archivo cambio): vuelve a Inactiva con el motivo.
    vm.prepararRestauracion(archivo, QStringLiteral("x"), RespaldoViewModel::Credencial::CredencialAutomatica);
    control.estaPreparada = true;
    control.preparaciones.resolverUltima(
        FakeControlRestauracion::ResultadoPreparacion::exito(FakeControlRestauracion::resumen()));
    QTRY_COMPARE(vm.estadoRestauracion(), RespaldoViewModel::EstadoRestauracion::Preparada);
    vm.confirmarRestauracion();
    control.estaPreparada = false;
    respaldo::ResultadoSolicitudRestauracion s;
    s.estado = respaldo::ResultadoSolicitudRestauracion::Estado::Rechazada;
    s.error = respaldo::ErrorRestauracion::de(respaldo::ErrorRestauracion::Categoria::OrigenCambio);
    control.solicitudes.resolverUltima(s);
    QTRY_COMPARE(vm.estadoRestauracion(), RespaldoViewModel::EstadoRestauracion::Inactiva);
    QVERIFY(vm.errorRestauracion().contains(QStringLiteral("cambió después de verificarlo")));
    QVERIFY(vm.tituloSolicitud().isEmpty());

    // Descartar la preparacion.
    vm.prepararRestauracion(archivo, QStringLiteral("x"), RespaldoViewModel::Credencial::CredencialAutomatica);
    control.estaPreparada = true;
    control.preparaciones.resolverUltima(
        FakeControlRestauracion::ResultadoPreparacion::exito(FakeControlRestauracion::resumen()));
    QTRY_COMPARE(vm.estadoRestauracion(), RespaldoViewModel::EstadoRestauracion::Preparada);
    vm.descartarRestauracion();
    QCOMPARE(control.descartes, 1);
    QCOMPARE(vm.estadoRestauracion(), RespaldoViewModel::EstadoRestauracion::Inactiva);
    QVERIFY(vm.fechaRespaldoRestauracion().isEmpty());
}

void TestRespaldoViewModel::datosPreviosYArranque()
{
    FakeDatosPrevios datos;
    datos.arranque.estado = ResultadoArranquePresentacion::Estado::Aplicada;
    datos.arranque.creadoRespaldoUtc = QDateTime(QDate(2026, 10, 1), QTime(16, 30), QTimeZone::UTC);
    RespaldoViewModel vm;
    vm.setDatosPrevios(&datos);
    QVERIFY(vm.avisoArranqueActivo());
    QCOMPARE(vm.tituloArranque(), QStringLiteral("Restauración aplicada"));
    QCOMPARE(vm.varianteArranque(), QStringLiteral("exito"));
    QVERIFY(vm.descripcionArranque().contains(QStringLiteral("e.firma")));
    QCOMPARE(datos.consultas.size(), 1);
    QVERIFY(!vm.hayDatosPrevios());

    datos.consultas.resolverUltima(true);
    QTRY_VERIFY(vm.hayDatosPrevios());
    QVERIFY(!vm.tituloDatosPrevios().isEmpty());

    vm.descartarDatosPrevios();
    QVERIFY(vm.descartandoDatosPrevios());
    vm.descartarDatosPrevios();
    QCOMPARE(datos.descartes.size(), 1);
    vm.consultarDatosPrevios(); // no consulta mientras descarta
    QCOMPARE(datos.consultas.size(), 1);

    ResultadoDescartePresentacion r;
    r.estado = ResultadoDescartePresentacion::Estado::Incompleto;
    r.carpetasAjenas = 1;
    datos.descartes.resolverUltima(r);
    QTRY_VERIFY(!vm.descartandoDatosPrevios());
    QCOMPARE(vm.varianteDescarte(), QStringLiteral("advertencia"));
    QVERIFY(vm.descripcionDescarte().contains(QStringLiteral("no se tocó")));
    verificarSaneado(vm.descripcionDescarte().remove(QStringLiteral("pre-restauracion")));
    QCOMPARE(datos.consultas.size(), 2); // reconsulta tras descartar

    datos.consultas.resolverUltima(false);
    QTRY_VERIFY(!vm.hayDatosPrevios());

    vm.cerrarAvisoArranque();
    QVERIFY(!vm.avisoArranqueActivo());

    // Ninguno: sin aviso.
    FakeDatosPrevios sinMarcador;
    RespaldoViewModel otro;
    otro.setDatosPrevios(&sinMarcador);
    QVERIFY(!otro.avisoArranqueActivo());
}

void TestRespaldoViewModel::catalogoSinSubidoNiRutas()
{
    using namespace catalogo;
    QStringList textos;
    for (int e = 0; e <= static_cast<int>(EstadoR::ErrorPublicacion); ++e) {
        for (int d = 0; d <= static_cast<int>(respaldo::ErrorDestino::Categoria::Interno); ++d) {
            ResultadoRespaldoPresentacion r = resultado(static_cast<EstadoR>(e));
            r.errorDestino = static_cast<respaldo::ErrorDestino::Categoria>(d);
            r.faltantes = 1;
            r.omitidos = 2;
            r.avisoRegistro = true;
            r.avisoPersistencia = true;
            const MensajeVisible m = mensajeRespaldo(r);
            textos << m.titulo << m.descripcion << advertenciasRespaldo(r);
        }
        for (int c = 0; c <= static_cast<int>(respaldo::ErrorRespaldo::Categoria::Interno); ++c) {
            ResultadoRespaldoPresentacion r = resultado(static_cast<EstadoR>(e));
            r.errorContenedor = static_cast<respaldo::ErrorRespaldo::Categoria>(c);
            textos << causaRespaldo(r);
        }
    }
    textos << avisoCopiasSinRegistro(1) << avisoCopiasSinRegistro(4);
    for (int c = 0; c <= static_cast<int>(respaldo::ErrorGestorRespaldo::Categoria::EnCurso); ++c) {
        textos << mensajeErrorGestor(static_cast<respaldo::ErrorGestorRespaldo::Categoria>(c));
    }
    for (int c = 0; c <= static_cast<int>(respaldo::ErrorRestauracion::Categoria::Interno); ++c) {
        auto error = respaldo::ErrorRestauracion::de(static_cast<respaldo::ErrorRestauracion::Categoria>(c),
                                                     QStringLiteral("diag./Users/usuario"));
        textos << mensajeErrorRestauracion(error);
        for (int k = 0; k <= static_cast<int>(respaldo::ErrorRespaldo::Categoria::Interno); ++k) {
            error.contenedor = static_cast<respaldo::ErrorRespaldo::Categoria>(k);
            textos << mensajeErrorRestauracion(error);
        }
    }
    for (int s = 0; s <= static_cast<int>(respaldo::ResultadoSolicitudRestauracion::Estado::FalloTrasDetener); ++s) {
        respaldo::ResultadoSolicitudRestauracion r;
        r.estado = static_cast<respaldo::ResultadoSolicitudRestauracion::Estado>(s);
        const MensajeVisible m = mensajeSolicitudRestauracion(r);
        textos << m.titulo << m.descripcion;
    }
    for (int s = 0; s <= static_cast<int>(ResultadoDescartePresentacion::Estado::ErrorReconciliacion); ++s) {
        ResultadoDescartePresentacion r;
        r.estado = static_cast<ResultadoDescartePresentacion::Estado>(s);
        const MensajeVisible m = mensajeDescarte(r);
        textos << m.titulo << m.descripcion;
    }
    for (int s = 0; s <= static_cast<int>(ResultadoArranquePresentacion::Estado::Revertida); ++s) {
        const MensajeVisible m = mensajeArranque(static_cast<ResultadoArranquePresentacion::Estado>(s),
                                                 QStringLiteral("1 oct 2026, 10:30"));
        textos << m.titulo << m.descripcion;
    }
    textos << avisoDatosPrevios().titulo << avisoDatosPrevios().descripcion;
    for (const QString& t : std::as_const(textos)) {
        verificarSaneado(t);
    }
    // Cada error de restauracion tiene texto.
    for (int c = 0; c <= static_cast<int>(respaldo::ErrorRestauracion::Categoria::Interno); ++c) {
        QVERIFY(!mensajeErrorRestauracion(
                     respaldo::ErrorRestauracion::de(static_cast<respaldo::ErrorRestauracion::Categoria>(c)))
                     .isEmpty());
    }
}

QTEST_MAIN(TestRespaldoViewModel)

#include "TestRespaldoViewModel.moc"
