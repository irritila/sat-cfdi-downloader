#include "application/respaldo/RestauracionService.h"

#include "ports/respaldo/FuenteRespaldo.h"

#include <QHash>
#include <QIODevice>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPromise>

#include <atomic>
#include <thread>

namespace satcfdi::respaldo {

QString claveEstable(ResultadoSolicitudRestauracion::Estado e)
{
    using E = ResultadoSolicitudRestauracion::Estado;
    switch (e) {
    case E::Relanzando: return QStringLiteral("relanzando");
    case E::SinRelanzamiento: return QStringLiteral("sin_relanzamiento");
    case E::Rechazada: return QStringLiteral("rechazada");
    case E::FalloTrasDetener: return QStringLiteral("fallo_tras_detener");
    }
    return QStringLiteral("rechazada");
}

namespace {

using C = ErrorRestauracion::Categoria;
using ResultadoPreparar = Resultado<ResumenRestauracion, ErrorRestauracion>;
using EstadoSolicitud = ResultadoSolicitudRestauracion::Estado;

constexpr quint64 kAvisoProgresoBytes = 1024 * 1024;

ErrorRestauracion error(C c, const char* d)
{
    return ErrorRestauracion::de(c, QString::fromLatin1(d));
}

ErrorRestauracion deContenedor(const ErrorRespaldo& e)
{
    using K = ErrorRespaldo::Categoria;
    ErrorRestauracion r;
    r.diagnostico = e.diagnostico;
    r.contenedor = e.categoria;
    switch (e.categoria) {
    case K::CredencialIncorrecta: r.categoria = C::CredencialIncorrecta; break;
    case K::ClaveRecuperacionMalFormada: r.categoria = C::ClaveRecuperacionMalFormada; break;
    case K::Cancelado: r.categoria = C::Cancelado; break;
    case K::Lectura: r.categoria = C::Origen; break;
    case K::Escritura:
    case K::DestinoInvalido: r.categoria = C::Staging; break;
    case K::RutaInvalida: r.categoria = C::RutaInesperada; break;
    default: r.categoria = C::Contenedor; break;
    }
    return r;
}

ResultadoSolicitudRestauracion solicitud(EstadoSolicitud estado, std::optional<ErrorRestauracion> e = std::nullopt)
{
    ResultadoSolicitudRestauracion r;
    r.estado = estado;
    r.error = std::move(e);
    return r;
}

// Hilo de trabajo desacoplado: no se une; solo usa estado compartido. Asi una
// lectura bloqueada nunca retiene al hilo grafico ni al cierre.
std::atomic<int> g_hilosVivos{0};

void lanzar(std::function<void()> trabajo)
{
    ++g_hilosVivos;
    std::thread([t = std::move(trabajo)]() mutable {
        t();
        t = {}; // suelta lo capturado antes de contarse como terminado
        --g_hilosVivos;
    }).detach();
}

struct EntradaLeida {
    EntradaManifiesto::Estado estado = EntradaManifiesto::Estado::Presente;
    quint64 tamano = 0;
    QByteArray sha256;
};

} // namespace

struct RestauracionService::Estado {
    Dependencias d;

    mutable QMutex mutex;
    RestauracionService* q = nullptr; // nulo tras cerrar(); protegido por mutex
    std::atomic<bool> cerrando{false};
    std::atomic<bool> cancelado{false};
    int trabajadores = 0;
    bool solicitando = false;
    // Preparacion en curso.
    std::shared_ptr<QPromise<ResultadoPreparar>> promesa;
    std::shared_ptr<std::atomic<bool>> resuelta;
    // Preparacion lista.
    std::optional<StagingRestauracion> preparado;
    std::shared_ptr<OrigenRestauracion> origenPreparado;
    HuellaArchivo huellaOrigen;
    QByteArray huellaStaging;

    bool debeParar() const { return cancelado.load() || cerrando.load(); }

    void emitirProgreso(Fase fase, quint64 bytes, quint64 total)
    {
        QMutexLocker l(&mutex);
        if (RestauracionService* s = q) {
            QMetaObject::invokeMethod(
                s, [s, fase, bytes, total] { emit s->progreso(fase, bytes, total); }, Qt::QueuedConnection);
        }
    }

