#include "application/respaldo/GestorRespaldo.h"

#include <QMetaObject>
#include <QPromise>
#include <QThread>

#include <variant>

namespace satcfdi::respaldo {

QString claveEstable(ErrorGestorRespaldo::Categoria c)
{
    using C = ErrorGestorRespaldo::Categoria;
    switch (c) {
    case C::NoDisponible: return QStringLiteral("no_disponible");
    case C::FaltaCarpeta: return QStringLiteral("falta_carpeta");
    case C::FaltaContrasena: return QStringLiteral("falta_contrasena");
    case C::YaConfigurada: return QStringLiteral("ya_configurada");
    case C::Carpeta: return QStringLiteral("carpeta");
    case C::Almacen: return QStringLiteral("almacen");
    case C::Kdf: return QStringLiteral("kdf");
    case C::VerificacionIncorrecta: return QStringLiteral("verificacion_incorrecta");
    case C::Configuracion: return QStringLiteral("configuracion");
    case C::Detenido: return QStringLiteral("detenido");
    case C::EnCurso: return QStringLiteral("en_curso");
    }
    return QStringLiteral("configuracion");
}

namespace {

using C = ErrorGestorRespaldo::Categoria;
using Estado = ResultadoRespaldo::Estado;
using ResultadoVacio = Resultado<Exito, ErrorGestorRespaldo>;

QString claveAlmacen(ErrorAlmacenKek::Categoria c)
{
    using A = ErrorAlmacenKek::Categoria;
    switch (c) {
    case A::NoEncontrada: return QStringLiteral("no_encontrada");
    case A::Danada: return QStringLiteral("danada");
    case A::KekInvalida: return QStringLiteral("kek_invalida");
    case A::ReferenciaInvalida: return QStringLiteral("referencia_invalida");
    case A::AlmacenBloqueado: return QStringLiteral("bloqueado");
    case A::AccesoDenegado: return QStringLiteral("acceso_denegado");
    case A::AlmacenMalConfigurado: return QStringLiteral("mal_configurado");
    case A::AlmacenNoDisponible: return QStringLiteral("no_disponible");
    case A::FalloEscritura: return QStringLiteral("fallo_escritura");
    case A::Interno: return QStringLiteral("interno");
    }
    return QStringLiteral("interno");
}

QString claveCarpeta(ErrorCarpetaRespaldo::Categoria c)
{
    using K = ErrorCarpetaRespaldo::Categoria;
    switch (c) {
    case K::RutaInvalida: return QStringLiteral("ruta_invalida");
    case K::NoExiste: return QStringLiteral("no_existe");
    case K::NoEsCarpeta: return QStringLiteral("no_es_carpeta");
    case K::BookmarkInvalido: return QStringLiteral("bookmark_invalido");
    case K::NoResuelve: return QStringLiteral("no_resuelve");
    case K::EnPapelera: return QStringLiteral("en_papelera");
    case K::Interno: return QStringLiteral("interno");
    }
    return QStringLiteral("interno");
}

ErrorGestorRespaldo deAlmacen(const ErrorAlmacenKek& e)
{
    return ErrorGestorRespaldo::de(C::Almacen, QStringLiteral("kek.") + claveAlmacen(e.categoria));
}

ResultadoRespaldo resultadoPrevio(Estado estado, const QString& diagnostico,
                                  std::optional<ErrorDestino::Categoria> destino = std::nullopt)
{
    ResultadoRespaldo r;
    r.estado = estado;
    r.diagnostico = diagnostico;
    r.errorDestino = destino;
    return r;
}

bool mismaKek(const KekRespaldo& a, const KekRespaldo& b)
{
    return a.valida() && b.valida() && a.clave().igualA(b.clave().datos(), b.clave().tamano());
}

} // namespace

struct GestorRespaldo::Impl {
    GestorRespaldo* q;
    Dependencias d;

