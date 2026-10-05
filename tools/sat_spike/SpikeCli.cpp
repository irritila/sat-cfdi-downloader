#include "SpikeCli.h"

#include "infrastructure/crypto/EFirmaOpenSsl.h"
#include "infrastructure/sat/ClienteHttpSat.h"
#include "infrastructure/sat/EnmascaradorEvidencia.h"
#include "infrastructure/sat/RespuestasSat.h"
#include "infrastructure/sat/SatOperaciones.h"
#include "infrastructure/sat/SobresSat.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

#include <optional>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace satcfdi::spike {

namespace {

using sat::Operacion;

// --- Argumentos ------------------------------------------------------------

struct Argumentos {
    QString modo; // dry-run | autentica | solicita | verifica | descarga
    QString cer;
    QString key;
    QString salida;
    QString tipo; // emitidos | recibidos
    QString desde;
    QString hasta;
    QString idSolicitud;
    QString idPaquete;
    std::optional<sat::VarianteC14n> c14n;
    std::optional<sat::VarianteC14n> c14nDeclarada;
    std::optional<sat::FormatoIssuer> issuer;
    bool wsAddressing = false;
    bool ultima = false;     // verifica: IdSolicitud de la ultima solicitud aceptada (ledger)
    int paquete = 0;         // descarga: n-esimo IdPaquete de la ultima verificacion (1..)
};

// IdSolicitud: UUID (36 caracteres con guiones). IdPaquete: <UUID>_<NN>
// (docs/web-service.md §6.2 y phpcfdi: "4E80345D-917F-40BB-A98F-4A73939343C5_01").
const QRegularExpression& patronUuid()
{
    static const QRegularExpression re(QStringLiteral(
        "^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"));
    return re;
}

bool esIdSolicitud(const QString& v)
{
    return patronUuid().match(v).hasMatch();
}

bool esIdPaquete(const QString& v)
{
    const qsizetype guion = v.lastIndexOf(QLatin1Char('_'));
    if (guion <= 0 || guion == v.size() - 1) {
        return false;
    }
    const QString sufijo = v.mid(guion + 1);
    for (const QChar c : sufijo) {
        if (!c.isDigit() || c.unicode() > 127) {
            return false;
        }
    }
    return sufijo.size() <= 4 && esIdSolicitud(v.left(guion));
}

std::optional<sat::VarianteC14n> varianteDe(const QString& v)
{
    if (v == QLatin1String("exclusiva")) {
        return sat::VarianteC14n::Exclusiva;
    }
    if (v == QLatin1String("inclusiva")) {
        return sat::VarianteC14n::Inclusiva;
    }
    return std::nullopt;
}

// Devuelve mensaje de error (vacio = ok).
QString parsear(const QStringList& args, Argumentos& a)
{
    const QStringList modos{QStringLiteral("autentica"), QStringLiteral("solicita"), QStringLiteral("verifica"),
                            QStringLiteral("descarga")};
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString& arg = args.at(i);
        auto valor = [&](QString& destino) -> bool {
            if (i + 1 >= args.size()) {
                return false;
            }
            destino = args.at(++i);
            return true;
        };
        bool ok = true;
        if (arg == QLatin1String("--dry-run")) {
            if (!a.modo.isEmpty()) {
                return QStringLiteral("solo un modo por ejecucion");
            }
            a.modo = QStringLiteral("dry-run");
        } else if (modos.contains(arg)) {
            if (!a.modo.isEmpty()) {
                return QStringLiteral("solo un modo por ejecucion");
            }
            a.modo = arg;
        } else if (arg == QLatin1String("--cer")) {
            ok = valor(a.cer);
        } else if (arg == QLatin1String("--key")) {
            ok = valor(a.key);
        } else if (arg == QLatin1String("--salida")) {
            ok = valor(a.salida);
        } else if (arg == QLatin1String("--tipo")) {
            ok = valor(a.tipo);
        } else if (arg == QLatin1String("--desde")) {
            ok = valor(a.desde);
        } else if (arg == QLatin1String("--hasta")) {
            ok = valor(a.hasta);
        } else if (arg == QLatin1String("--id")) {
            ok = valor(a.idSolicitud);
        } else if (arg == QLatin1String("--ultima")) {
            a.ultima = true;
        } else if (arg == QLatin1String("--paquete")) {
            QString v;
            ok = valor(v);
            bool numero = false;
            a.paquete = v.toInt(&numero);
            if (ok && (!numero || a.paquete < 1)) {
                return QStringLiteral("--paquete requiere un numero 1, 2, ...");
            }
        } else if (arg == QLatin1String("--id-paquete")) {
            ok = valor(a.idPaquete);
        } else if (arg == QLatin1String("--c14n") || arg == QLatin1String("--c14n-declarada")) {
            QString v;
            ok = valor(v);
            const auto variante = varianteDe(v);
            if (ok && !variante) {
                return QStringLiteral("%1 acepta exclusiva|inclusiva").arg(arg);
            }
            (arg == QLatin1String("--c14n") ? a.c14n : a.c14nDeclarada) = variante;
        } else if (arg == QLatin1String("--issuer")) {
            QString v;
            ok = valor(v);
            if (v == QLatin1String("docsat")) {
                a.issuer = sat::FormatoIssuer::DocSat;
            } else if (v == QLatin1String("rfc4514")) {
                a.issuer = sat::FormatoIssuer::Rfc4514;
            } else if (ok) {
                return QStringLiteral("--issuer acepta docsat|rfc4514");
            }
        } else if (arg == QLatin1String("--ws-addressing")) {
            a.wsAddressing = true;
        } else {
            // No se reproduce el texto: podria ser una contrasena pegada por error.
            return QStringLiteral("argumento no reconocido en la posicion %1 (no se muestra)").arg(i);
        }
        if (!ok) {
            return QStringLiteral("falta el valor de %1").arg(arg);
        }
    }
    if (a.modo.isEmpty()) {
        return QStringLiteral("falta el modo");
    }
    if (a.cer.isEmpty() || a.key.isEmpty()) {
        return QStringLiteral("--cer y --key son obligatorios");
    }
    if (a.modo == QLatin1String("solicita")) {
        if (a.tipo != QLatin1String("emitidos") && a.tipo != QLatin1String("recibidos")) {
            return QStringLiteral("solicita requiere --tipo emitidos|recibidos");
        }
        if (a.desde.isEmpty()) {
            return QStringLiteral("solicita requiere --desde AAAA-MM-DD");
        }
    }
    if (a.modo == QLatin1String("verifica") && !a.idSolicitud.isEmpty() && a.ultima) {
        return QStringLiteral("--id y --ultima son mutuamente excluyentes");
    }
    if (a.modo == QLatin1String("descarga") && !a.idPaquete.isEmpty() && a.paquete > 0) {
        return QStringLiteral("--id-paquete y --paquete son mutuamente excluyentes");
    }
    if ((a.ultima && a.modo != QLatin1String("verifica")) || (a.paquete > 0 && a.modo != QLatin1String("descarga"))) {
        return QStringLiteral("--ultima solo aplica a verifica y --paquete solo a descarga");
    }
    // Formato (sin red ni contrasena; el valor no se reproduce).
    if (!a.idSolicitud.isEmpty() && !esIdSolicitud(a.idSolicitud)) {
        return QStringLiteral("--id no tiene formato de IdSolicitud (UUID); revise el ledger o use --ultima");
    }
    if (!a.idPaquete.isEmpty() && !esIdPaquete(a.idPaquete)) {
        return QStringLiteral("--id-paquete no tiene formato de IdPaquete (UUID_NN); use --paquete N");
    }
    if (a.modo == QLatin1String("verifica") && a.idSolicitud.isEmpty() && !a.ultima) {
        return QStringLiteral("verifica requiere --id UUID o --ultima");
    }
    if (a.modo == QLatin1String("descarga") && a.idPaquete.isEmpty() && a.paquete == 0) {
        return QStringLiteral("descarga requiere --id-paquete UUID_NN o --paquete N");
    }
    return {};
}