    // Ejecuta `f` en el hilo grafico si el servicio sigue vivo.
    void enGrafico(std::function<void()> f)
    {
        QMutexLocker l(&mutex);
        if (RestauracionService* s = q) {
            QMetaObject::invokeMethod(s, std::move(f), Qt::QueuedConnection);
        }
    }

    static void resolver(const std::shared_ptr<QPromise<ResultadoPreparar>>& p,
                         const std::shared_ptr<std::atomic<bool>>& hecho, ResultadoPreparar r)
    {
        if (!hecho->exchange(true)) {
            p->addResult(std::move(r));
            p->finish();
        }
    }

    // --- Hilo de trabajo ------------------------------------------------------
    Resultado<KekRespaldo, ErrorRestauracion> elegirKek(const PeticionRestauracion& p);
    ResultadoPreparar preparar(const PeticionRestauracion& p, std::optional<StagingRestauracion>& staging,
                               HuellaArchivo& origenVerificado, QByteArray& stagingVerificado);
};

RestauracionService::RestauracionService(Dependencias dependencias, QObject* parent)
    : QObject(parent), m_estado(std::make_shared<Estado>())
{
    qRegisterMetaType<satcfdi::respaldo::RestauracionService::Fase>();
    m_estado->d = std::move(dependencias);
    m_estado->q = this;
}

RestauracionService::~RestauracionService()
{
    cerrar();
}

bool RestauracionService::enCurso() const
{
    QMutexLocker l(&m_estado->mutex);
    return m_estado->trabajadores > 0;
}

bool RestauracionService::preparada() const
{
    QMutexLocker l(&m_estado->mutex);
    return m_estado->preparado.has_value();
}

void RestauracionService::cancelar()
{
    std::shared_ptr<QPromise<ResultadoPreparar>> promesa;
    std::shared_ptr<std::atomic<bool>> resuelta;
    {
        QMutexLocker l(&m_estado->mutex);
        if (!m_estado->promesa || m_estado->resuelta->load()) {
            return;
        }
        m_estado->cancelado.store(true);
        promesa = m_estado->promesa;
        resuelta = m_estado->resuelta;
    }
    // Sin esperar al hilo de trabajo (puede estar bloqueado leyendo).
    Estado::resolver(promesa, resuelta, ResultadoPreparar::fallo(error(C::Cancelado, "cancelado")));
}

QFuture<ResultadoPreparar> RestauracionService::preparar(PeticionRestauracion peticion)
{
    const std::shared_ptr<Estado> e = m_estado;
    auto promesa = std::make_shared<QPromise<ResultadoPreparar>>();
    auto resuelta = std::make_shared<std::atomic<bool>>(false);
    QFuture<ResultadoPreparar> futuro = promesa->future();
    promesa->start();
    {
        QMutexLocker l(&e->mutex);
        std::optional<ErrorRestauracion> rechazo;
        if (e->cerrando.load()) {
            rechazo = error(C::Detenido, "detenido");
        } else if (e->trabajadores > 0 || e->solicitando) {
            rechazo = error(C::EnCurso, "en_curso");
        } else if (!peticion.origen || !peticion.secreto || peticion.secreto->vacio() || !e->d.contenedor
                   || !e->d.area || !e->d.base) {
            rechazo = error(C::Interno, "peticion");
        }
        if (rechazo) {
            l.unlock();
            Estado::resolver(promesa, resuelta, ResultadoPreparar::fallo(*rechazo));
            return futuro;
        }
        // Una preparacion lista anterior se descarta (crearStaging tambien la
        // retira del disco).
        e->preparado.reset();
        e->origenPreparado.reset();
        e->cancelado.store(false);
        e->promesa = promesa;
        e->resuelta = resuelta;
        ++e->trabajadores;
    }

    auto p = std::make_shared<PeticionRestauracion>(std::move(peticion));
    lanzar([e, p, promesa, resuelta] {
        std::optional<StagingRestauracion> staging;
        HuellaArchivo huellaOrigen;
        QByteArray huellaStaging;
        ResultadoPreparar r = e->preparar(*p, staging, huellaOrigen, huellaStaging);
        if (p->secreto) {
            p->secreto->limpiar();
        }
        bool aceptada = false;
        {
            QMutexLocker l(&e->mutex);
            if (r && !e->debeParar() && !resuelta->exchange(true)) {
                e->preparado = staging;
                e->origenPreparado = p->origen;
                e->huellaOrigen = huellaOrigen;
                e->huellaStaging = huellaStaging;
                aceptada = true;
            }
        }
        if (aceptada) {
            promesa->addResult(std::move(r));
            promesa->finish();
        } else {
            if (staging) {
                e->d.area->descartar(*staging);
            }
            Estado::resolver(promesa, resuelta,
                             e->debeParar() && r ? ResultadoPreparar::fallo(error(C::Cancelado, "cancelado"))
                                                 : std::move(r));
        }
        QMutexLocker l(&e->mutex);
        --e->trabajadores;
        if (e->promesa == promesa) {
            e->promesa.reset();
            e->resuelta.reset();
        }
    });
    return futuro;
}

Resultado<KekRespaldo, ErrorRestauracion> RestauracionService::Estado::elegirKek(const PeticionRestauracion& p)
{
    using R = Resultado<KekRespaldo, ErrorRestauracion>;
    using Cred = PeticionRestauracion::Credencial;
    const ContenedorRespaldo& c = *d.contenedor;
    std::optional<ErrorRestauracion> ultimo;
    const bool probarContrasena = p.credencial != Cred::ClaveRecuperacion;
    const bool probarClave = p.credencial != Cred::Contrasena;

    // D16: cada intento sobre un dispositivo NUEVO (posicion 0).
    const auto autenticar = [&](const KekRespaldo& kek) -> std::optional<ErrorRestauracion> {
        auto dispositivo = p.origen->abrir();
        if (!dispositivo) {
            return dispositivo.error();
        }
        auto cab = c.autenticarCabecera(*dispositivo.valor(), kek);
        if (!cab) {
            return deContenedor(cab.error());
        }
        return std::nullopt;
    };

    if (probarContrasena) {
        auto dispositivo = p.origen->abrir();
        if (!dispositivo) {
            return R::fallo(dispositivo.error());
        }
        std::unique_ptr<QIODevice> primero = std::move(dispositivo).valor();
        auto cabecera = c.leerCabecera(*primero);
        primero.reset();
        if (!cabecera) {
            return R::fallo(deContenedor(cabecera.error()));
        }
        if (debeParar()) {
            return R::fallo(error(C::Cancelado, "cancelado"));
        }
        auto kek = c.derivarKekContrasena(*p.secreto, cabecera.valor().kdf);
        if (!kek) {
            return R::fallo(deContenedor(kek.error()));
        }
        if (debeParar()) {
            return R::fallo(error(C::Cancelado, "cancelado"));
        }
        ultimo = autenticar(kek.valor());
        if (!ultimo) {
            return R::exito(std::move(kek).valor());
        }
        if (ultimo->categoria != C::CredencialIncorrecta) {
            return R::fallo(*ultimo);
        }
    }
    if (probarClave) {
        auto clave = c.leerClaveRecuperacion(*p.secreto);
        if (!clave) {
            // Automatica: el texto no era una clave; el error generico se conserva.
            return R::fallo(ultimo ? *ultimo : deContenedor(clave.error()));
        }
        auto kek = c.derivarKekRecuperacion(clave.valor());
        if (!kek) {
            return R::fallo(deContenedor(kek.error()));
        }
        if (debeParar()) {
            return R::fallo(error(C::Cancelado, "cancelado"));
        }
        ultimo = autenticar(kek.valor());
        if (!ultimo) {
            return R::exito(std::move(kek).valor());
        }
        return R::fallo(*ultimo);
    }
    return R::fallo(ultimo ? *ultimo : error(C::CredencialIncorrecta, "credencial"));
}

ResultadoPreparar RestauracionService::Estado::preparar(const PeticionRestauracion& p,
                                                        std::optional<StagingRestauracion>& staging,
                                                        HuellaArchivo& origenVerificado, QByteArray& stagingVerificado)
{
    using R = ResultadoPreparar;
    const auto cancelada = []() { return R::fallo(error(C::Cancelado, "cancelado")); };

    emitirProgreso(Fase::Autenticando, 0, 0);
    auto inicial = p.origen->huella();
    if (!inicial) {
        return R::fallo(inicial.error());
    }
    origenVerificado = inicial.valor();

    auto creado = d.area->crearStaging(origenVerificado.tamano);
    if (!creado) {
        return R::fallo(creado.error());
    }
    staging = creado.valor();
    if (debeParar()) {
        return cancelada();
    }

    auto kek = elegirKek(p);
    // Seguridad 6: el texto tecleado ya no hace falta (con o sin KEK).
    if (p.secreto) {
        p.secreto->limpiar();
    }
    if (!kek) {
        return R::fallo(kek.error());
    }

    // Extraccion completa (D5): autentica todo y escribe en el staging.
    const quint64 total = origenVerificado.tamano;
    emitirProgreso(Fase::Extrayendo, 0, total);
    auto abierto = p.origen->abrir();
    if (!abierto) {
        return R::fallo(abierto.error());
    }
    std::unique_ptr<QIODevice> dispositivo = std::move(abierto).valor();
    QHash<QString, EntradaLeida> entradas;
    QStringList orden;
    OpcionesLectura opciones;
    opciones.cancelacion = Cancelacion([this] { return debeParar(); });
    quint64 ultimoAviso = 0;
    opciones.progreso = [this, &ultimoAviso, total](quint64 bytes) {
        if (bytes - ultimoAviso >= kAvisoProgresoBytes) {
            ultimoAviso = bytes;
            emitirProgreso(Fase::Extrayendo, bytes, total);
        }
    };
    opciones.alLeerEntrada = [&entradas, &orden](const EntradaManifiesto& m) {
        entradas.insert(m.rutaRelativa, EntradaLeida{m.estado, m.tamano, m.sha256});
        orden.append(m.rutaRelativa);
    };
    auto inventario = d.contenedor->extraer(*dispositivo, kek.valor(), staging->extraccion, opciones);
    dispositivo.reset();
    if (!inventario) {
        return R::fallo(deContenedor(inventario.error()));
    }
    if (debeParar()) {
        return cancelada();
    }

    // Metadatos y rutas.
    emitirProgreso(Fase::Validando, total, total);
    const auto metadatos = MetadatosCaptura::desdeBytes(inventario.valor().metadatos);
    if (!metadatos) {
        return R::fallo(error(C::MetadatosInvalidos, "metadatos"));
    }
    if (metadatos->versionEsquema > d.base->versionMaximaConocida()) {
        return R::fallo(error(C::EsquemaMasNuevo, "esquema.mas_nuevo"));
    }
    int bases = 0;
    int faltantes = 0;
    int paquetes = 0;
    for (const QString& ruta : std::as_const(orden)) {
        const EntradaLeida& en = entradas.value(ruta);
        if (ruta == kRutaRespaldoBase) {
            bases += en.estado == EntradaManifiesto::Estado::Presente ? 1 : 0;
        } else if (ruta == kRutaRespaldoPreferencias) {
            // opcional
        } else if (ruta.startsWith(kPrefijoRespaldoPaquetes)
                   && esRutaRelativaValida(ruta.mid(kPrefijoRespaldoPaquetes.size()))) {
            (en.estado == EntradaManifiesto::Estado::Presente ? paquetes : faltantes) += 1;
        } else {
            return R::fallo(error(C::RutaInesperada, "ruta.inesperada"));
        }
    }
    if (bases != 1) {
        return R::fallo(error(C::RutaInesperada, "ruta.sin_base"));
    }

    if (auto colocada = d.area->colocarBase(*staging); !colocada) {
        return R::fallo(colocada.error());
    }
    auto informe = d.base->preparar(staging->rutaBase());
    if (!informe) {
        return R::fallo(informe.error());
    }
    if (debeParar()) {
        return cancelada();
    }

    // mtime de los ZIP: huella del indice si el contenido coincide (asi el
    // indice no reindexa, T015.2); si no, la fecha de descarga.
    int mtimes = 0;
    for (const MtimePaquete& m : informe.valor().paquetes) {
        const auto it = entradas.constFind(kPrefijoRespaldoPaquetes + m.rutaLocal);
        if (it == entradas.constEnd() || it->estado != EntradaManifiesto::Estado::Presente) {
            continue;
        }
        std::optional<qint64> ms;
        if (m.modificadoMs && m.sha256 == it->sha256 && m.tamano == static_cast<qint64>(it->tamano)) {
            ms = m.modificadoMs;
        } else if (m.descargadoMs) {
            ms = m.descargadoMs;
        }
        if (!ms) {
            continue;
        }
        if (auto f = d.area->fijarMtime(*staging, m.rutaLocal, *ms); !f) {
            return R::fallo(f.error());
        }
        ++mtimes;
    }

    emitirProgreso(Fase::Sincronizando, total, total);
    if (auto s = d.area->sincronizar(*staging); !s) {
        return R::fallo(s.error());
    }
    auto h = d.area->huella(*staging);
    if (!h) {
        return R::fallo(h.error());
    }
    stagingVerificado = h.valor();
    auto final = p.origen->huella();
    if (!final) {
        return R::fallo(final.error());
    }
    if (!(final.valor() == origenVerificado)) {
        return R::fallo(error(C::OrigenCambio, "origen.cambio"));
    }
    if (debeParar()) {
        return cancelada();
    }
    const CabeceraRespaldo& cab = inventario.valor().cabecera;
    DatosPreparado datos{stagingVerificado, origenVerificado, cab.idRespaldo, cab.creadoEnUtc};
    if (auto marcado = d.area->marcarPreparado(*staging, datos); !marcado) {
        return R::fallo(marcado.error());
    }

    ResumenRestauracion resumen;
    resumen.idRespaldo = cab.idRespaldo;
    resumen.creadoEnUtc = cab.creadoEnUtc;
    resumen.idInstalacionRespaldo = cab.idInstalacion;
    resumen.versionAppRespaldo = metadatos->versionApp;
    resumen.capturadaEnUtc = metadatos->capturadaEnUtc;
    resumen.esquemaRespaldo = informe.valor().versionInicial;
    resumen.esquemaFinal = informe.valor().versionFinal;
    resumen.migrada = resumen.esquemaFinal > resumen.esquemaRespaldo;
    resumen.perfiles = informe.valor().perfiles;
    resumen.solicitudes = informe.valor().solicitudes;
    resumen.paquetes = paquetes;
    resumen.faltantes = faltantes;
    resumen.credencialesBorradas = informe.valor().credencialesBorradas;
    resumen.mtimesConservados = mtimes;
    resumen.bytesDatos = inventario.valor().bytesDatos;
    return R::exito(std::move(resumen));
}

QFuture<void> RestauracionService::descartar()
{
    const std::shared_ptr<Estado> e = m_estado;
    std::optional<StagingRestauracion> s;
    {
        QMutexLocker l(&e->mutex);
        if (e->solicitando || !e->preparado) {
            return QtFuture::makeReadyVoidFuture();
        }
        s = e->preparado;
        e->preparado.reset();
        e->origenPreparado.reset();
        ++e->trabajadores;
    }
    auto promesa = std::make_shared<QPromise<void>>();
    QFuture<void> f = promesa->future();
    promesa->start();
    lanzar([e, s, promesa] {
        e->d.area->descartar(*s);
        {
            QMutexLocker l(&e->mutex);
            --e->trabajadores;
        }
        promesa->finish();
    });
    return f;
}

QFuture<ResultadoSolicitudRestauracion> RestauracionService::solicitar()
{
    const std::shared_ptr<Estado> e = m_estado;
    StagingRestauracion s;
    std::shared_ptr<OrigenRestauracion> origen;
    HuellaArchivo huellaOrigen;
    QByteArray huellaStaging;
    {
        QMutexLocker l(&e->mutex);
        if (e->cerrando.load()) {
            return QtFuture::makeReadyValueFuture(solicitud(EstadoSolicitud::Rechazada, error(C::Detenido, "detenido")));
        }
        if (e->solicitando || e->trabajadores > 0) {
            return QtFuture::makeReadyValueFuture(solicitud(EstadoSolicitud::Rechazada, error(C::EnCurso, "en_curso")));
        }
        if (!e->preparado || !e->d.detenerApp) {
            return QtFuture::makeReadyValueFuture(
                solicitud(EstadoSolicitud::Rechazada, error(C::NoPreparada, "no_preparada")));
        }
        e->solicitando = true;
        s = *e->preparado;
        origen = e->origenPreparado;
        huellaOrigen = e->huellaOrigen;
        huellaStaging = e->huellaStaging;
    }
    auto promesa = std::make_shared<QPromise<ResultadoSolicitudRestauracion>>();
    QFuture<ResultadoSolicitudRestauracion> futuro = promesa->future();
    promesa->start();
    const auto terminar = [e, promesa](ResultadoSolicitudRestauracion r, bool liberar) {
        if (liberar) {
            QMutexLocker l(&e->mutex);
            e->solicitando = false;
        }
        promesa->addResult(std::move(r));
        promesa->finish();
    };

    // 1. Antes de detener nada: el `.afbak` y el staging siguen como se
    // verificaron. Si no, la preparacion se descarta y hay que volver a
    // verificar (preparar otra vez).
    lanzar([e, s, origen, huellaOrigen, huellaStaging, terminar] {
        std::optional<ErrorRestauracion> cambio;
        if (auto h = origen->huella(); !h || !(h.valor() == huellaOrigen)) {
            cambio = error(C::OrigenCambio, "origen.cambio");
        } else if (auto hs = e->d.area->huella(s); !hs || hs.valor() != huellaStaging) {
            cambio = error(C::StagingCambio, "staging.cambio");
        }
        if (cambio) {
            {
                QMutexLocker l(&e->mutex);
                e->preparado.reset();
                e->origenPreparado.reset();
            }
            e->d.area->descartar(s);
            terminar(solicitud(EstadoSolicitud::Rechazada, cambio), true);
            return;
        }
        // 2. Hilo grafico: detener la app (worker, ejecutor, respaldo).
        e->enGrafico([e, s, terminar] {
            QFuture<void> detenida = e->d.detenerApp();
            RestauracionService* q = nullptr;
            {
                QMutexLocker l(&e->mutex);
                q = e->q;
            }
            detenida.then(q, [e, s, terminar] {
                // 3. Marcador `solicitado` (reverifica el staging) fuera del grafico.
                lanzar([e, s, terminar] {
                    auto marcado = e->d.area->marcarSolicitado(s);
                    // 4. Hilo grafico: relanzamiento y salida.
                    e->enGrafico([e, marcado = std::move(marcado), terminar]() mutable {
                        const bool relanzado = e->d.relanzar && e->d.relanzar();
                        {
                            QMutexLocker l(&e->mutex);
                            e->preparado.reset();
                            e->origenPreparado.reset();
                        }
                        if (marcado) {
                            terminar(solicitud(relanzado ? EstadoSolicitud::Relanzando : EstadoSolicitud::SinRelanzamiento),
                                     false);
                        } else {
                            // La app ya no opera: sale (y relanza) sin restaurar.
                            // R1: nada desacoplado al salir; el marcador sigue
                            // en `preparado` y el siguiente arranque descarta
                            // el staging.
                            terminar(solicitud(EstadoSolicitud::FalloTrasDetener, marcado.error()), false);
                        }
                        if (e->d.salir) {
                            e->enGrafico([e] { e->d.salir(); });
                        }
                    });
                });
            });
        });
    });
    return futuro;
}

int RestauracionService::hilosDesacopladosVivos()
{
    return g_hilosVivos.load();
}

void RestauracionService::cerrar()
{
    m_estado->cerrando.store(true);
    cancelar();
    QMutexLocker l(&m_estado->mutex);
    m_estado->q = nullptr;
    // Una preparacion lista sin solicitar queda con su marcador `preparado`:
    // el siguiente arranque la descarta. Una solicitada la aplica.
}

} // namespace satcfdi::respaldo