    QThread hilo;
    QObject* contexto = nullptr;
    bool cerrado = false;

    // Hilo grafico.
    EstadoRespaldoGestor espejo;
    QPointer<OSIntegration> os;

    // Hilo propio.
    ConfiguracionRespaldo cfg;
    bool cfgCargada = false;
    // Seguridad 1: la ultima lectura del ini fue integra (existe, sin error y
    // con las dos referencias de KEK validas). Solo entonces se reconcilia.
    bool cfgIntegra = false;

    Impl(GestorRespaldo* q_, Dependencias deps) : q(q_), d(std::move(deps)) {}

    template <typename T>
    QFuture<T> enHilo(std::function<T()> trabajo, T siCerrado)
    {
        if (cerrado) {
            return QtFuture::makeReadyValueFuture(std::move(siCerrado));
        }
        auto promesa = std::make_shared<QPromise<T>>();
        QFuture<T> f = promesa->future();
        QMetaObject::invokeMethod(
            contexto,
            [promesa, trabajo = std::move(trabajo)]() {
                promesa->start();
                promesa->addResult(trabajo());
                promesa->finish();
            },
            Qt::QueuedConnection);
        return f;
    }

    // --- Hilo propio --------------------------------------------------------
    void asegurarCargada()
    {
        if (cfgCargada) {
            return;
        }
        cfgCargada = true;
        auto c = d.configuracion->cargar();
        cfgIntegra = c && c.valor().integra;
        if (c) {
            cfg = std::move(c).valor().configuracion;
        }
        publicarEspejo();
    }

    // Guarda `cfg` y relee el ini (seguridad 3): el ini releido es la verdad.
    // Devuelve true si, aun con un error reportado al guardar, el ini quedo
    // con las mismas referencias de KEK y carpeta que `cfg`.
    bool persistir()
    {
        const bool guardado = static_cast<bool>(d.configuracion->guardar(cfg));
        bool coincide = guardado;
        if (auto releida = d.configuracion->cargar()) {
            const ConfiguracionRespaldo& r = releida.valor().configuracion;
            coincide = r.kekContrasena == cfg.kekContrasena && r.kekRecuperacion == cfg.kekRecuperacion
                && r.bookmark == cfg.bookmark && r.rutaCanonica == cfg.rutaCanonica
                && r.diarioActivado == cfg.diarioActivado;
            cfgIntegra = releida.valor().integra;
        } else {
            cfgIntegra = false;
        }
        publicarEspejo();
        return coincide;
    }

    // Referencias que el ini (releido ahora) contiene, para un rollback que
    // nunca borre una KEK referenciada.
    std::optional<ConfiguracionRespaldo> releer()
    {
        auto r = d.configuracion->cargar();
        if (!r) {
            return std::nullopt;
        }
        return r.valor().configuracion;
    }

    // Rollback de una KEK recien guardada: solo si el ini no la referencia.
    void borrarSiNoReferenciada(const ReferenciaKek& ref)
    {
        const auto ini = releer();
        if (ini && (ini->kekContrasena == ref || ini->kekRecuperacion == ref)) {
            return;
        }
        if (!ini) {
            return; // no se pudo comprobar: se conserva (huerfana, nunca perdida)
        }
        (void)d.almacenKek->borrar(ref);
    }

    // Copia los campos de configuracion al espejo del hilo grafico.
    void publicarEspejo()
    {
        const ConfiguracionRespaldo c = cfg;
        QMetaObject::invokeMethod(
            q,
            [this, c]() {
                espejo.cargado = true;
                espejo.carpetaConfigurada = c.carpetaConfigurada();
                espejo.rutaCarpeta = c.rutaCanonica;
                espejo.contrasenaConfigurada = c.contrasenaConfigurada();
                espejo.diarioActivado = c.diarioActivado;
                espejo.ultimoExitoUtc = c.ultimoExitoUtc;
                espejo.ultimoIntentoUtc = c.ultimoIntentoUtc;
                espejo.ultimoResultado = c.ultimoResultado;
                emit q->estadoCambiado();
            },
            Qt::QueuedConnection);
    }