sat::OpcionesFirma opcionesPara(Operacion op, const Argumentos& a)
{
    sat::OpcionesFirma o = sat::OpcionesFirma::porDefecto(op);
    if (a.c14n) {
        o.c14n = *a.c14n;
    }
    if (a.c14nDeclarada) {
        o.c14nDeclarada = a.c14nDeclarada;
    }
    if (a.issuer) {
        o.formatoIssuer = *a.issuer;
    }
    if (op == Operacion::Autentica) {
        o.wsAddressing = a.wsAddressing; // To/Action WS-Addressing (solo Autentica)
    }
    return o;
}

QString textoC14n(sat::VarianteC14n v)
{
    return v == sat::VarianteC14n::Exclusiva ? QStringLiteral("exclusiva") : QStringLiteral("inclusiva");
}

QString textoOpciones(const sat::OpcionesFirma& o)
{
    return QStringLiteral("c14n=%1 declarada=%2 issuer=%3")
        .arg(textoC14n(o.c14n), textoC14n(o.declarada()),
             o.formatoIssuer == sat::FormatoIssuer::DocSat ? QStringLiteral("docsat") : QStringLiteral("rfc4514"));
}

// --- Material -------------------------------------------------------------

std::optional<BufferSecreto> leerArchivoSecreto(const QString& ruta, std::size_t maximo)
{
    QFile f(ruta);
    if (!f.open(QIODevice::ReadOnly) || f.size() <= 0 || static_cast<std::size_t>(f.size()) > maximo) {
        return std::nullopt;
    }
    QByteArray bytes = f.readAll();
    BufferSecreto b = BufferSecreto::desdeBytes(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    secretos::limpiarMemoria(bytes.data(), static_cast<std::size_t>(bytes.size()));
    return b;
}

struct MaterialCargado {
    std::optional<MaterialFirma> material;
    QString rfc;
    QDateTime vigenteHasta;
};

// --- Espera de futures ----------------------------------------------------

template <typename T>
T esperar(QFuture<T> f)
{
    if (!f.isFinished()) {
        QFutureWatcher<T> w;
        QEventLoop bucle;
        QObject::connect(&w, &QFutureWatcher<T>::finished, &bucle, &QEventLoop::quit);
        w.setFuture(f);
        if (!f.isFinished()) {
            bucle.exec();
        }
    }
    return f.result();
}

// --- Archivos privados ----------------------------------------------------

// Crea (si falta) `dir` y confirma con lstat que es un directorio propio, no
// enlace, en 0700. Falla cerrado.
bool directorioPrivado(const QString& dir, QString& error)
{
    if (!QDir().mkpath(dir)) {
        error = QStringLiteral("no se pudo crear el directorio de salida");
        return false;
    }
    const QByteArray ruta = QFile::encodeName(dir);
    ::chmod(ruta.constData(), S_IRWXU);
    struct stat st {};
    if (::lstat(ruta.constData(), &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != ::getuid()
        || (st.st_mode & 0777) != 0700) {
        error = QStringLiteral("el directorio de salida no es propio o no tiene permisos 0700");
        return false;
    }
    return true;
}

// Abre (crea si falta) un archivo 0600 sin seguir enlaces y confirma con
// fstat que es regular, propio y 0600. -1 = error.
int abrirPrivado(const QString& ruta, int banderas, QString& error)
{
    const int fd = ::open(QFile::encodeName(ruta).constData(), banderas | O_CREAT | O_NOFOLLOW | O_CLOEXEC,
                          S_IRUSR | S_IWUSR);
    if (fd < 0) {
        error = QStringLiteral("no se pudo abrir %1").arg(QFileInfo(ruta).fileName());
        return -1;
    }
    ::fchmod(fd, S_IRUSR | S_IWUSR);
    struct stat st {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != ::getuid() || (st.st_mode & 0777) != 0600) {
        ::close(fd);
        error = QStringLiteral("%1 no es un archivo propio con permisos 0600").arg(QFileInfo(ruta).fileName());
        return -1;
    }
    return fd;
}

bool escribirTodo(int fd, const QByteArray& datos)
{
    qsizetype escrito = 0;
    while (escrito < datos.size()) {
        const ssize_t n = ::write(fd, datos.constData() + escrito, static_cast<size_t>(datos.size() - escrito));
        if (n <= 0) {
            return false;
        }
        escrito += n;
    }
    return ::fsync(fd) == 0;
}

// --- Ledger ---------------------------------------------------------------

// Ledger JSONL append-only con lock exclusivo (flock) sobre ledger.lock que
// se conserva mientras viva el objeto (toda la operacion real).
class Ledger {
public:
    explicit Ledger(QString directorio)
        : m_dir(std::move(directorio))
    {
    }
    ~Ledger()
    {
        if (m_lock >= 0) {
            ::flock(m_lock, LOCK_UN);
            ::close(m_lock);
        }
    }
    Ledger(const Ledger&) = delete;
    Ledger& operator=(const Ledger&) = delete;

    QString ruta() const { return QDir(m_dir).filePath(QStringLiteral("ledger.jsonl")); }
    const QString& directorio() const { return m_dir; }

    // Prepara el directorio (0700), toma el lock y lee/valida el ledger.
    bool abrir(QString& error)
    {
        if (!directorioPrivado(m_dir, error)) {
            return false;
        }
        m_lock = abrirPrivado(QDir(m_dir).filePath(QStringLiteral("ledger.lock")), O_RDWR, error);
        if (m_lock < 0) {
            return false;
        }
        if (::flock(m_lock, LOCK_EX | LOCK_NB) != 0) {
            error = QStringLiteral("el ledger esta en uso por otro proceso; intente cuando termine");
            return false;
        }
        QFile f(ruta());
        if (f.exists()) {
            if (!f.open(QIODevice::ReadOnly)) {
                error = QStringLiteral("no se pudo leer el ledger");
                return false;
            }
            int numero = 0;
            while (!f.atEnd()) {
                ++numero;
                const QByteArray linea = f.readLine().trimmed();
                if (linea.isEmpty()) {
                    continue;
                }
                QJsonParseError e{};
                const QJsonDocument doc = QJsonDocument::fromJson(linea, &e);
                if (e.error != QJsonParseError::NoError || !doc.isObject()
                    || !doc.object().value(QStringLiteral("evento")).isString()) {
                    error = QStringLiteral("ledger corrupto (linea %1); revise el archivo, no se envia nada")
                                .arg(numero);
                    return false;
                }
                m_entradas.append(doc.object());
            }
        }
        return true;
    }

    const QList<QJsonObject>& entradas() const { return m_entradas; }

    // Registros de `evento` que no terminaron AntesDeEnvio (el SAT los
    // recibio o es incierto). Para solicitudes, verificaciones y descargas.
    QList<QJsonObject> enviados(const QString& evento) const
    {
        QSet<QString> noEnviados;
        for (const QJsonObject& e : m_entradas) {
            if (e.value(QStringLiteral("evento")) == QLatin1String("resultado")
                && e.value(QStringLiteral("fase")) == QLatin1String("AntesDeEnvio")) {
                noEnviados.insert(e.value(QStringLiteral("registro")).toString());
            }
        }
        QList<QJsonObject> r;
        for (const QJsonObject& e : m_entradas) {
            if (e.value(QStringLiteral("evento")) == evento
                && !noEnviados.contains(e.value(QStringLiteral("registro")).toString())) {
                r.append(e);
            }
        }
        return r;
    }

    bool agregar(QJsonObject entrada, const QDateTime& ahoraUtc, QString& error)
    {
        Q_ASSERT(m_lock >= 0);
        const int fd = abrirPrivado(ruta(), O_WRONLY | O_APPEND, error);
        if (fd < 0) {
            return false;
        }
        entrada.insert(QStringLiteral("en"), ahoraUtc.toString(Qt::ISODateWithMs));
        const QByteArray linea = QJsonDocument(entrada).toJson(QJsonDocument::Compact) + '\n';
        const bool ok = escribirTodo(fd, linea);
        ::close(fd);
        if (!ok) {
            error = QStringLiteral("no se pudo escribir el ledger");
            return false;
        }
        m_entradas.append(entrada);
        return true;
    }

private:
    QString m_dir;
    int m_lock = -1;
    QList<QJsonObject> m_entradas;
};

// --- Ejecucion ------------------------------------------------------------

class Ejecucion {
public:
    Ejecucion(const Argumentos& a, const DependenciasSpike& d)
        : m_a(a)
        , m_d(d)
    {
    }

    int correr();

private:
    // Toda salida pasa por el enmascarado de evidencia y, ademas, oculta los
    // valores reales conocidos de esta ejecucion (RFC, IdSolicitud, IdPaquete).
    QString seguro(const QString& texto, const QStringList& extra = {}) const
    {
        QStringList reales = extra;
        for (const QString& v : {m_m.rfc, m_idSolicitud, m_idPaquete}) {
            if (!v.isEmpty()) {
                reales.append(v);
            }
        }
        return sat::evidencia::ocultarValores(sat::evidencia::enmascarar(texto), reales);
    }
    void decir(const QString& texto) const { m_d.imprimir(seguro(texto)); }
    void error(const QString& texto) const { m_d.imprimirError(seguro(texto)); }

    int cargarMaterial();
    int dryRun();
    bool confirmar(const QString& operacion, const QString& detalle);
    std::optional<sat::TokenSat> autenticar(int& salida);
    int solicitar();
    int verificar();
    int descargar();
    int resolverDesdeLedger();
    QString directorioSalida() const;
    bool anotar(QJsonObject entrada);
    sat::ResultadoHttp enviar(Operacion op, const QByteArray& sobre, const sat::TokenSat* token);
    void reportarHttp(const sat::ResultadoHttp& r) const;
    void reportarError(const sat::ErrorRespuesta& e) const;

    const Argumentos& m_a;
    const DependenciasSpike& m_d;
    MaterialCargado m_m;
    std::unique_ptr<Ledger> m_ledger; // lock tomado durante toda la operacion real
    std::unique_ptr<sat::ClienteHttpSat> m_cliente;
    // Ids efectivos (de --id/--id-paquete o resueltos del ledger).
    QString m_idSolicitud;
    QString m_idPaquete;
};

int Ejecucion::cargarMaterial()
{
    for (const QString& ruta : {m_a.cer, m_a.key}) {
        const QFileInfo info(ruta);
        if (!info.isFile()) {
            error(QStringLiteral("error: no existe el archivo %1").arg(info.fileName()));
            return kSalidaUso;
        }
        if (rutaDentroDe(info.canonicalFilePath(), m_d.raizRepositorio)) {
            error(QStringLiteral("error: %1 esta dentro del repositorio; use una ruta fuera del repo")
                      .arg(info.fileName()));
            return kSalidaUso;
        }
    }
    auto cer = leerArchivoSecreto(m_a.cer, crypto::kMaxCertificadoDer);
    auto key = leerArchivoSecreto(m_a.key, crypto::kMaxLlaveDer);
    if (!cer || !key) {
        error(QStringLiteral("error: no se pudo leer el .cer o el .key"));
        return kSalidaUso;
    }
    std::optional<BufferSecreto> contrasena = m_d.leerContrasena ? m_d.leerContrasena() : std::nullopt;
    if (!contrasena || contrasena->vacio()) {
        error(QStringLiteral("error: contrasena no proporcionada"));
        return kSalidaUso;
    }

    const auto info = crypto::leerCertificado(cer->datos(), cer->tamano());
    if (!info) {
        error(QStringLiteral("error: e.firma invalida: %1").arg(claveEstable(info.error().categoria)));
        return kSalidaMaterial;
    }
    const QDateTime ahora = m_d.relojUtc();
    const auto validada = crypto::validarEFirma(*cer, *key, *contrasena, info.valor().rfc, ahora);
    if (!validada) {
        error(QStringLiteral("error: e.firma invalida: %1").arg(claveEstable(validada.error().categoria)));
        return kSalidaMaterial;
    }
    auto llave = crypto::descifrarLlavePkcs8(*key, *contrasena);
    if (!llave) {
        error(QStringLiteral("error: e.firma invalida: %1").arg(claveEstable(llave.error().categoria)));
        return kSalidaMaterial;
    }
    m_m.rfc = info.valor().rfc;
    m_m.vigenteHasta = validada.valor().metadata.vigenteHasta;
    m_m.material.emplace(std::move(*cer), std::move(llave).valor(), std::move(*contrasena));
    decir(QStringLiteral("e.firma valida: RFC %1, vigente hasta %2")
              .arg(sat::evidencia::rfcParaConsola(m_m.rfc), m_m.vigenteHasta.toString(Qt::ISODate)));
    return kSalidaOk;
}

QString Ejecucion::directorioSalida() const
{
    return QDir::cleanPath(QDir(m_a.salida.isEmpty() ? m_d.directorioSalidaPorDefecto : m_a.salida).absolutePath());
}

bool Ejecucion::anotar(QJsonObject entrada)
{
    QString e;
    if (!m_ledger->agregar(std::move(entrada), m_d.relojUtc(), e)) {
        error(QStringLiteral("error: %1").arg(e));
        return false;
    }
    return true;
}

bool Ejecucion::confirmar(const QString& operacion, const QString& detalle)
{
    m_d.imprimir(QStringLiteral("OPERACION REAL contra el SAT (produccion): %1").arg(operacion));
    m_d.imprimir(QStringLiteral("  RFC: %1").arg(sat::evidencia::rfcParaConsola(m_m.rfc)));
    if (!detalle.isEmpty()) {
        m_d.imprimir(seguro(QStringLiteral("  %1").arg(detalle)));
    }
    m_d.imprimir(QStringLiteral("Escriba yes para continuar:"));
    const std::optional<QString> linea = m_d.leerLinea ? m_d.leerLinea() : std::nullopt;
    if (!linea || linea->trimmed() != QLatin1String("yes")) {
        error(QStringLiteral("cancelado: no se confirmo con yes"));
        return false;
    }
    return true;
}

sat::ResultadoHttp Ejecucion::enviar(Operacion op, const QByteArray& sobre, const sat::TokenSat* token)
{
    if (!m_cliente) {
        m_cliente = m_d.crearCliente();
    }
    sat::ResultadoHttp r;
    if (!m_cliente) {
        r.fase = sat::FaseResultado::AntesDeEnvio;
        r.diagnostico = QStringLiteral("SinCliente");
    } else {
        const sat::DescriptorOperacion& d = sat::descriptor(op);
        r = esperar(m_cliente->enviar(sat::PeticionSat{d.endpoint, d.soapAction, sobre}, token));
    }
    reportarHttp(r);
    return r;
}

void Ejecucion::reportarHttp(const sat::ResultadoHttp& r) const
{
    decir(QStringLiteral("fase=%1 http=%2 deadlineVencido=%3 diagnostico=%4")
              .arg(sat::claveEstable(r.fase), r.estadoHttp ? QString::number(*r.estadoHttp) : QStringLiteral("-"),
                   r.deadlineVencido ? QStringLiteral("si") : QStringLiteral("no"),
                   r.diagnostico.isEmpty() ? QStringLiteral("-") : r.diagnostico));
}

void Ejecucion::reportarError(const sat::ErrorRespuesta& e) const
{
    if (e.fault) {
        decir(QStringLiteral("SOAP Fault: faultcode=%1 faultstring=%2 detail=%3")
                  .arg(e.fault->codigo, e.fault->mensaje, e.fault->detalle));
    }
    error(QStringLiteral("respuesta no valida: %1").arg(e.diagnostico));
}

std::optional<sat::TokenSat> Ejecucion::autenticar(int& salida)
{
    salida = kSalidaOk;
    if (!confirmar(QStringLiteral("Autentica"), QStringLiteral("(obtiene un token; vive solo en memoria)"))) {
        salida = kSalidaCancelada;
        return std::nullopt;
    }
    sat::ContextoSobre ctx;
    ctx.ahoraUtc = m_d.relojUtc();
    const sat::OpcionesFirma opciones = opcionesPara(Operacion::Autentica, m_a);
    decir(QStringLiteral("Autentica: %1").arg(textoOpciones(opciones)));
    auto sobre = sat::construirAutentica(*m_m.material, ctx, opciones);
    if (!sobre) {
        error(QStringLiteral("error al construir el sobre: %1").arg(sobre.error().diagnostico));
        salida = kSalidaSat;
        return std::nullopt;
    }
    sat::ResultadoHttp r = enviar(Operacion::Autentica, sobre.valor().xml, nullptr);
    if (r.fase != sat::FaseResultado::RespuestaExplicita) {
        salida = kSalidaSat;
        return std::nullopt;
    }
    // Limpia el cuerpo (contiene el token) en cuanto se parsea.
    auto parseada = sat::parsearAutenticaYLimpiar(r.cuerpo);
    if (!parseada) {
        reportarError(parseada.error());
        salida = kSalidaSat;
        return std::nullopt;
    }
    sat::TokenSat token = std::move(std::move(parseada).valor().token);
    const qint64 ttl = token.creado().isValid() && token.expira().isValid()
                           ? token.creado().secsTo(token.expira())
                           : -1;
    decir(QStringLiteral("token: obtenido (longitud %1) Created=%2 Expires=%3 TTL=%4 s")
              .arg(token.tamano())
              .arg(token.creado().toString(Qt::ISODateWithMs), token.expira().toString(Qt::ISODateWithMs))
              .arg(ttl));
    if (!token.vigenteEn(m_d.relojUtc())) {
        error(QStringLiteral("el token no esta vigente segun el reloj local"));
        salida = kSalidaSat;
        return std::nullopt;
    }
    return token;
}

int Ejecucion::dryRun()
{
    decir(QStringLiteral("== dry-run: sin red y sin escribir en disco"));
    const QDateTime ahora = m_d.relojUtc();
    const QDate dia = m_a.desde.isEmpty() ? ahora.date().addDays(-8) : QDate::fromString(m_a.desde, Qt::ISODate);
    if (!dia.isValid()) {
        error(QStringLiteral("error: --desde invalida"));
        return kSalidaUso;
    }
    sat::ParametrosSolicitud p;
    p.rfcSolicitante = m_m.rfc;
    p.fechaInicial = QDateTime(dia, QTime(0, 0, 0));
    p.fechaFinal = QDateTime(dia, QTime(23, 59, 59));
    sat::ParametrosSolicitud emitidos = p;
    emitidos.rfcEmisor = m_m.rfc;
    sat::ParametrosSolicitud recibidos = p;
    recibidos.rfcReceptor = m_m.rfc;
    const QString idSolicitud = m_idSolicitud.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces)
                                                          : m_idSolicitud;
    const QString idPaquete = m_idPaquete.isEmpty()
                                  ? QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper() + QStringLiteral("_01")
                                  : m_idPaquete;
    sat::ContextoSobre ctx;
    ctx.ahoraUtc = ahora;

    const MaterialFirma& mat = *m_m.material;
    const QList<Operacion> ops = {Operacion::Autentica, Operacion::SolicitaDescargaEmitidos,
                                  Operacion::SolicitaDescargaRecibidos, Operacion::VerificaSolicitudDescarga,
                                  Operacion::Descargar};
    int salida = kSalidaOk;
    for (Operacion op : ops) {
        const sat::OpcionesFirma o = opcionesPara(op, m_a);
        auto sobre = [&]() {
            switch (op) {
            case Operacion::Autentica:
                return sat::construirAutentica(mat, ctx, o);
            case Operacion::SolicitaDescargaEmitidos:
                return sat::construirSolicitudEmitidos(emitidos, mat, o);
            case Operacion::SolicitaDescargaRecibidos:
                return sat::construirSolicitudRecibidos(recibidos, mat, o);
            case Operacion::VerificaSolicitudDescarga:
                return sat::construirVerificacion(idSolicitud, m_m.rfc, mat, o);
            case Operacion::Descargar:
                break;
            }
            return sat::construirDescarga(idPaquete, m_m.rfc, mat, o);
        }();
        const sat::DescriptorOperacion& d = sat::descriptor(op);
        decir(QStringLiteral("-- %1 (%2) endpoint=%3 SOAPAction=%4 token=%5")
                  .arg(d.nombre, textoOpciones(o), d.endpoint.toString(), d.soapAction,
                       d.requiereToken ? QStringLiteral("WRAP") : QStringLiteral("no")));
        if (!sobre) {
            error(QStringLiteral("error al construir %1: %2").arg(d.nombre, sobre.error().diagnostico));
            salida = kSalidaSat;
            continue;
        }
        decir(QString::fromUtf8(sobre.valor().xml));
    }
    decir(QStringLiteral("== dry-run terminado: rango de ejemplo %1 (00:00:00 a 23:59:59)").arg(dia.toString(Qt::ISODate)));
    return salida;
}

int Ejecucion::solicitar()
{
    const QDate desde = QDate::fromString(m_a.desde, Qt::ISODate);
    const QDate hasta = m_a.hasta.isEmpty() ? desde : QDate::fromString(m_a.hasta, Qt::ISODate);
    const QDate hoy = m_d.relojUtc().toLocalTime().date();
    if (!desde.isValid() || !hasta.isValid() || desde != hasta) {
        error(QStringLiteral("error: el spike solo admite un dia completo (--desde AAAA-MM-DD, --hasta igual u omitido)"));
        return kSalidaUso;
    }
    if (desde >= hoy) {
        error(QStringLiteral("error: el dia debe estar cerrado (anterior a hoy)"));
        return kSalidaUso;
    }
    if (desde.daysTo(hoy) <= 7) {
        m_d.imprimir(QStringLiteral("advertencia: la tarea pide un dia de hace mas de 7 dias"));
    }
    const bool esEmitidos = m_a.tipo == QLatin1String("emitidos");
    const Operacion op = esEmitidos ? Operacion::SolicitaDescargaEmitidos : Operacion::SolicitaDescargaRecibidos;
    const QString rango = QStringLiteral("%1T00:00:00 a %1T23:59:59").arg(desde.toString(Qt::ISODate));
    const QString criterio = QStringLiteral("%1|%2|%3|CFDI|Vigente").arg(sat::descriptor(op).nombre, m_m.rfc,
                                                                          desde.toString(Qt::ISODate));
    const QString hashCriterio =
        QString::fromLatin1(QCryptographicHash::hash(criterio.toUtf8(), QCryptographicHash::Sha256).toHex());

    // Validacion del ledger (bajo lock) ANTES de cualquier red.
    const QList<QJsonObject> previas = m_ledger->enviados(QStringLiteral("solicitud"));
    for (const QJsonObject& e : previas) {
        if (e.value(QStringLiteral("criterio")).toString() == hashCriterio) {
            error(QStringLiteral("rechazado: ese criterio ya se registro en el ledger (no se reenvia)"));
            return kSalidaLedger;
        }
    }
    if (previas.size() >= kMaxSolicitudes) {
        error(QStringLiteral("rechazado: tope de %1 solicitudes reales alcanzado").arg(kMaxSolicitudes));
        return kSalidaLedger;
    }
    decir(QStringLiteral("ledger: %1 de %2 solicitudes usadas").arg(previas.size()).arg(kMaxSolicitudes));

    int salida = kSalidaOk;
    std::optional<sat::TokenSat> token = autenticar(salida);
    if (!token) {
        return salida;
    }

    sat::ParametrosSolicitud p;
    p.rfcSolicitante = m_m.rfc;
    p.fechaInicial = QDateTime(desde, QTime(0, 0, 0));
    p.fechaFinal = QDateTime(desde, QTime(23, 59, 59));
    if (esEmitidos) {
        p.rfcEmisor = m_m.rfc;
    } else {
        p.rfcReceptor = m_m.rfc;
    }
    const sat::OpcionesFirma opciones = opcionesPara(op, m_a);
    auto sobre = esEmitidos ? sat::construirSolicitudEmitidos(p, *m_m.material, opciones)
                            : sat::construirSolicitudRecibidos(p, *m_m.material, opciones);
    if (!sobre) {
        error(QStringLiteral("error al construir el sobre: %1").arg(sobre.error().diagnostico));
        return kSalidaSat;
    }
    if (!confirmar(sat::descriptor(op).nombre,
                   QStringLiteral("Rango: %1 | filtros: TipoSolicitud=CFDI EstadoComprobante=Vigente | %2")
                       .arg(rango, textoOpciones(opciones)))) {
        return kSalidaCancelada;
    }
    const QString registro = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString hashSobre = QString::fromLatin1(
        QCryptographicHash::hash(sobre.valor().xml, QCryptographicHash::Sha256).toHex());
    if (!anotar(QJsonObject{{QStringLiteral("evento"), QStringLiteral("solicitud")},
                            {QStringLiteral("registro"), registro},
                            {QStringLiteral("operacion"), sat::descriptor(op).nombre},
                            {QStringLiteral("rfc"), m_m.rfc},
                            {QStringLiteral("rango"), rango},
                            {QStringLiteral("criterio"), hashCriterio},
                            {QStringLiteral("hashSobre"), hashSobre},
                            {QStringLiteral("opciones"), textoOpciones(opciones)}})) {
        error(QStringLiteral("no se envia la solicitud"));
        return kSalidaLedger;
    }

    const sat::ResultadoHttp r = enviar(op, sobre.valor().xml, &*token);
    QJsonObject resultado{{QStringLiteral("evento"), QStringLiteral("resultado")},
                          {QStringLiteral("registro"), registro},
                          {QStringLiteral("fase"), sat::claveEstable(r.fase)}};
    if (r.estadoHttp) {
        resultado.insert(QStringLiteral("http"), *r.estadoHttp);
    }
    if (r.fase == sat::FaseResultado::DespuesDeEnvio) {
        resultado.insert(QStringLiteral("incierta"), true);
        anotar(resultado);
        error(QStringLiteral("RESULTADO INCIERTO (DespuesDeEnvio): NO reenviar; quedo marcado en el ledger"));
        return kSalidaIncierta;
    }
    if (r.fase == sat::FaseResultado::AntesDeEnvio) {
        anotar(resultado);
        error(QStringLiteral("la solicitud no llego al SAT (AntesDeEnvio)"));
        return kSalidaSat;
    }
    auto respuesta = sat::parsearSolicitud(r.cuerpo);
    if (!respuesta) {
        if (respuesta.error().fault) {
            resultado.insert(QStringLiteral("fault"), respuesta.error().fault->codigo);
        }
        anotar(resultado);
        reportarError(respuesta.error());
        return kSalidaSat;
    }
    const sat::RespuestaSolicitud& s = respuesta.valor();
    resultado.insert(QStringLiteral("codEstatus"), s.codEstatus);
    resultado.insert(QStringLiteral("mensaje"), s.mensaje);
    resultado.insert(QStringLiteral("idSolicitud"), s.idSolicitud);
    anotar(resultado);
    decir(QStringLiteral("CodEstatus=%1 Mensaje=%2 IdSolicitud=%3")
              .arg(s.codEstatus, seguro(s.mensaje, {s.idSolicitud}), sat::evidencia::enmascararId(s.idSolicitud)));
    m_d.imprimir(QStringLiteral("El IdSolicitud real quedo en el ledger local (directorio de salida)."));
    return s.codEstatus == QLatin1String("5000") ? kSalidaOk : kSalidaSat;
}

int Ejecucion::verificar()
{
    // Guardias (bajo lock, antes de la red): maximo y espaciado por IdSolicitud.
    const QList<QJsonObject> previas = m_ledger->enviados(QStringLiteral("verificacion"));
    int delMismo = 0;
    QDateTime ultima;
    for (const QJsonObject& e : previas) {
        if (e.value(QStringLiteral("idSolicitud")).toString() == m_idSolicitud) {
            ++delMismo;
            const QDateTime en = QDateTime::fromString(e.value(QStringLiteral("en")).toString(), Qt::ISODateWithMs);
            if (en.isValid() && (!ultima.isValid() || en > ultima)) {
                ultima = en;
            }
        }
    }
    if (delMismo >= kMaxVerificacionesPorSolicitud) {
        error(QStringLiteral("rechazado: ya hay %1 verificaciones de esa solicitud (maximo %2)")
                  .arg(delMismo)
                  .arg(kMaxVerificacionesPorSolicitud));
        return kSalidaLedger;
    }
    if (ultima.isValid()) {
        const qint64 transcurridos = ultima.secsTo(m_d.relojUtc());
        const qint64 minimo = qint64{kMinutosEntreVerificaciones} * 60;
        if (transcurridos < minimo) {
            const qint64 faltan = minimo - transcurridos;
            error(QStringLiteral("rechazado: espere %1 min %2 s antes de verificar de nuevo esa solicitud "
                                 "(minimo %3 min entre verificaciones)")
                      .arg(faltan / 60)
                      .arg(faltan % 60)
                      .arg(kMinutosEntreVerificaciones));
            return kSalidaLedger;
        }
    }
    decir(QStringLiteral("ledger: %1 de %2 verificaciones de esa solicitud")
              .arg(delMismo)
              .arg(kMaxVerificacionesPorSolicitud));

    int salida = kSalidaOk;
    std::optional<sat::TokenSat> token = autenticar(salida);
    if (!token) {
        return salida;
    }
    const sat::OpcionesFirma opciones = opcionesPara(Operacion::VerificaSolicitudDescarga, m_a);
    auto sobre = sat::construirVerificacion(m_idSolicitud, m_m.rfc, *m_m.material, opciones);
    if (!sobre) {
        error(QStringLiteral("error al construir el sobre: %1").arg(sobre.error().diagnostico));
        return kSalidaSat;
    }
    if (!confirmar(QStringLiteral("VerificaSolicitudDescarga"),
                   QStringLiteral("IdSolicitud: %1 | %2").arg(m_idSolicitud, textoOpciones(opciones)))) {
        return kSalidaCancelada;
    }
    const QString registro = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!anotar(QJsonObject{{QStringLiteral("evento"), QStringLiteral("verificacion")},
                            {QStringLiteral("registro"), registro},
                            {QStringLiteral("idSolicitud"), m_idSolicitud}})) {
        return kSalidaLedger;
    }
    const sat::ResultadoHttp r = enviar(Operacion::VerificaSolicitudDescarga, sobre.valor().xml, &*token);
    QJsonObject e{{QStringLiteral("evento"), QStringLiteral("resultado")},
                  {QStringLiteral("registro"), registro},
                  {QStringLiteral("fase"), sat::claveEstable(r.fase)}};
    if (r.fase != sat::FaseResultado::RespuestaExplicita) {
        anotar(e);
        return kSalidaSat;
    }
    auto v = sat::parsearVerificacion(r.cuerpo);
    if (!v) {
        anotar(e);
        reportarError(v.error());
        return kSalidaSat;
    }
    const sat::RespuestaVerificacion& rv = v.valor();
    e.insert(QStringLiteral("codEstatus"), rv.codEstatus);
    e.insert(QStringLiteral("estadoSolicitud"), rv.estadoSolicitud ? *rv.estadoSolicitud : -1);
    e.insert(QStringLiteral("codigoEstadoSolicitud"), rv.codigoEstadoSolicitud);
    e.insert(QStringLiteral("numeroCfdis"), rv.numeroCfdis ? *rv.numeroCfdis : -1);
    e.insert(QStringLiteral("idsPaquetes"), QJsonArray::fromStringList(rv.idsPaquetes));
    anotar(e);
    decir(QStringLiteral("CodEstatus=%1 Mensaje=%2 EstadoSolicitud=%3 CodigoEstadoSolicitud=%4 NumeroCFDIs=%5 "
                         "IdsPaquetes=%6")
              .arg(rv.codEstatus, seguro(rv.mensaje, rv.idsPaquetes),
                   rv.estadoSolicitud ? QString::number(*rv.estadoSolicitud) : QStringLiteral("-"),
                   rv.codigoEstadoSolicitud,
                   rv.numeroCfdis ? QString::number(*rv.numeroCfdis) : QStringLiteral("-"),
                   rv.idsPaquetes.join(u',')));
    return kSalidaOk;
}

