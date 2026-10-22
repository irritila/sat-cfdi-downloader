#include "application/respaldo/RespaldoService.h"

#include <QCryptographicHash>
#include <QHash>
#include <QIODevice>
#include <QMetaObject>
#include <QPromise>
#include <QRandomGenerator>
#include <QSet>
#include <QThread>
#include <QTimeZone>

#include <algorithm>
#include <atomic>

namespace satcfdi::respaldo {

QString claveEstable(ResultadoRespaldo::Estado e)
{
    using E = ResultadoRespaldo::Estado;
    switch (e) {
    case E::Exito: return QStringLiteral("exito");
    case E::ExitoConAdvertencias: return QStringLiteral("exito_con_advertencias");
    case E::YaEnCurso: return QStringLiteral("ya_en_curso");
    case E::Cancelado: return QStringLiteral("cancelado");
    case E::Detenido: return QStringLiteral("detenido");
    case E::ErrorDestino: return QStringLiteral("error_destino");
    case E::ErrorCaptura: return QStringLiteral("error_captura");
    case E::ErrorFuente: return QStringLiteral("error_fuente");
    case E::ErrorContenedor: return QStringLiteral("error_contenedor");
    case E::ErrorPublicacion: return QStringLiteral("error_publicacion");
    }
    return QStringLiteral("error_contenedor");
}

namespace {

using Estado = ResultadoRespaldo::Estado;

constexpr int kIntentosNombre = 5;
constexpr quint64 kAvisoProgresoBytes = 1024 * 1024;

ResultadoRespaldo resultado(Estado estado, QString diagnostico = {})
{
    ResultadoRespaldo r;
    r.estado = estado;
    r.diagnostico = std::move(diagnostico);
    return r;
}

ResultadoRespaldo deDestino(Estado estado, const ErrorDestino& e)
{
    ResultadoRespaldo r = resultado(estado, e.diagnostico);
    r.errorDestino = e.categoria;
    return r;
}

QString sufijoAleatorio()
{
    return QStringLiteral("%1").arg(QRandomGenerator::system()->generate(), 8, 16, QLatin1Char('0'));
}

// Ejecuta `f` al salir del alcance salvo que se desarme.
class AlSalir {
public:
    explicit AlSalir(std::function<void()> f) : m_f(std::move(f)) {}
    ~AlSalir()
    {
        if (m_f) {
            m_f();
        }
    }
    void desarmar() { m_f = {}; }
    AlSalir(const AlSalir&) = delete;
    AlSalir& operator=(const AlSalir&) = delete;

private:
    std::function<void()> m_f;
};

} // namespace

struct RespaldoService::Impl {
    RespaldoService* q;
    FuenteRespaldo& fuente;
    std::shared_ptr<ContenedorRespaldo> contenedor;
    Opciones opciones;
    QString propietario;

    QThread hilo;
    QObject* contexto = nullptr;
    std::atomic<bool> enCurso{false};
    std::atomic<bool> cancelado{false};
    std::atomic<bool> cerrando{false};
    bool cerrado = false;

    Impl(RespaldoService* q_, FuenteRespaldo& f, std::shared_ptr<ContenedorRespaldo> c, Opciones o)
        : q(q_), fuente(f), contenedor(std::move(c)), opciones(std::move(o))
    {
        if (!opciones.generadorSufijo) {
            opciones.generadorSufijo = sufijoAleatorio;
        }
    }

    // Hilo grafico, antes de encolar: el trabajo ya ve el id resuelto.
    bool resolverId()
    {
        if (opciones.idInstalacion.isNull() && opciones.proveedorIdInstalacion) {
            opciones.idInstalacion = opciones.proveedorIdInstalacion();
        }
        if (opciones.idInstalacion.isNull()) {
            return false;
        }
        if (propietario.isEmpty()) {
            propietario = propietarioDe(opciones.idInstalacion);
        }
        return true;
    }

    void encolar(std::function<void()> trabajo)
    {
        QMetaObject::invokeMethod(contexto, std::move(trabajo), Qt::QueuedConnection);
    }