    // Borra items del almacen (ambito de esta instalacion) que no son las KEK
    // referenciadas. SOLO con la ultima lectura del ini integra y las dos
    // referencias validas; en cualquier otro caso no se borra nada.
    void reconciliar()
    {
        if (!d.almacenKek || !cfgIntegra || cfg.kekContrasena.nula() || cfg.kekRecuperacion.nula()) {
            return;
        }
        auto lista = d.almacenKek->listar();
        if (!lista) {
            return;
        }
        for (const ReferenciaKek& r : lista.valor()) {
            if (r != cfg.kekContrasena && r != cfg.kekRecuperacion) {
                (void)d.almacenKek->borrar(r);
            }
        }
    }

    // Resuelve el bookmark y, si esta obsoleto o la carpeta se movio, renueva
    // y persiste la referencia.
    std::variant<ReferenciaCarpetaRespaldo, ResultadoRespaldo> resolverCarpeta()
    {
        if (!cfg.carpetaConfigurada()) {
            return resultadoPrevio(Estado::ErrorDestino, QStringLiteral("carpeta.no_configurada"),
                                   ErrorDestino::Categoria::CarpetaInexistente);
        }
        auto resuelta = d.carpetas->resolver(cfg.bookmark);
        if (!resuelta) {
            return resultadoPrevio(Estado::ErrorDestino,
                                   QStringLiteral("carpeta.") + claveCarpeta(resuelta.error().categoria),
                                   ErrorDestino::Categoria::CarpetaInexistente);
        }
        ReferenciaCarpetaRespaldo ref{resuelta.valor().rutaCanonica, cfg.bookmark, resuelta.valor().uuidVolumen};
        if (resuelta.valor().obsoleto || resuelta.valor().rutaCanonica != cfg.rutaCanonica
            || resuelta.valor().uuidVolumen != cfg.uuidVolumen) {
            if (resuelta.valor().obsoleto) {
                auto nueva = d.carpetas->crearReferencia(resuelta.valor().rutaCanonica);
                if (nueva) {
                    ref = nueva.valor();
                }
            }
            cfg.rutaCanonica = ref.rutaCanonica;
            cfg.bookmark = ref.bookmark;
            cfg.uuidVolumen = ref.uuidVolumen;
            (void)persistir();
        }
        return ref;
    }

    std::variant<std::shared_ptr<PeticionRespaldo>, ResultadoRespaldo> preparar()
    {
        asegurarCargada();
        if (!d.almacenKek || !d.carpetas || !d.fabricaDestino) {
            return resultadoPrevio(Estado::ErrorContenedor, QStringLiteral("respaldo.no_disponible"));
        }
        if (!cfg.carpetaConfigurada()) {
            return resultadoPrevio(Estado::ErrorDestino, QStringLiteral("carpeta.no_configurada"),
                                   ErrorDestino::Categoria::CarpetaInexistente);
        }
        if (!cfg.contrasenaConfigurada()) {
            return resultadoPrevio(Estado::ErrorContenedor, QStringLiteral("kek.no_configurada"));
        }
        auto carpeta = resolverCarpeta();
        if (auto* r = std::get_if<ResultadoRespaldo>(&carpeta)) {
            return *r;
        }
        const ReferenciaCarpetaRespaldo ref = std::get<ReferenciaCarpetaRespaldo>(carpeta);
        auto pw = d.almacenKek->cargar(cfg.kekContrasena);
        if (!pw) {
            return resultadoPrevio(Estado::ErrorContenedor, QStringLiteral("kek.") + claveAlmacen(pw.error().categoria));
        }
        auto rec = d.almacenKek->cargar(cfg.kekRecuperacion);
        if (!rec) {
            return resultadoPrevio(Estado::ErrorContenedor, QStringLiteral("kek.") + claveAlmacen(rec.error().categoria));
        }
        auto p = std::make_shared<PeticionRespaldo>();
        p->destino = d.fabricaDestino(ref.rutaCanonica, ref.uuidVolumen);
        p->kekContrasena = std::move(pw).valor();
        p->kekRecuperacion = std::move(rec).valor();
        return p;
    }