int Ejecucion::descargar()
{
    // Guardia (bajo lock, antes de la red): una sola descarga GUARDADA por
    // IdPaquete. Si un intento anterior no logro guardar el ZIP, se permite
    // repetir la descarga de ese paquete.
    if (paqueteYaGuardado(m_ledger->entradas(), m_idPaquete)) {
        error(QStringLiteral("rechazado: ese paquete ya se descargo y guardo (una sola descarga por IdPaquete)"));
        return kSalidaLedger;
    }
    int salida = kSalidaOk;
    std::optional<sat::TokenSat> token = autenticar(salida);
    if (!token) {
        return salida;
    }
    const sat::OpcionesFirma opciones = opcionesPara(Operacion::Descargar, m_a);
    auto sobre = sat::construirDescarga(m_idPaquete, m_m.rfc, *m_m.material, opciones);
    if (!sobre) {
        error(QStringLiteral("error al construir el sobre: %1").arg(sobre.error().diagnostico));
        return kSalidaSat;
    }
    if (!confirmar(QStringLiteral("Descargar"),
                   QStringLiteral("IdPaquete: %1 | %2").arg(m_idPaquete, textoOpciones(opciones)))) {
        return kSalidaCancelada;
    }
    // Archivo nombrado por hash: el IdPaquete real solo queda en el ledger.
    const QString archivo =
        QStringLiteral("paquete-%1.zip")
            .arg(QString::fromLatin1(
                QCryptographicHash::hash(m_idPaquete.toUtf8(), QCryptographicHash::Sha256).toHex().left(16)));
    const QString registro = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!anotar(QJsonObject{{QStringLiteral("evento"), QStringLiteral("descarga")},
                            {QStringLiteral("registro"), registro},
                            {QStringLiteral("idPaquete"), m_idPaquete},
                            {QStringLiteral("archivo"), archivo}})) {
        return kSalidaLedger;
    }
    const sat::ResultadoHttp r = enviar(Operacion::Descargar, sobre.valor().xml, &*token);
    QJsonObject e{{QStringLiteral("evento"), QStringLiteral("resultado")},
                  {QStringLiteral("registro"), registro},
                  {QStringLiteral("fase"), sat::claveEstable(r.fase)}};
    if (r.fase != sat::FaseResultado::RespuestaExplicita) {
        anotar(e);
        return kSalidaSat;
    }
    auto d = sat::parsearDescarga(r.cuerpo);
    if (!d) {
        anotar(e);
        reportarError(d.error());
        return kSalidaSat;
    }
    sat::RespuestaDescarga rd = std::move(d).valor();
    e.insert(QStringLiteral("codEstatus"), rd.codEstatus);
    e.insert(QStringLiteral("tamanoPaquete"), static_cast<qint64>(rd.tamanoPaquete));
    decir(QStringLiteral("CodEstatus=%1 Mensaje=%2 Paquete=%3 bytes")
              .arg(rd.codEstatus, seguro(rd.mensaje))
              .arg(rd.tamanoPaquete));
    bool guardado = false;
    if (rd.paquete.isEmpty()) {
        error(QStringLiteral("error: la respuesta no trae Paquete; no hay ZIP que guardar"));
    } else {
        const ResultadoGuardado g = guardarPaquete(m_ledger->directorio(), archivo, rd.paquete, m_d.escribirArchivo);
        guardado = g.guardado;
        if (guardado) {
            m_d.imprimir(seguro(QStringLiteral("ZIP guardado (0600, fuera del repo): %1").arg(g.ruta)));
        } else {
            error(QStringLiteral("ERROR: el paquete se descargo pero NO se guardo (%1). La descarga de ese "
                                 "IdPaquete sigue permitida: corrija el problema y repitala.")
                      .arg(g.error));
        }
    }
    e.insert(QStringLiteral("guardado"), guardado);
    anotar(e);
    if (rd.codEstatus != QLatin1String("5000")) {
        return kSalidaSat;
    }
    return guardado ? kSalidaOk : kSalidaPaqueteNoGuardado;
}

