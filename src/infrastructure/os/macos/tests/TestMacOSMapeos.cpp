// Pruebas de los mapeos puros de MacOSIntegration (T004, corte C). Sin
// AppKit, permisos, bundle firmado ni sesion grafica.

#include "infrastructure/os/macos/IconoMenuBar.h"
#include "infrastructure/os/macos/MacOSMapeos.h"
#include "infrastructure/os/MenuBarDefinicion.h"

#include <QImage>
#include <QTest>

using namespace satcfdi::macos;
using LoginItemStatus = satcfdi::OSIntegration::LoginItemStatus;
using NotificationStatus = satcfdi::OSIntegration::NotificationStatus;
using NotificationSendResult = satcfdi::OSIntegration::NotificationSendResult;
using LaunchContext = satcfdi::OSIntegration::LaunchContext;

class TestMacOSMapeos : public QObject {
    Q_OBJECT

private slots:
    void estadoLoginItem()
    {
        QCOMPARE(mapearEstadoLoginItem(kSmStatusNotRegistered), LoginItemStatus::Disabled);
        QCOMPARE(mapearEstadoLoginItem(kSmStatusNotFound), LoginItemStatus::Disabled);
        QCOMPARE(mapearEstadoLoginItem(kSmStatusEnabled), LoginItemStatus::Enabled);
        QCOMPARE(mapearEstadoLoginItem(kSmStatusRequiresApproval), LoginItemStatus::RequiresApproval);
        QCOMPARE(mapearEstadoLoginItem(42), LoginItemStatus::Unavailable);
        QCOMPARE(mapearEstadoLoginItem(-1), LoginItemStatus::Unavailable);
    }