    // Barrera del diario: el gestor queda elegible solo despues de la limpieza
    // de `.partial` del arranque (o de saber que no aplica).
    void terminarArranque(const std::shared_ptr<QPromise<int>>& promesa, int borrados)
    {
        QMetaObject::invokeMethod(
            q,
            [this, promesa, borrados]() {
                espejo.arranqueCompleto = true;
                promesa->addResult(borrados);
                promesa->finish();
                emit q->estadoCambiado();
            },
            Qt::QueuedConnection);
    }

    void registrar(const ResultadoRespaldo& r)
    {
        const QDateTime ahora = d.reloj().toUTC();
        cfg.ultimoIntentoUtc = ahora;
        cfg.ultimoResultado = claveEstable(r.estado);
        if (r.exito()) {
            cfg.ultimoExitoUtc = ahora;
        }
        (void)persistir();
    }
};

GestorRespaldo::GestorRespaldo(Dependencias dependencias, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(this, std::move(dependencias)))
{
    m_impl->hilo.setObjectName(QStringLiteral("satcfdi-respaldo-config"));
    m_impl->contexto = new QObject;
    m_impl->contexto->moveToThread(&m_impl->hilo);
    QObject::connect(&m_impl->hilo, &QThread::finished, m_impl->contexto, &QObject::deleteLater);
    m_impl->hilo.start();
}

GestorRespaldo::~GestorRespaldo()
{
    cerrar();
}

void GestorRespaldo::cerrar()
{
    if (m_impl->cerrado) {
        return;
    }
    m_impl->cerrado = true;
    // Vacia la cola (ningun trabajo espera al hilo grafico) y une el hilo.
    QMetaObject::invokeMethod(m_impl->contexto, [] {}, Qt::BlockingQueuedConnection);
    m_impl->hilo.quit();
    m_impl->hilo.wait();
}

void GestorRespaldo::fijarOS(OSIntegration* os)
{
    m_impl->os = os;
}

EstadoRespaldoGestor GestorRespaldo::estado() const
{
    return m_impl->espejo;
}

bool GestorRespaldo::enCurso() const
{
    return m_impl->espejo.enCurso;
}

void GestorRespaldo::fijarPendienteDiario(EstadoRespaldoGestor::PendienteDiario pendiente)
{
    if (m_impl->espejo.pendienteDiario != pendiente) {
        m_impl->espejo.pendienteDiario = pendiente;
        emit estadoCambiado();
    }
}

QFuture<int> GestorRespaldo::iniciar()
{
    Impl* impl = m_impl.get();
    auto promesa = std::make_shared<QPromise<int>>();
    QFuture<int> f = promesa->future();
    if (impl->cerrado) {
        return QtFuture::makeReadyValueFuture(-1);
    }
    QMetaObject::invokeMethod(
        impl->contexto,
        [impl, promesa]() {
            promesa->start();
            impl->asegurarCargada();
            impl->reconciliar();
            std::shared_ptr<DestinoRespaldo> destino;
            if (impl->d.carpetas && impl->d.fabricaDestino && impl->cfg.carpetaConfigurada()) {
                auto carpeta = impl->resolverCarpeta();
                if (const auto* ref = std::get_if<ReferenciaCarpetaRespaldo>(&carpeta)) {
                    destino = impl->d.fabricaDestino(ref->rutaCanonica, ref->uuidVolumen);
                }
            }
            if (!destino || !impl->d.servicio) {
                impl->terminarArranque(promesa, -1);
                return;
            }
            // limpiarPartialPropios() se pide desde el hilo grafico.
            QMetaObject::invokeMethod(
                impl->q,
                [impl, promesa, destino]() {
                    impl->d.servicio->limpiarPartialPropios(destino).then(impl->contexto, [impl, promesa](int n) {
                        impl->terminarArranque(promesa, n);
                    });
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    return f;
}

QFuture<Resultado<Exito, ErrorGestorRespaldo>> GestorRespaldo::configurarCarpeta(const QString& rutaElegida)
{
    Impl* impl = m_impl.get();
    return impl->enHilo<ResultadoVacio>(
        [impl, rutaElegida]() {
            impl->asegurarCargada();
            if (!impl->d.carpetas) {
                return ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::NoDisponible, QStringLiteral("carpeta.no_disponible")));
            }
            auto ref = impl->d.carpetas->crearReferencia(rutaElegida);
            if (!ref) {
                return ResultadoVacio::fallo(ErrorGestorRespaldo::de(
                    C::Carpeta, QStringLiteral("carpeta.") + claveCarpeta(ref.error().categoria)));
            }
            impl->cfg.rutaCanonica = ref.valor().rutaCanonica;
            impl->cfg.bookmark = ref.valor().bookmark;
            impl->cfg.uuidVolumen = ref.valor().uuidVolumen;
            if (!impl->persistir()) {
                return ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::Configuracion, QStringLiteral("configuracion.escribir")));
            }
            return ResultadoVacio::exito(Exito{});
        },
        ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::Detenido)));
}