// --ultima / --paquete N: toma los Ids del ledger (bajo lock, sin red). Los
// valores no se muestran; solo su forma enmascarada.
int Ejecucion::resolverDesdeLedger()
{
    const QList<QJsonObject>& entradas = m_ledger->entradas();
    if (m_a.ultima) {
        for (auto it = entradas.crbegin(); it != entradas.crend(); ++it) {
            const QString id = it->value(QStringLiteral("idSolicitud")).toString();
            if (it->value(QStringLiteral("evento")) == QLatin1String("resultado")
                && it->value(QStringLiteral("codEstatus")) == QLatin1String("5000") && esIdSolicitud(id)) {
                m_idSolicitud = id;
                break;
            }
        }
        if (m_idSolicitud.isEmpty()) {
            error(QStringLiteral("error: el ledger no tiene ninguna solicitud aceptada (CodEstatus=5000)"));
            return kSalidaUso;
        }
        decir(QStringLiteral("usando la ultima solicitud aceptada del ledger: %1")
                  .arg(sat::evidencia::enmascararId(m_idSolicitud)));
    }
    if (m_a.paquete > 0) {
        QStringList paquetes;
        for (auto it = entradas.crbegin(); it != entradas.crend() && paquetes.isEmpty(); ++it) {
            if (it->value(QStringLiteral("evento")) == QLatin1String("resultado")) {
                for (const QJsonValue& v : it->value(QStringLiteral("idsPaquetes")).toArray()) {
                    paquetes.append(v.toString());
                }
            }
        }
        if (paquetes.isEmpty()) {
            error(QStringLiteral("error: el ledger no tiene ninguna verificacion con paquetes"));
            return kSalidaUso;
        }
        if (m_a.paquete > paquetes.size()) {
            error(QStringLiteral("error: la ultima verificacion tiene %1 paquete(s); --paquete %2 no existe")
                      .arg(paquetes.size())
                      .arg(m_a.paquete));
            return kSalidaUso;
        }
        m_idPaquete = paquetes.at(m_a.paquete - 1);
        if (!esIdPaquete(m_idPaquete)) {
            error(QStringLiteral("error: el IdPaquete del ledger no tiene el formato esperado"));
            return kSalidaLedger;
        }
        decir(QStringLiteral("usando el paquete %1 de %2 de la ultima verificacion: %3")
                  .arg(m_a.paquete)
                  .arg(paquetes.size())
                  .arg(sat::evidencia::enmascararId(m_idPaquete)));
    }
    return kSalidaOk;
}