    bool debeParar() const { return cancelado.load() || cerrando.load(); }

    void avisar(Fase fase, quint64 bytes, quint64 total) { emit q->progreso(fase, bytes, total); }

    // Hilo del servicio.
    ResultadoRespaldo ejecutar(QFuture<Resultado<CapturaRespaldo, ErrorCaptura>> futuroCaptura,
                               std::function<void(const CapturaRespaldo&)> limpiador, PeticionRespaldo& p);

    ResultadoRespaldo errorDeContenedor(const ErrorRespaldo& e, const ArchivoParcial& parcial) const
    {
        if (const auto d = parcial.errorEscritura()) {
            return deDestino(Estado::ErrorDestino, *d);
        }
        using C = ErrorRespaldo::Categoria;
        ResultadoRespaldo r;
        switch (e.categoria) {
        case C::Cancelado: return resultado(Estado::Cancelado, QStringLiteral("cancelado"));
        case C::FuenteCambio:
        case C::FuenteFaltante:
        case C::FuenteInvalida:
        case C::RutaInvalida: r = resultado(Estado::ErrorFuente, e.diagnostico); break;
        case C::Escritura: r = resultado(Estado::ErrorDestino, e.diagnostico); r.errorDestino = ErrorDestino::Categoria::Escritura; break;
        default: r = resultado(Estado::ErrorContenedor, e.diagnostico); break;
        }
        r.errorContenedor = e.categoria;
        return r;
    }