QFuture<Resultado<std::shared_ptr<BufferSecreto>, ErrorGestorRespaldo>>
GestorRespaldo::configurarContrasena(std::shared_ptr<BufferSecreto> contrasena)
{
    using R = Resultado<std::shared_ptr<BufferSecreto>, ErrorGestorRespaldo>;
    Impl* impl = m_impl.get();
    return impl->enHilo<R>(
        [impl, contrasena]() {
            impl->asegurarCargada();
            if (!impl->d.almacenKek || !impl->d.contenedor) {
                return R::fallo(ErrorGestorRespaldo::de(C::NoDisponible, QStringLiteral("kek.no_disponible")));
            }
            if (impl->cfg.contrasenaConfigurada()) {
                return R::fallo(ErrorGestorRespaldo::de(C::YaConfigurada, QStringLiteral("kek.ya_configurada")));
            }
            ContenedorRespaldo& c = *impl->d.contenedor;
            if (!contrasena || contrasena->vacio()) {
                return R::fallo(ErrorGestorRespaldo::de(C::Kdf, QStringLiteral("contrasena.vacia")));
            }
            if (!c.comprobarDisponible()) {
                return R::fallo(ErrorGestorRespaldo::de(C::Kdf, QStringLiteral("kdf.argon2id")));
            }
            auto sal = c.generarSal();
            if (!sal) {
                return R::fallo(ErrorGestorRespaldo::de(C::Kdf, QStringLiteral("kdf.sal")));
            }
            auto kekPw = c.derivarKekContrasena(*contrasena, ParametrosKdf::predeterminados(sal.valor()));
            auto clave = c.generarClaveRecuperacion();
            if (!kekPw || !clave) {
                return R::fallo(ErrorGestorRespaldo::de(C::Kdf, QStringLiteral("kdf.derivar")));
            }
            auto kekRec = c.derivarKekRecuperacion(clave.valor());
            auto texto = c.formatearClaveRecuperacion(clave.valor());
            if (!kekRec || !texto) {
                return R::fallo(ErrorGestorRespaldo::de(C::Kdf, QStringLiteral("kdf.recuperacion")));
            }
            auto refPw = impl->d.almacenKek->guardar(kekPw.valor());
            if (!refPw) {
                return R::fallo(deAlmacen(refPw.error()));
            }
            auto refRec = impl->d.almacenKek->guardar(kekRec.valor());
            if (!refRec) {
                (void)impl->d.almacenKek->borrar(refPw.valor());
                return R::fallo(deAlmacen(refRec.error()));
            }
            impl->cfg.kekContrasena = refPw.valor();
            impl->cfg.kekRecuperacion = refRec.valor();
            if (!impl->persistir()) {
                impl->borrarSiNoReferenciada(refPw.valor());
                impl->borrarSiNoReferenciada(refRec.valor());
                impl->cfg.kekContrasena = ReferenciaKek();
                impl->cfg.kekRecuperacion = ReferenciaKek();
                impl->publicarEspejo();
                return R::fallo(ErrorGestorRespaldo::de(C::Configuracion, QStringLiteral("configuracion.escribir")));
            }
            return R::exito(std::make_shared<BufferSecreto>(std::move(texto).valor()));
        },
        R::fallo(ErrorGestorRespaldo::de(C::Detenido)));
}