    void resultadoLoginItem()
    {
        // Exito o ya en el estado pedido: manda el status leido.
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::Ninguno, kSmStatusEnabled), LoginItemStatus::Enabled);
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::Ninguno, kSmStatusRequiresApproval),
                 LoginItemStatus::RequiresApproval);
        QCOMPARE(mapearResultadoLoginItem(false, ErrorLoginItem::Ninguno, kSmStatusNotRegistered),
                 LoginItemStatus::Disabled);
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::YaEnEstadoSolicitado, kSmStatusEnabled),
                 LoginItemStatus::Enabled);
        QCOMPARE(mapearResultadoLoginItem(false, ErrorLoginItem::YaEnEstadoSolicitado, kSmStatusNotFound),
                 LoginItemStatus::Disabled);
        // Rechazo y no disponible ganan sobre el status.
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::RechazadoPorUsuario, kSmStatusRequiresApproval),
                 LoginItemStatus::Rejected);
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::NoDisponible, kSmStatusNotFound),
                 LoginItemStatus::Unavailable);
        // Otro error al habilitar sin quedar habilitado -> Rejected.
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::Otro, kSmStatusNotRegistered),
                 LoginItemStatus::Rejected);
        QCOMPARE(mapearResultadoLoginItem(true, ErrorLoginItem::Otro, kSmStatusRequiresApproval),
                 LoginItemStatus::RequiresApproval);
        // Otro error al deshabilitar: el status leido.
        QCOMPARE(mapearResultadoLoginItem(false, ErrorLoginItem::Otro, kSmStatusEnabled), LoginItemStatus::Enabled);
    }

    void autorizacionNotificaciones()
    {
        QCOMPARE(mapearAutorizacionNotificaciones(kUnAuthNotDetermined), NotificationStatus::NotDetermined);
        QCOMPARE(mapearAutorizacionNotificaciones(kUnAuthDenied), NotificationStatus::Denied);
        QCOMPARE(mapearAutorizacionNotificaciones(kUnAuthAuthorized), NotificationStatus::Granted);
        QCOMPARE(mapearAutorizacionNotificaciones(kUnAuthProvisional), NotificationStatus::Granted);
        QCOMPARE(mapearAutorizacionNotificaciones(kUnAuthEphemeral), NotificationStatus::Granted);
        QCOMPARE(mapearAutorizacionNotificaciones(99), NotificationStatus::Unavailable);
    }

    void resultadoEnvio()
    {
        QCOMPARE(resultadoEnvioSegunEstado(NotificationStatus::Granted), NotificationSendResult::Sent);
        QCOMPARE(resultadoEnvioSegunEstado(NotificationStatus::Denied), NotificationSendResult::PermissionDenied);
        QCOMPARE(resultadoEnvioSegunEstado(NotificationStatus::NotDetermined),
                 NotificationSendResult::PermissionNotDetermined);
        QCOMPARE(resultadoEnvioSegunEstado(NotificationStatus::Unavailable), NotificationSendResult::Unavailable);
    }

    void contextoDeArranque()
    {
        QCOMPARE(launchContextDesdeEventoApertura(true, kAeClaseCore, kAeAbrirAplicacion, kAeLanzadoComoLoginItem),
                 LaunchContext::LoginItem);
        QCOMPARE(launchContextDesdeEventoApertura(true, kAeClaseCore, kAeAbrirAplicacion, 0), LaunchContext::Manual);
        QCOMPARE(launchContextDesdeEventoApertura(false, kAeClaseCore, kAeAbrirAplicacion, kAeLanzadoComoLoginItem),
                 LaunchContext::Manual);
        // 'odoc' (abrir documentos) o 'lgsi' (service item) no son Login Item.
        QCOMPARE(launchContextDesdeEventoApertura(true, kAeClaseCore, 0x6F646F63, kAeLanzadoComoLoginItem),
                 LaunchContext::Manual);
        QCOMPARE(launchContextDesdeEventoApertura(true, kAeClaseCore, kAeAbrirAplicacion, 0x6C677369),
                 LaunchContext::Manual);
    }

    void textosDistinguenEstados()
    {
        QStringList textos;
        for (auto e : {LoginItemStatus::Disabled, LoginItemStatus::Enabled, LoginItemStatus::RequiresApproval,
                       LoginItemStatus::Rejected, LoginItemStatus::Unavailable}) {
            textos << textoEstadoLoginItem(e);
        }
        textos.removeDuplicates();
        QCOMPARE(textos.size(), 5);

        QStringList notif;
        for (auto e : {NotificationStatus::NotDetermined, NotificationStatus::Granted, NotificationStatus::Denied,
                       NotificationStatus::Unavailable}) {
            notif << textoEstadoNotificaciones(e);
        }
        notif.removeDuplicates();
        QCOMPARE(notif.size(), 4);

        QVERIFY(textoAccionMonitoreo(false) != textoAccionMonitoreo(true));
    }

    // T012 D6: definicion compartida del menu (orden, separadores, textos y
    // reglas de visibilidad que antes vivian en MacOSIntegration.mm).
    void definicionMenuBar()
    {
        using namespace satcfdi::menubar;
        EstadoMenu estado;
        estado.monitoreo.fase = satcfdi::OSIntegration::EstadoMonitoreo::Fase::ActivoEnEspera;
        QStringList visibles;
        for (const Entrada& e : entradas(estado)) {
            if (e.visible) {
                visibles.append(e.separador ? QStringLiteral("---") : e.texto);
            }
        }
        QCOMPARE(visibles, (QStringList{
                               QStringLiteral("Mostrar ventana"), QStringLiteral("Nueva solicitud…"),
                               QStringLiteral("Abrir carpeta de paquetes"), QStringLiteral("---"),
                               QStringLiteral("Monitoreo activo"), QStringLiteral("Pausar monitoreo"),
                               QStringLiteral("---"), QStringLiteral("Abrir al iniciar sesión"),
                               textoEstadoLoginItem(satcfdi::OSIntegration::LoginItemStatus::Disabled),
                               QStringLiteral("---"),
                               textoEstadoNotificaciones(satcfdi::OSIntegration::NotificationStatus::NotDetermined),
                               QStringLiteral("Solicitar permiso de notificaciones"), QStringLiteral("---"),
                               QStringLiteral("Salir de SAT CFDI Downloader")}));
        QVERIFY(!entrada(Id::EstadoMonitoreo, estado).habilitada);
        estado.monitoreo.pendientes = 2;
        estado.monitoreoPausado = true;
        estado.preferenciaLoginItem = true;
        estado.loginItem = satcfdi::OSIntegration::LoginItemStatus::RequiresApproval;
        estado.notificaciones = satcfdi::OSIntegration::NotificationStatus::Denied;
        QCOMPARE(entrada(Id::PendientesMonitoreo, estado).texto, QStringLiteral("Pendientes: 2"));
        QVERIFY(entrada(Id::PendientesMonitoreo, estado).visible);
        QCOMPARE(entrada(Id::AccionMonitoreo, estado).texto, QStringLiteral("Reanudar monitoreo"));
        QVERIFY(entrada(Id::InicioAutomatico, estado).marcada);
        QVERIFY(entrada(Id::AbrirAjustesLoginItem, estado).visible);
        QVERIFY(!entrada(Id::SolicitarPermiso, estado).visible);
        QVERIFY(entrada(Id::EnviarPrueba, estado).visible);
        QVERIFY(entrada(Id::AbrirAjustesNotificaciones, estado).visible);
        // Textos que abren ventana o dialogo llevan "…" (UX-37).
        QCOMPARE(entrada(Id::AbrirAjustesLoginItem, estado).texto, QStringLiteral("Abrir ajustes de inicio de sesión…"));
        QCOMPARE(entrada(Id::EnviarPrueba, estado).texto, QStringLiteral("Enviar notificación de prueba"));
        QCOMPARE(entrada(Id::AbrirAjustesNotificaciones, estado).texto,
                 QStringLiteral("Abrir ajustes de notificaciones…"));
        QCOMPARE(entrada(Id::EstadoLoginItem, estado).texto, QStringLiteral("Estado en macOS: pendiente de aprobación"));
    }

    // T013 D6/UX-37: ningun texto del menu usa "..." ASCII ni palabras sin
    // acento conocidas, en ningun estado.
    void ortografiaMenuBar()
    {
        using namespace satcfdi::menubar;
        using E = satcfdi::OSIntegration::EstadoMonitoreo;
        using L = satcfdi::OSIntegration::LoginItemStatus;
        using N = satcfdi::OSIntegration::NotificationStatus;
        const QStringList prohibidas{QStringLiteral("..."), QStringLiteral("sesion"), QStringLiteral("notificacion "),
                                     QStringLiteral("aprobacion"), QStringLiteral("pausado")};
        for (E::Fase f : {E::Fase::Pausado, E::Fase::ActivoEnEspera, E::Fase::Ejecutando, E::Fase::Deteniendo,
                          E::Fase::Detenido}) {
            for (E::Actividad a : {E::Actividad::Ninguna, E::Actividad::Enviando, E::Actividad::Verificando,
                                   E::Actividad::Descargando, E::Actividad::Otra}) {
                for (L l : {L::Disabled, L::Enabled, L::RequiresApproval, L::Rejected, L::Unavailable}) {
                    for (N n : {N::NotDetermined, N::Granted, N::Denied, N::Unavailable}) {
                        EstadoMenu s;
                        s.monitoreo.fase = f;
                        s.monitoreo.actividad = a;
                        s.monitoreo.pendientes = 1;
                        s.loginItem = l;
                        s.notificaciones = n;
                        for (const Entrada& e : entradas(s)) {
                            for (const QString& p : prohibidas) {
                                QVERIFY2(!(e.texto + QLatin1Char(' ')).contains(p), qPrintable(e.texto));
                            }
                        }
                    }
                }
            }
        }
    }

    // UX-38: iconos de las lineas informativas y existencia de los SVG.
    void iconosMenuBar()
    {
        using namespace satcfdi::menubar;
        using F = satcfdi::OSIntegration::EstadoMonitoreo::Fase;
        EstadoMenu s;
        s.monitoreo.pendientes = 3;
        s.monitoreo.fase = F::ActivoEnEspera;
        QCOMPARE(entrada(Id::EstadoMonitoreo, s).icono, QStringLiteral("check-circle"));
        s.monitoreo.fase = F::Pausado;
        QCOMPARE(entrada(Id::EstadoMonitoreo, s).icono, QStringLiteral("clock"));
        s.monitoreo.fase = F::Ejecutando;
        QCOMPARE(entrada(Id::EstadoMonitoreo, s).icono, QStringLiteral("arrow-down-circle-dotted"));
        s.monitoreo.fase = F::Detenido;
        QVERIFY(entrada(Id::EstadoMonitoreo, s).icono.isEmpty());
        QCOMPARE(entrada(Id::PendientesMonitoreo, s).icono, QStringLiteral("clock"));
        // Las acciones no llevan icono.
        for (const Entrada& e : entradas(s)) {
            if (e.habilitada && !e.separador) {
                QVERIFY2(e.icono.isEmpty(), qPrintable(e.texto));
            }
        }
        for (const char* n : {"check-circle", "clock", "arrow-down-circle-dotted"}) {
            const QString ruta = QFINDTESTDATA(QStringLiteral("../../../../presentation/qml/assets/icons/%1.svg")
                                                   .arg(QLatin1String(n)));
            QVERIFY2(!ruta.isEmpty(), n);
        }
    }

    // T007 D2: textos de la linea de estado del worker.
    void textosEstadoMonitoreo()
    {
        using E = satcfdi::OSIntegration::EstadoMonitoreo;
        auto texto = [](E::Fase f, E::Actividad a = E::Actividad::Ninguna) {
            E e;
            e.fase = f;
            e.actividad = a;
            return textoEstadoMonitoreo(e);
        };
        QCOMPARE(texto(E::Fase::ActivoEnEspera), QStringLiteral("Monitoreo activo"));
        QCOMPARE(texto(E::Fase::Pausado), QStringLiteral("Monitoreo en pausa"));
        QCOMPARE(texto(E::Fase::Ejecutando, E::Actividad::Enviando), QStringLiteral("Trabajando: enviando…"));
        QCOMPARE(texto(E::Fase::Ejecutando, E::Actividad::Verificando), QStringLiteral("Trabajando: verificando…"));
        QCOMPARE(texto(E::Fase::Ejecutando, E::Actividad::Descargando), QStringLiteral("Trabajando: descargando…"));
        QCOMPARE(texto(E::Fase::Ejecutando, E::Actividad::Otra), QStringLiteral("Trabajando…"));
        QVERIFY(!texto(E::Fase::Deteniendo).isEmpty());
        QVERIFY(!texto(E::Fase::Detenido).isEmpty());
        QCOMPARE(textoPendientes(0), QString());
        QCOMPARE(textoPendientes(3), QStringLiteral("Pendientes: 3"));
    }

    // --- T014.4 D1: notificaciones accionables ---------------------------

    void uuidCanonico()
    {
        QVERIFY(esUuidCanonico(QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e")));
        QVERIFY(!esUuidCanonico(QString()));
        QVERIFY(!esUuidCanonico(QStringLiteral("0F8FAD5B-D9CB-469F-A165-70867728950E"))); // mayusculas
        QVERIFY(!esUuidCanonico(QStringLiteral("{0f8fad5b-d9cb-469f-a165-70867728950e}"))); // llaves
        QVERIFY(!esUuidCanonico(QStringLiteral("0f8fad5bd9cb469fa16570867728950e")));       // sin guiones
        QVERIFY(!esUuidCanonico(QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950g")));   // no hex
        QVERIFY(!esUuidCanonico(QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e ")));  // espacio
        QVERIFY(!esUuidCanonico(QStringLiteral("XAXX010101000")));                         // RFC
        QVERIFY(!esUuidCanonico(QStringLiteral("/Users/x/paquetes/a.zip")));                // ruta
    }

    void destinoDeUserInfo()
    {
        using D = satcfdi::OSIntegration::DestinoNotificacion;
        const QString id = QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e");
        QCOMPARE(destinoDesdeUserInfo(QStringLiteral("solicitud"), id), (D{D::Tipo::Solicitud, id}));
        QCOMPARE(destinoDesdeUserInfo(QStringLiteral("perfil"), id), (D{D::Tipo::Perfil, id}));
        QCOMPARE(destinoDesdeUserInfo(QStringLiteral("rfc"), id), D{});
        QCOMPARE(destinoDesdeUserInfo(QStringLiteral("Solicitud"), id), D{});
        QCOMPARE(destinoDesdeUserInfo(QString(), id), D{});
        QCOMPARE(destinoDesdeUserInfo(QStringLiteral("solicitud"), QStringLiteral("XAXX010101000")), D{});
        QCOMPARE(destinoDesdeUserInfo(QStringLiteral("perfil"), QString()), D{});

        // Ida y vuelta.
        for (D::Tipo t : {D::Tipo::Solicitud, D::Tipo::Perfil}) {
            const D d{t, id};
            QVERIFY(destinoTransportable(d));
            QCOMPARE(destinoDesdeUserInfo(claveTipoDestino(t), id), d);
        }
        QVERIFY(!destinoTransportable(D{}));
        QVERIFY(!destinoTransportable(D{D::Tipo::Ninguno, id}));
        QVERIFY(!destinoTransportable(D{D::Tipo::Solicitud, QStringLiteral("{") + id + QStringLiteral("}")}));
        QVERIFY(claveTipoDestino(D::Tipo::Ninguno).isEmpty());
    }

    void categoriaPorAcciones()
    {
        using D = satcfdi::OSIntegration::DestinoNotificacion;
        using A = satcfdi::OSIntegration::AccionNotificacion;
        const QString id = QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e");
        const D solicitud{D::Tipo::Solicitud, id};
        const D perfil{D::Tipo::Perfil, id};
        QCOMPARE(categoriaNotificacion(solicitud, {A::MostrarEnFinder}), QLatin1String(kCategoriaMostrarEnFinder));
        QCOMPARE(categoriaNotificacion(perfil, {A::AbrirPerfiles}), QLatin1String(kCategoriaAbrirPerfiles));
        QVERIFY(categoriaNotificacion(solicitud, {}).isEmpty());
        QVERIFY(categoriaNotificacion(perfil, {}).isEmpty());
        // Accion que no corresponde al destino o destino no transportable.
        QVERIFY(categoriaNotificacion(perfil, {A::MostrarEnFinder}).isEmpty());
        QVERIFY(categoriaNotificacion(solicitud, {A::AbrirPerfiles}).isEmpty());
        QVERIFY(categoriaNotificacion(D{}, {A::MostrarEnFinder}).isEmpty());
        QVERIFY(categoriaNotificacion(D{D::Tipo::Solicitud, QStringLiteral("x")}, {A::MostrarEnFinder}).isEmpty());
        QCOMPARE(textoAccionNotificacion(A::MostrarEnFinder), QStringLiteral("Mostrar en Finder"));
        QCOMPARE(textoAccionNotificacion(A::AbrirPerfiles), QStringLiteral("Abrir Perfiles SAT"));
        QVERIFY(textoAccionNotificacion(A::Abrir).isEmpty());
    }

    void respuestaANotificacion()
    {
        using D = satcfdi::OSIntegration::DestinoNotificacion;
        using A = satcfdi::OSIntegration::AccionNotificacion;
        const QString id = QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e");
        const QString sol = QStringLiteral("solicitud");
        const QString per = QStringLiteral("perfil");
        const QString finder = QLatin1String(kAccionMostrarEnFinder);
        const QString perfiles = QLatin1String(kAccionAbrirPerfiles);
        using R = ActivacionNotificacion;

        // Pulsar el cuerpo -> Abrir (el identificador se ignora).
        QCOMPARE(activacionDesdeRespuesta(sol, id, true, QStringLiteral("x")), (R{D{D::Tipo::Solicitud, id}, A::Abrir}));
        QCOMPARE(activacionDesdeRespuesta(per, id, true, QString()), (R{D{D::Tipo::Perfil, id}, A::Abrir}));
        // Botones.
        QCOMPARE(activacionDesdeRespuesta(sol, id, false, finder), (R{D{D::Tipo::Solicitud, id}, A::MostrarEnFinder}));
        QCOMPARE(activacionDesdeRespuesta(per, id, false, perfiles), (R{D{D::Tipo::Perfil, id}, A::AbrirPerfiles}));
        // Boton que no corresponde al destino -> Abrir con ese destino.
        QCOMPARE(activacionDesdeRespuesta(per, id, false, finder), (R{D{D::Tipo::Perfil, id}, A::Abrir}));
        QCOMPARE(activacionDesdeRespuesta(sol, id, false, perfiles), (R{D{D::Tipo::Solicitud, id}, A::Abrir}));
        // Destino invalido o ausente -> Ninguno + Abrir, sea cual sea el boton.
        QCOMPARE(activacionDesdeRespuesta(QString(), QString(), true, QString()), (R{D{}, A::Abrir}));
        QCOMPARE(activacionDesdeRespuesta(sol, QStringLiteral("XAXX010101000"), false, finder), (R{D{}, A::Abrir}));
        QCOMPARE(activacionDesdeRespuesta(QStringLiteral("otro"), id, false, perfiles), (R{D{}, A::Abrir}));
        // Descartar / desconocido -> nada.
        QVERIFY(!activacionDesdeRespuesta(sol, id, false, QStringLiteral("com.apple.UNNotificationDismissActionIdentifier")));
        QVERIFY(!activacionDesdeRespuesta(sol, id, false, QString()));
        QVERIFY(!activacionDesdeRespuesta(sol, id, false, QStringLiteral("mx.adenium.satcfdi.accion.otra")));
    }

    // --- T014.4 D2: icono del menu bar -----------------------------------

    void textosIcono()
    {
        using E = satcfdi::OSIntegration::EstadoIcono;
        QCOMPARE(textoEstadoIcono(E::Normal), QStringLiteral("SAT CFDI Downloader"));
        QCOMPARE(textoEstadoIcono(E::Trabajando), QStringLiteral("SAT CFDI Downloader: trabajando"));
        QCOMPARE(textoEstadoIcono(E::Pausado), QStringLiteral("SAT CFDI Downloader: en pausa"));
        QCOMPARE(textoEstadoIcono(E::Atencion), QStringLiteral("SAT CFDI Downloader: requiere atención"));
    }

    void variantesIcono()
    {
        using E = satcfdi::OSIntegration::EstadoIcono;
        const QList<E> estados{E::Normal, E::Pausado, E::Trabajando, E::Atencion};
        for (const int lado : {18, 36}) {
            QList<QImage> imagenes;
            for (E e : estados) {
                const QImage img = dibujarIconoMenuBar(e, lado);
                QCOMPARE(img.size(), QSize(lado, lado));
                QVERIFY(img.hasAlphaChannel());
                // Plantilla: solo negro con alfa (el color lo pone macOS) y no vacia.
                bool tieneTinta = false;
                for (int y = 0; y < lado; ++y) {
                    for (int x = 0; x < lado; ++x) {
                        const QColor c = img.pixelColor(x, y);
                        if (c.alpha() > 0) {
                            tieneTinta = true;
                            QCOMPARE(c.red() + c.green() + c.blue(), 0);
                        }
                    }
                }
                QVERIFY(tieneTinta);
                imagenes.append(img);
            }
            // Las cuatro variantes son distinguibles entre si.
            for (int i = 0; i < imagenes.size(); ++i) {
                for (int j = i + 1; j < imagenes.size(); ++j) {
                    QVERIFY2(imagenes.at(i) != imagenes.at(j), qPrintable(QStringLiteral("%1 vs %2").arg(i).arg(j)));
                }
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestMacOSMapeos)
#include "TestMacOSMapeos.moc"