int Ejecucion::correr()
{
    m_idSolicitud = m_a.idSolicitud;
    m_idPaquete = m_a.idPaquete;
    const bool real = m_a.modo != QLatin1String("dry-run");
    if (real && rutaDentroDe(directorioSalida(), m_d.raizRepositorio)) {
        // Antes de pedir la contrasena: el ledger y los ZIP nunca van al repo.
        error(QStringLiteral("error: el directorio de salida esta dentro del repositorio"));
        return kSalidaUso;
    }
    if (m_a.c14nDeclarada) {
        m_d.imprimir(QStringLiteral(
            "AVISO: --c14n-declarada es EXPERIMENTAL y no conforme a XMLDSig (declara una C14N distinta de la "
            "calculada); solo para diagnostico. Registrela aparte en la evidencia."));
    }
    if (const int r = cargarMaterial(); r != kSalidaOk) {
        return r;
    }
    if (!real) {
        return dryRun();
    }
    m_ledger = std::make_unique<Ledger>(directorioSalida());
    if (QString e; !m_ledger->abrir(e)) {
        error(QStringLiteral("error: %1").arg(e));
        return kSalidaLedger;
    }
    if (m_a.ultima || m_a.paquete > 0) {
        if (const int r = resolverDesdeLedger(); r != kSalidaOk) {
            return r;
        }
    }
    if (m_a.modo == QLatin1String("autentica")) {
        int salida = kSalidaOk;
        return autenticar(salida) ? kSalidaOk : salida;
    }
    if (m_a.modo == QLatin1String("solicita")) {
        return solicitar();
    }
    if (m_a.modo == QLatin1String("verifica")) {
        return verificar();
    }
    return descargar();
}

} // namespace