QFuture<Resultado<Exito, ErrorGestorRespaldo>> GestorRespaldo::cambiarContrasena(std::shared_ptr<BufferSecreto> nueva,
                                                                                  VerificacionCambio verificacion)
{
    Impl* impl = m_impl.get();
    // Seguridad 2: no se cambia la KEK mientras un respaldo la usa.
    if (impl->espejo.enCurso || (impl->d.servicio && impl->d.servicio->enCurso())) {
        return QtFuture::makeReadyValueFuture(
            ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::EnCurso, QStringLiteral("ya_en_curso"))));
    }
    return impl->enHilo<ResultadoVacio>(
        [impl, nueva, verificacion]() {
            impl->asegurarCargada();
            if (!impl->d.almacenKek || !impl->d.contenedor) {
                return ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::NoDisponible, QStringLiteral("kek.no_disponible")));
            }
            if (!impl->cfg.contrasenaConfigurada()) {
                return ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::FaltaContrasena, QStringLiteral("kek.no_configurada")));
            }
            if (!nueva || nueva->vacio() || !verificacion.texto) {
                return ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::Kdf, QStringLiteral("contrasena.vacia")));
            }
            ContenedorRespaldo& c = *impl->d.contenedor;
            AlmacenKekRespaldo& almacen = *impl->d.almacenKek;

            // 1. Verificar.
            bool verificada = false;
            if (verificacion.tipo == VerificacionCambio::Tipo::ContrasenaAnterior) {
                auto guardada = almacen.cargar(impl->cfg.kekContrasena);
                if (!guardada) {
                    return ResultadoVacio::fallo(deAlmacen(guardada.error()));
                }
                auto derivada = c.derivarKekContrasena(*verificacion.texto, guardada.valor().parametros());
                verificada = derivada && mismaKek(derivada.valor(), guardada.valor());
            } else {
                auto guardada = almacen.cargar(impl->cfg.kekRecuperacion);
                if (!guardada) {
                    return ResultadoVacio::fallo(deAlmacen(guardada.error()));
                }
                auto clave = c.leerClaveRecuperacion(*verificacion.texto);
                if (clave) {
                    auto derivada = c.derivarKekRecuperacion(clave.valor());
                    verificada = derivada && mismaKek(derivada.valor(), guardada.valor());
                }
            }
            if (!verificada) {
                return ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::VerificacionIncorrecta, QStringLiteral("verificacion")));
            }

            // 2. KEK nueva con sal nueva (D12); la de recuperacion no cambia (D16).
            auto sal = c.generarSal();
            if (!sal) {
                return ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::Kdf, QStringLiteral("kdf.sal")));
            }
            auto kek = c.derivarKekContrasena(*nueva, ParametrosKdf::predeterminados(sal.valor()));
            if (!kek) {
                return ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::Kdf, QStringLiteral("kdf.derivar")));
            }
            auto ref = almacen.guardar(kek.valor());
            if (!ref) {
                return ResultadoVacio::fallo(deAlmacen(ref.error()));
            }
            // 3. Persistir la referencia nueva y solo despues borrar la anterior.
            const ReferenciaKek anterior = impl->cfg.kekContrasena;
            impl->cfg.kekContrasena = ref.valor();
            if (!impl->persistir()) {
                impl->cfg.kekContrasena = anterior;
                impl->borrarSiNoReferenciada(ref.valor());
                impl->publicarEspejo();
                return ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::Configuracion, QStringLiteral("configuracion.escribir")));
            }
            (void)almacen.borrar(anterior); // si falla, queda huerfana y se reconcilia
            impl->reconciliar();
            return ResultadoVacio::exito(Exito{});
        },
        ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::Detenido)));
}