    void depurar(DestinoRespaldo& destino, const PeticionRespaldo& p, const QByteArray& idPublicado,
                 EstadoRegistro& registro, ResultadoRespaldo& r);
    int limpiarParciales(DestinoRespaldo& destino) const;
};

RespaldoService::RespaldoService(FuenteRespaldo& fuente, std::shared_ptr<ContenedorRespaldo> contenedor,
                                 Opciones opciones, QObject* parent)
    : QObject(parent), m_impl(std::make_unique<Impl>(this, fuente, std::move(contenedor), std::move(opciones)))
{
    qRegisterMetaType<satcfdi::respaldo::RespaldoService::Fase>();
    m_impl->hilo.setObjectName(QStringLiteral("satcfdi-respaldo"));
    m_impl->contexto = new QObject;
    m_impl->contexto->moveToThread(&m_impl->hilo);
    QObject::connect(&m_impl->hilo, &QThread::finished, m_impl->contexto, &QObject::deleteLater);
    m_impl->hilo.start();
}

RespaldoService::~RespaldoService()
{
    cerrar();
}

QString RespaldoService::propietarioDe(const QUuid& idInstalacion)
{
    const QByteArray h = QCryptographicHash::hash(QByteArrayLiteral("AFB1 propietario parcial v1")
                                                      + idInstalacion.toRfc4122(),
                                                  QCryptographicHash::Sha256);
    return QString::fromLatin1(h.left(4).toHex());
}

quint64 RespaldoService::estimarTamano(const CapturaRespaldo& captura)
{
    quint64 datos = 0;
    quint64 estructura = 204 + 1024; // cabecera + metadatos
    for (const EntradaCaptura& e : captura.entradas) {
        if (e.huella) {
            datos += e.huella->tamano;
        }
        // registro + fila de manifiesto: ruta dos veces, tamanos y SHA-256.
        estructura += 2 * static_cast<quint64>(e.rutaEnRespaldo.toUtf8().size()) + 2 * (1 + 1 + 2 + 8 + 32);
    }
    const quint64 total = datos + estructura;
    const quint64 bloques = total / kTamanoBloque + 1;
    return total + bloques * (5 + 16);
}

quint64 RespaldoService::espacioNecesario(quint64 tamanoEstimado)
{
    return tamanoEstimado + std::max<quint64>(tamanoEstimado / 10, 16ULL * 1024 * 1024);
}

bool RespaldoService::enCurso() const
{
    return m_impl->enCurso.load();
}

void RespaldoService::cancelar()
{
    if (m_impl->enCurso.load()) {
        m_impl->cancelado.store(true);
    }
}

QFuture<ResultadoRespaldo> RespaldoService::respaldar(PeticionRespaldo peticion)
{
    if (m_impl->cerrado || m_impl->cerrando.load()) {
        return QtFuture::makeReadyValueFuture(resultado(Estado::Detenido, QStringLiteral("detenido")));
    }
    bool libre = false;
    if (!m_impl->enCurso.compare_exchange_strong(libre, true)) {
        return QtFuture::makeReadyValueFuture(resultado(Estado::YaEnCurso, QStringLiteral("ya_en_curso")));
    }
    m_impl->cancelado.store(false);
    if (!peticion.destino || !m_impl->resolverId()) {
        m_impl->enCurso.store(false);
        return QtFuture::makeReadyValueFuture(resultado(Estado::ErrorContenedor, QStringLiteral("peticion")));
    }

    // La captura se inicia en el hilo que llama (contrato de FuenteRespaldo).
    m_impl->avisar(Fase::Captura, 0, 0);
    auto captura = m_impl->fuente.capturar();
    auto limpiador = m_impl->fuente.limpiadorAutonomo();

    auto promesa = std::make_shared<QPromise<ResultadoRespaldo>>();
    QFuture<ResultadoRespaldo> futuro = promesa->future();
    auto p = std::make_shared<PeticionRespaldo>(std::move(peticion));
    Impl* impl = m_impl.get();
    impl->encolar([impl, promesa, captura, limpiador, p]() mutable {
        promesa->start();
        ResultadoRespaldo r = impl->ejecutar(std::move(captura), std::move(limpiador), *p);
        p.reset(); // suelta KEK y destino antes de completar
        impl->cancelado.store(false);
        impl->enCurso.store(false);
        promesa->addResult(std::move(r));
        promesa->finish();
    });
    return futuro;
}

ResultadoRespaldo RespaldoService::Impl::ejecutar(QFuture<Resultado<CapturaRespaldo, ErrorCaptura>> futuroCaptura,
                                                  std::function<void(const CapturaRespaldo&)> limpiador,
                                                  PeticionRespaldo& p)
{
    // 1. Captura (T025.2). No se interrumpe a medias: se espera su fin; al
    // cerrar se abandona y la limpia su propia continuacion.
    while (!futuroCaptura.isFinished()) {
        if (cerrando.load()) {
            futuroCaptura.then([limpiador](Resultado<CapturaRespaldo, ErrorCaptura> c) {
                if (c) {
                    limpiador(c.valor());
                }
            });
            return resultado(Estado::Detenido, QStringLiteral("cerrado_en_captura"));
        }
        QThread::msleep(5);
    }
    if (futuroCaptura.isCanceled() || futuroCaptura.resultCount() == 0) {
        ResultadoRespaldo r = resultado(Estado::ErrorCaptura, QStringLiteral("captura.cancelada"));
        r.errorCaptura = ErrorCaptura::Categoria::Interno;
        return r;
    }
    auto rc = futuroCaptura.result();
    if (!rc) {
        ResultadoRespaldo r = resultado(Estado::ErrorCaptura, rc.error().diagnostico);
        r.errorCaptura = rc.error().categoria;
        return r;
    }
    const CapturaRespaldo captura = rc.valor();
    AlSalir limpiarCaptura([&] { limpiador(captura); });
    if (debeParar()) {
        return resultado(Estado::Cancelado, QStringLiteral("cancelado.captura"));
    }

    // 2. Destino y escritura.
    const quint64 estimado = estimarTamano(captura);
    quint64 totalDatos = 0;
    for (const EntradaCaptura& e : captura.entradas) {
        totalDatos += e.huella ? e.huella->tamano : 0;
    }
    avisar(Fase::Escritura, 0, totalDatos);
    // M2: la carpeta se abre una vez por respaldo, en este hilo (D20).
    if (auto abierta = p.destino->abrir(); !abierta) {
        ResultadoRespaldo r = deDestino(Estado::ErrorDestino, abierta.error());
        r.tamanoEstimado = estimado;
        return r;
    }
    auto valido = p.destino->validar(espacioNecesario(estimado));
    if (!valido) {
        ResultadoRespaldo r = deDestino(Estado::ErrorDestino, valido.error());
        r.tamanoEstimado = estimado;
        return r;
    }
    if (debeParar()) {
        return resultado(Estado::Cancelado, QStringLiteral("cancelado.destino"));
    }
    auto creado = p.destino->crearParcial(propietario);
    if (!creado) {
        return deDestino(Estado::ErrorDestino, creado.error());
    }
    std::unique_ptr<ArchivoParcial> parcial = std::move(creado).valor();
    AlSalir descartar([&] { parcial->descartar(); });

    quint64 ultimoAviso = 0;
    auto escritor = contenedor->crearEscritor(
        parcial->dispositivo(), p.kekContrasena, p.kekRecuperacion,
        datosNuevoRespaldo(captura, opciones.idInstalacion, opciones.reloj()), [&](quint64 bytes) {
            if (bytes - ultimoAviso >= kAvisoProgresoBytes || bytes == totalDatos) {
                ultimoAviso = bytes;
                avisar(Fase::Escritura, bytes, totalDatos);
            }
        });
    if (!escritor) {
        return errorDeContenedor(escritor.error(), *parcial);
    }
    std::unique_ptr<EscritorRespaldo> esc = std::move(escritor).valor();
    const Cancelacion cancelacion([this] { return debeParar(); });
    auto alimentado = alimentarEscritor(*esc, captura, cancelacion);
    if (!alimentado) {
        return errorDeContenedor(alimentado.error(), *parcial);
    }
    auto inventario = esc->finalizar();
    if (!inventario) {
        return errorDeContenedor(inventario.error(), *parcial);
    }
    esc.reset();
    if (debeParar()) {
        return resultado(Estado::Cancelado, QStringLiteral("cancelado.escritura"));
    }

    // 3. Publicacion (D8): F_FULLFSYNC + rename exclusivo a un nombre opaco.
    avisar(Fase::Publicacion, totalDatos, totalDatos);
    if (auto s = parcial->sincronizar(); !s) {
        return deDestino(Estado::ErrorPublicacion, s.error());
    }
    if (debeParar()) {
        return resultado(Estado::Cancelado, QStringLiteral("cancelado.publicacion"));
    }
    if (auto misma = p.destino->comprobarIdentidad(); !misma) {
        return deDestino(Estado::ErrorDestino, misma.error());
    }
    const QString fecha = opciones.reloj().toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    QString publicado;
    std::optional<EntradaDestino> entradaPublicada;
    std::optional<ErrorDestino> ultimoError;
    for (int intento = 0; intento < kIntentosNombre && publicado.isEmpty(); ++intento) {
        const QString nombre =
            nombres::kPrefijoPublicado + fecha + QLatin1Char('-') + opciones.generadorSufijo() + nombres::kSufijoPublicado;
        auto r = parcial->publicar(nombre);
        if (r) {
            publicado = nombre;
            entradaPublicada = r.valor();
        } else if (r.error().categoria != ErrorDestino::Categoria::Conflicto) {
            // M1: ExclusivoNoSoportado y demas errores: sin reintentos.
            return deDestino(Estado::ErrorPublicacion, r.error());
        } else {
            ultimoError = r.error();
        }
    }
    if (publicado.isEmpty()) {
        return deDestino(Estado::ErrorPublicacion, *ultimoError);
    }
    descartar.desarmar();

    ResultadoRespaldo r;
    r.nombrePublicado = publicado;
    r.faltantes = alimentado.valor().faltantes;
    r.omitidos = alimentado.valor().omitidos;
    r.bytesDatos = inventario.valor().bytesDatos;
    r.tamanoEstimado = estimado;

    // D20: registrar lo publicado (fuera de SQLite y del respaldo).
    EstadoRegistro registro;
    if (opciones.registro) {
        auto cargado = opciones.registro->cargar();
        if (cargado) {
            registro = std::move(cargado).valor();
        } else {
            r.avisoRegistro = true;
        }
        EntradaPublicada e;
        e.carpeta = p.destino->identidad();
        e.nombre = publicado;
        e.idRespaldo = inventario.valor().cabecera.idRespaldo;
        e.fechaUtc = inventario.valor().cabecera.creadoEnUtc;
        e.tamano = entradaPublicada->tamano;
        e.dispositivo = entradaPublicada->dispositivo;
        e.inodo = entradaPublicada->inodo;
        registro.publicados.append(e);
        if (!opciones.registro->guardar(registro)) {
            r.avisoRegistro = true;
        }
    }

    // 4. Retencion (D17, D20), solo despues de publicar; un fallo no invalida.
    avisar(Fase::Depuracion, totalDatos, totalDatos);
    if (auto misma = p.destino->comprobarIdentidad(); !misma) {
        r.avisoDepuracion = true; // la carpeta cambio: no se depura nada
    } else {
        depurar(*p.destino, p, inventario.valor().cabecera.idRespaldo, registro, r);
        if (opciones.registro && !opciones.registro->guardar(registro)) {
            r.avisoRegistro = true;
        }
    }

    const bool advertencias = alimentado.valor().conAdvertencias() || r.avisoDepuracion || r.avisoRegistro
        || r.copiasSinRegistro > 0 || r.duplicadosProveedor > 0;
    r.estado = advertencias ? Estado::ExitoConAdvertencias : Estado::Exito;
    return r;
}

void RespaldoService::Impl::depurar(DestinoRespaldo& destino, const PeticionRespaldo& p, const QByteArray& idPublicado,
                                    EstadoRegistro& registro, ResultadoRespaldo& r)
{
    auto lista = destino.listar();
    if (!lista) {
        r.avisoDepuracion = true;
        return;
    }
    const QString carpeta = destino.identidad();
    struct Copia {
        QDateTime fecha;
        QByteArray id;
        QStringList nombres;
    };
    QHash<QByteArray, Copia> porId;
    QSet<QString> presentes;
    for (const EntradaDestino& e : lista.valor()) {
        presentes.insert(e.nombre);
        if (!nombres::pareceRespaldo(e.nombre)) {
            continue;
        }
        QByteArray id;
        QDateTime fecha;
        if (!e.sinDescargar) {
            // En disco: la cabecera autenticada decide (D17).
            // D16: cada KEK sobre un dispositivo NUEVO (posicion 0): la
            // contrasena actual o, para copias de una contrasena anterior, la
            // clave de recuperacion.
            std::optional<CabeceraRespaldo> cabecera;
            bool legible = false;
            for (const KekRespaldo* kek : {&p.kekContrasena, &p.kekRecuperacion}) {
                auto dispositivo = destino.abrirParaLeer(e.nombre);
                if (!dispositivo) {
                    break; // enlace, no regular, sin descargar o ilegible
                }
                legible = true;
                auto cab = contenedor->autenticarCabecera(*dispositivo.valor(), *kek);
                if (cab) {
                    cabecera = std::move(cab).valor();
                    break;
                }
            }
            if (!legible) {
                continue; // no cuenta ni se borra
            }
            std::optional<CabeceraRespaldo> cab = cabecera;
            if (!cab || cab->idInstalacion != opciones.idInstalacion) {
                continue; // ajeno o de otra clave: no cuenta ni se borra
            }
            id = cab->idRespaldo;
            fecha = cab->creadoEnUtc;
        } else {
            // D20: solo en la nube. Nunca se descarga: cuenta solo si coincide
            // con lo que esta instalacion publico (nombre, tamano, archivo).
            const EntradaPublicada* coincidencia = nullptr;
            for (const EntradaPublicada& pub : registro.publicados) {
                if (pub.carpeta == carpeta && pub.nombre == e.nombre && pub.tamano == e.tamano
                    && pub.dispositivo == e.dispositivo && pub.inodo == e.inodo) {
                    coincidencia = &pub;
                    break;
                }
            }
            if (!coincidencia) {
                ++r.copiasSinRegistro; // no se toca
                continue;
            }
            id = coincidencia->idRespaldo;
            fecha = coincidencia->fechaUtc;
        }
        Copia& c = porId[id];
        c.fecha = fecha;
        c.id = id;
        c.nombres.append(e.nombre);
    }

    // Registro: fuera lo que ya no esta en la carpeta.
    registro.publicados.erase(std::remove_if(registro.publicados.begin(), registro.publicados.end(),
                                             [&](const EntradaPublicada& pub) {
                                                 return pub.carpeta == carpeta && !presentes.contains(pub.nombre);
                                             }),
                              registro.publicados.end());

    // B2: ids repetidos (p. ej. copia en conflicto del proveedor): aviso una
    // sola vez por id; nunca se borran.
    for (const Copia& c : std::as_const(porId)) {
        if (c.nombres.size() > 1 && !registro.duplicadosAvisados.contains(c.id)) {
            registro.duplicadosAvisados.append(c.id);
            ++r.duplicadosProveedor;
        }
    }

    if (!porId.contains(idPublicado)) {
        r.avisoDepuracion = true; // no se ve la copia nueva: no se depura nada
        return;
    }
    QList<Copia> copias = porId.values();
    std::sort(copias.begin(), copias.end(), [](const Copia& a, const Copia& b) {
        return a.fecha != b.fecha ? a.fecha > b.fecha : a.id > b.id;
    });
    for (qsizetype i = opciones.copiasConservadas; i < copias.size(); ++i) {
        const Copia& c = copias.at(i);
        if (c.id == idPublicado || c.nombres.size() != 1) {
            continue; // la nueva nunca; un id repetido nunca
        }
        const QString nombre = c.nombres.constFirst();
        if (destino.borrar(nombre)) {
            ++r.depurados;
            registro.publicados.erase(std::remove_if(registro.publicados.begin(), registro.publicados.end(),
                                                     [&](const EntradaPublicada& pub) {
                                                         return pub.carpeta == carpeta && pub.nombre == nombre;
                                                     }),
                                      registro.publicados.end());
        } else {
            r.avisoDepuracion = true;
        }
    }
}

int RespaldoService::Impl::limpiarParciales(DestinoRespaldo& destino) const
{
    if (!destino.abrir()) {
        return -1;
    }
    auto r = destino.limpiarParciales(propietario);
    return r ? r.valor() : -1;
}

QFuture<int> RespaldoService::limpiarPartialPropios(std::shared_ptr<DestinoRespaldo> destino)
{
    if (m_impl->cerrado || m_impl->cerrando.load() || !destino || !m_impl->resolverId()) {
        return QtFuture::makeReadyValueFuture(-1);
    }
    auto promesa = std::make_shared<QPromise<int>>();
    QFuture<int> futuro = promesa->future();
    Impl* impl = m_impl.get();
    impl->encolar([impl, promesa, destino]() {
        promesa->start();
        promesa->addResult(impl->limpiarParciales(*destino));
        promesa->finish();
    });
    return futuro;
}

void RespaldoService::cerrar()
{
    if (m_impl->cerrado) {
        return;
    }
    m_impl->cerrando.store(true);
    m_impl->cancelado.store(true);
    // Vacia la cola: cada trabajo ya encolado corre, ve `cerrando` y termina
    // (un respaldo en escritura se detiene en el siguiente chunk y borra su
    // .partial; una captura pendiente se abandona con su limpiador). Ningun
    // trabajo espera al hilo grafico, asi que esta espera no se bloquea.
    QMetaObject::invokeMethod(m_impl->contexto, [] {}, Qt::BlockingQueuedConnection);
    m_impl->hilo.quit();
    m_impl->hilo.wait();
    m_impl->cerrado = true;
}

} // namespace satcfdi::respaldo