ResultadoGuardado guardarPaquete(const QString& dirSalida, const QString& archivo, const QByteArray& datos,
                                 const EscritorArchivo& escritor)
{
    ResultadoGuardado r;
    const QString dirPaquetes = QDir(dirSalida).filePath(QStringLiteral("paquetes"));
    r.ruta = QDir(dirPaquetes).filePath(archivo);
    if (!directorioPrivado(dirPaquetes, r.error)) {
        return r;
    }
    // O_EXCL: nunca sobrescribe; 0600 confirmado con fstat.
    const int fd = abrirPrivado(r.ruta, O_WRONLY | O_EXCL, r.error);
    if (fd < 0) {
        return r;
    }
    r.guardado = escritor ? escritor(fd, datos) : escribirTodo(fd, datos);
    if (::close(fd) != 0) {
        r.guardado = false;
    }
    if (!r.guardado) {
        QFile::remove(r.ruta);
        r.error = QStringLiteral("fallo la escritura del archivo");
    }
    return r;
}

bool paqueteYaGuardado(const QList<QJsonObject>& entradas, const QString& idPaquete)
{
    QSet<QString> registros;
    for (const QJsonObject& e : entradas) {
        if (e.value(QStringLiteral("evento")) == QLatin1String("descarga")
            && e.value(QStringLiteral("idPaquete")).toString() == idPaquete) {
            registros.insert(e.value(QStringLiteral("registro")).toString());
        }
    }
    for (const QJsonObject& e : entradas) {
        if (e.value(QStringLiteral("evento")) == QLatin1String("resultado")
            && registros.contains(e.value(QStringLiteral("registro")).toString())
            && e.value(QStringLiteral("guardado")).toBool()) {
            return true;
        }
    }
    return false;
}