QFuture<Resultado<Exito, ErrorGestorRespaldo>> GestorRespaldo::activarDiario(bool activado)
{
    Impl* impl = m_impl.get();
    return impl->enHilo<ResultadoVacio>(
        [impl, activado]() {
            impl->asegurarCargada();
            impl->cfg.diarioActivado = activado;
            if (!impl->persistir()) {
                return ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::Configuracion, QStringLiteral("configuracion.escribir")));
            }
            return ResultadoVacio::exito(Exito{});
        },
        ResultadoVacio::fallo(ErrorGestorRespaldo::de(C::Detenido)));
}

QFuture<ResultadoRespaldo> GestorRespaldo::respaldarAhora()
{
    Impl* impl = m_impl.get();
    if (impl->cerrado) {
        return QtFuture::makeReadyValueFuture(resultadoPrevio(Estado::Detenido, QStringLiteral("detenido")));
    }
    if (impl->espejo.enCurso || (impl->d.servicio && impl->d.servicio->enCurso())) {
        return QtFuture::makeReadyValueFuture(resultadoPrevio(Estado::YaEnCurso, QStringLiteral("ya_en_curso")));
    }
    impl->espejo.enCurso = true; // D18: rafagas y peticiones simultaneas no duplican
    emit estadoCambiado();
    std::shared_ptr<OSIntegration::ActividadSistema> actividad;
    if (impl->os) {
        actividad = impl->os->iniciarActividadSistema(OSIntegration::MotivoActividad::Respaldo);
    }

    auto promesa = std::make_shared<QPromise<ResultadoRespaldo>>();
    QFuture<ResultadoRespaldo> futuro = promesa->future();
    promesa->start();
    // Termina en el hilo grafico: suelta la actividad y el indicador.
    auto terminar = [impl, promesa, actividad](ResultadoRespaldo r) mutable {
        QMetaObject::invokeMethod(
            impl->q,
            [impl, promesa, actividad, r]() mutable {
                actividad.reset();
                impl->espejo.enCurso = false;
                promesa->addResult(r);
                promesa->finish();
                emit impl->q->estadoCambiado();
            },
            Qt::QueuedConnection);
    };
    QMetaObject::invokeMethod(
        impl->contexto,
        [impl, terminar]() mutable {
            auto preparado = impl->preparar();
            if (auto* r = std::get_if<ResultadoRespaldo>(&preparado)) {
                impl->registrar(*r);
                terminar(*r);
                return;
            }
            auto peticion = std::get<std::shared_ptr<PeticionRespaldo>>(preparado);
            // RespaldoService::respaldar() se llama desde el hilo grafico.
            QMetaObject::invokeMethod(
                impl->q,
                [impl, peticion, terminar]() mutable {
                    if (!impl->d.servicio) {
                        terminar(resultadoPrevio(Estado::ErrorContenedor, QStringLiteral("respaldo.no_disponible")));
                        return;
                    }
                    impl->d.servicio->respaldar(std::move(*peticion))
                        .then(impl->contexto, [impl, terminar](ResultadoRespaldo r) mutable {
                            impl->registrar(r);
                            terminar(r);
                        });
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    return futuro;
}

} // namespace satcfdi::respaldo