bool rutaDentroDe(const QString& ruta, const QString& raiz)
{
    if (raiz.isEmpty() || ruta.isEmpty()) {
        return false;
    }
    const QString r = QDir::cleanPath(QFileInfo(raiz).canonicalFilePath().isEmpty() ? raiz
                                                                                    : QFileInfo(raiz).canonicalFilePath());
    QString p = QFileInfo(ruta).canonicalFilePath();
    if (p.isEmpty()) {
        // Aun no existe: canonicalizar el ancestro existente mas cercano.
        QFileInfo info(QDir::cleanPath(QDir(ruta).absolutePath()));
        QStringList resto;
        while (!info.exists() && info.absoluteFilePath() != QStringLiteral("/")) {
            resto.prepend(info.fileName());
            info = QFileInfo(info.absolutePath());
        }
        p = QDir::cleanPath(info.canonicalFilePath() + QLatin1Char('/') + resto.join(QLatin1Char('/')));
    }
    p = QDir::cleanPath(p);
    return p == r || p.startsWith(r + QLatin1Char('/'));
}

QString uso()
{
    return QStringLiteral(
        "uso: satcfdi_sat_spike <modo> --cer <ruta.cer> --key <ruta.key> [opciones]\n"
        "modos:\n"
        "  --dry-run                      construye y muestra los sobres enmascarados (sin red)\n"
        "  autentica                      Autentica (token solo en memoria)\n"
        "  solicita --tipo emitidos|recibidos --desde AAAA-MM-DD [--hasta igual]\n"
        "  verifica --id UUID | --ultima   (--ultima: ultima solicitud aceptada del ledger)\n"
        "  descarga --id-paquete UUID_NN | --paquete N   (N-esimo paquete de la ultima verificacion)\n"
        "opciones:\n"
        "  --salida <dir>                 ledger y paquetes (fuera del repo)\n"
        "  --c14n exclusiva|inclusiva     C14N calculada (default por operacion)\n"
        "  --c14n-declarada exclusiva|inclusiva   EXPERIMENTAL, no conforme a XMLDSig\n"
        "  --issuer docsat|rfc4514        formato de X509IssuerName\n"
        "  --ws-addressing                incluye To/Action WS-Addressing en Autentica\n"
        "La contrasena se pide por prompt sin eco; nunca por argumento ni variable de entorno.");
}

int ejecutarSpike(const QStringList& argumentos, const DependenciasSpike& deps)
{
    Argumentos a;
    if (const QString e = parsear(argumentos, a); !e.isEmpty()) {
        deps.imprimirError(QStringLiteral("error: %1").arg(e));
        deps.imprimirError(uso());
        return kSalidaUso;
    }
    Ejecucion ejecucion(a, deps);
    return ejecucion.correr();
}

} // namespace satcfdi::spike
