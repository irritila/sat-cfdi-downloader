#include "app_core/RestauracionArranque.h"

#include "app_core/AppBootstrapper.h"
#include "app_core/AreaRestauracionLocal.h"
#include "app_core/ArchivosRestauracion.h"
#include "app_core/DatosPreviosRestauracion.h"
#include "app_core/FuenteRespaldoLocal.h"
#include "infrastructure/storage/FileOps.h"
#include "infrastructure/storage/FilesystemPackageStorage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSettings>

#include <cerrno>
#include <memory>
#include <optional>

namespace satcfdi {

namespace {

Q_LOGGING_CATEGORY(lcRestauracion, "satcfdi.restauracion")

using Estado = ResultadoRestauracionArranque::Estado;
using Fase = MarcadorRestauracion::Fase;
namespace ar = archivos_restauracion;
namespace mr = marcador_restauracion;

const QString kBase = QStringLiteral("satcfdi.sqlite3");
const QString kPreferencias = QStringLiteral("preferencias.ini");
const QString kCredenciales = QStringLiteral("credentials");
const QString kPaquetes = QStringLiteral("paquetes");

int orden(Fase f)
{
    return static_cast<int>(f);
}

// Canonica (sin enlaces). Si la entrada no existe (p. ej. la raiz ya movida a
// pre-restauracion), la del padre mas el nombre.
QString canonica(const QString& ruta)
{
    const QString c = QFileInfo(ruta).canonicalFilePath();
    if (!c.isEmpty()) {
        return c;
    }
    const QFileInfo info(QDir::cleanPath(ruta));
    const QString padre = QFileInfo(info.absolutePath()).canonicalFilePath();
    return padre.isEmpty() ? QDir::cleanPath(ruta) : QDir(padre).filePath(info.fileName());
}

// Contexto de una aplicacion: rutas y operaciones.
class Aplicacion {
public:
    Aplicacion(const RestauracionArranque::Opciones& o, FileOps& ops, MarcadorRestauracion marcador)
        : m_o(o), m_ops(ops), m(std::move(marcador)), datos(m.directorioDatos), raiz(m.raizPaquetes),
          padreRaiz(QFileInfo(m.raizPaquetes).absolutePath())
    {
    }

    ResultadoRestauracionArranque ejecutar();

private:
    const RestauracionArranque::Opciones& m_o;
    FileOps& m_ops;
    MarcadorRestauracion m;
    QString datos;
    QString raiz;
    QString padreRaiz;
    const char* paso = "";
    int ultimoErrno = 0;
    bool interrumpida = false;

    // false = corte simulado (pruebas).
    bool continuar(const char* p)
    {
        paso = p;
        if (m_o.puntoDeCorte && !m_o.puntoDeCorte(p)) {
            interrumpida = true;
            return false;
        }
        return true;
    }

    bool existe(const QString& r)
    {
        FileOps::TipoEntrada t{};
        return m_ops.tipo(QFile::encodeName(r), t) == 0;
    }

    bool escribirFase(Fase f, const char* p)
    {
        if (!continuar(p)) {
            return false;
        }
        m.fase = f;
        if (!mr::escribir(datos, m)) {
            ultimoErrno = EIO;
            return false;
        }
        return true;
    }

    // Rename sin reemplazo e idempotente: destino presente (y origen ausente)
    // = ya hecho; origen ausente y destino ausente = no habia nada.
    enum class Mov { Hecho, NoHabia, Fallo };
    Mov mover(const QString& origen, const QString& destino, const char* p)
    {
        const bool hayDestino = existe(destino);
        const bool hayOrigen = existe(origen);
        if (hayDestino) {
            if (hayOrigen) {
                paso = p;
                ultimoErrno = EEXIST;
                return Mov::Fallo;
            }
            return Mov::Hecho;
        }
        if (!hayOrigen) {
            return Mov::NoHabia;
        }
        if (!continuar(p)) {
            return Mov::Fallo;
        }
        if (const int e = m_ops.renombrarSinReemplazo(QFile::encodeName(origen), QFile::encodeName(destino)); e != 0) {
            ultimoErrno = e;
            return Mov::Fallo;
        }
        return Mov::Hecho;
    }

    // fsync de carpetas. R3: con datos y raiz en volumenes distintos no hay un
    // solo diario de volumen que ordene los renames: un fallo es error.
    bool sincronizar(const QStringList& dirs)
    {
        bool ok = true;
        for (const QString& d : dirs) {
            if (existe(d) && !ar::sincronizarDirectorio(d)) {
                ok = false;
            }
        }
        if (!ok && volumenesDistintos()) {
            ultimoErrno = EIO;
            return false;
        }
        return true;
    }
    bool volumenesDistintos() const { return ar::dispositivo(datos) != ar::dispositivo(padreRaiz); }

    QStringList archivosBase() const
    {
        return {kBase, kBase + QStringLiteral("-wal"), kBase + QStringLiteral("-shm")};
    }

    bool elegirPre();
    bool crearPre(bool retomando);
    bool preAjena = false; // una carpeta pre-restauracion no es nuestra: Error sin tocar nada
    bool moverActuales();
    bool promover();
    bool aplicarPreferencias();
    ResultadoRestauracionArranque revertir(const char* motivo);
    ResultadoRestauracionArranque descartar(const char* motivo);
    ResultadoRestauracionArranque descartarSoloDatos(const char* motivo);
    ResultadoRestauracionArranque resultado(Estado e, const QString& diagnostico) const
    {
        ResultadoRestauracionArranque r;
        r.estado = e;
        r.diagnostico = diagnostico;
        r.idRespaldo = m.idRespaldo;
        r.creadoRespaldoUtc = m.creadoRespaldoUtc;
        if (e == Estado::Aplicada) {
            r.preDatos = m.preDatos;
            r.prePaquetes = m.prePaquetes;
        }
        return r;
    }
    ResultadoRestauracionArranque cortada() const
    {
        return resultado(Estado::Interrumpida, QString::fromLatin1(paso));
    }
    bool raizPermitida() const
    {
        for (const QString& r : m_o.raicesPermitidas) {
            if (canonica(r) == raiz) {
                return true;
            }
        }
        return false;
    }
    // Seguridad 1: nombres estrictos `pre-restauracion-<yyyyMMddTHHmmssZ>[-N]`
    // en la carpeta de datos y, con el mismo nombre, junto a la raiz.
    bool preValidas() const
    {
        const Fase efectiva = m.fase == Fase::Revirtiendo ? m.faseAntesDeRevertir : m.fase;
        const bool requiere = orden(efectiva) >= orden(Fase::PreElegida);
        if (m.preDatos.isEmpty() && m.prePaquetes.isEmpty()) {
            return !requiere;
        }
        if (!requiere) {
            return false;
        }
        static const QRegularExpression patron(
            QStringLiteral("^pre-restauracion-(\\d{8}T\\d{6}Z)(-([2-9]|[1-9]\\d{1,2}))?$"));
        const QString nombre = QFileInfo(m.preDatos).fileName();
        const auto c = patron.match(nombre);
        if (!c.hasMatch() || c.captured(1) != m.fechaPre
            || !QDateTime::fromString(m.fechaPre, QStringLiteral("yyyyMMdd'T'HHmmss'Z'")).isValid()) {
            return false;
        }
        const QString esperadoDatos = QDir(datos).filePath(nombre);
        const QString esperadoPaquetes = canonica(datos) == padreRaiz ? esperadoDatos : QDir(padreRaiz).filePath(nombre);
        return m.preDatos == esperadoDatos && m.prePaquetes == esperadoPaquetes;
    }
    // Calidad 1: lo restaurado sigue como se verifico, este en el staging o
    // ya promovido (a mitad de `promoviendo`).
    bool restauradoIntacto()
    {
        const QString base = existe(QDir(m.stagingDatos).filePath(kBase)) ? QDir(m.stagingDatos).filePath(kBase)
                                                                           : QDir(datos).filePath(kBase);
        const QString paquetes = existe(QDir(m.stagingExtraccion).filePath(kPaquetes))
            ? QDir(m.stagingExtraccion).filePath(kPaquetes)
            : raiz;
        const QByteArray h = AreaRestauracionLocal::huellaComponentes(base, paquetes,
                                                                      QDir(m.stagingDatos).filePath(kPreferencias));
        return !h.isEmpty() && h == m.huellaStaging;
    }
    bool stagingIntacto() const
    {
        const QByteArray h = AreaRestauracionLocal::calcularHuella(m.stagingDatos, m.stagingExtraccion);
        return !h.isEmpty() && h == m.huellaStaging;
    }
};

bool Aplicacion::elegirPre()
{
    const QDateTime ahora = (m_o.reloj ? m_o.reloj() : QDateTime::currentDateTimeUtc()).toUTC();
    m.fechaPre = ahora.toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
    const QString base = QLatin1String(mr::kPrefijoPre) + m.fechaPre;
    for (int n = 1; n < 1000; ++n) {
        const QString nombre = n == 1 ? base : base + QStringLiteral("-%1").arg(n);
        const QString pd = QDir(datos).filePath(nombre);
        // Misma carpeta (p. ej. --data-dir): una sola pre-restauracion.
        const QString pp = canonica(datos) == padreRaiz ? pd : QDir(padreRaiz).filePath(nombre);
        if (!existe(pd) && !existe(pp)) {
            m.preDatos = pd;
            m.prePaquetes = pp;
            return escribirFase(Fase::PreElegida, "pre.elegir");
        }
    }
    paso = "pre.sin_nombre";
    ultimoErrno = EEXIST;
    return false;
}

bool Aplicacion::crearPre(bool retomando)
{
    QStringList carpetas{m.preDatos};
    if (m.prePaquetes != m.preDatos) {
        carpetas.append(m.prePaquetes);
    }
    if (retomando) {
        // Ya se movio algo: cada carpeta debe ser nuestra (marca con este id,
        // escrita antes de `moviendo_actuales`).
        for (const QString& d : std::as_const(carpetas)) {
            if (!datos_previos_restauracion::esCarpetaDe(d, m.id)) {
                paso = "pre.ajena";
                preAjena = true;
                return false;
            }
        }
        return true;
    }
    for (const QString& d : std::as_const(carpetas)) {
        if (!existe(d)) {
            if (!continuar("pre.crear")) {
                return false;
            }
            const int e = m_ops.crearDirectorio(QFile::encodeName(d), 0700); // mkdir: exclusivo
            if (e != 0 && e != EEXIST) {
                ultimoErrno = e;
                return false;
            }
        }
        // Calidad (alta): solo se adopta una carpeta vacia o con nuestra marca
        // (la creo este proceso en un intento previo). Una pre-restauracion
        // existente con contenido nunca se marca ni se usa.
        if (!datos_previos_restauracion::vaciaOSoloMarcaDe(d, m.id)) {
            paso = "pre.ocupada";
            preAjena = true;
            return false;
        }
    }
    // D22: marca de propiedad; solo las carpetas con ella cuentan como datos
    // previos (bloquean la reconciliacion de e.firma) y se pueden descartar.
    for (const QString& d : std::as_const(carpetas)) {
        if (!continuar("pre.marca")) {
            return false;
        }
        if (!datos_previos_restauracion::escribirMarca(d, m.id, m.preDatos, m.prePaquetes)) {
            paso = "pre.marca";
            ultimoErrno = EIO;
            return false;
        }
        // R8: los datos previos no entran a Time Machine (mejor esfuerzo).
        if (!ar::excluirDeRespaldoDelSistema(d)) {
            qCWarning(lcRestauracion, "restauracion: no se pudo excluir pre-restauracion del respaldo del sistema");
        }
    }
    return true;
}

bool Aplicacion::moverActuales()
{
    for (const QString& nombre : archivosBase()) {
        if (mover(QDir(datos).filePath(nombre), QDir(m.preDatos).filePath(nombre), "actuales.base") == Mov::Fallo) {
            return false;
        }
    }
    if (mover(QDir(datos).filePath(kCredenciales), QDir(m.preDatos).filePath(kCredenciales), "actuales.credenciales")
        == Mov::Fallo) {
        return false;
    }
    // La raiz actual siempre existe para que la reversion no sea ambigua.
    const QString preRaiz = QDir(m.prePaquetes).filePath(kPaquetes);
    if (!existe(raiz) && !existe(preRaiz)) {
        if (!continuar("actuales.crear_raiz")) {
            return false;
        }
        if (const int e = m_ops.crearDirectorio(QFile::encodeName(raiz), 0700); e != 0 && e != EEXIST) {
            ultimoErrno = e;
            return false;
        }
    }
    if (mover(raiz, preRaiz, "actuales.paquetes") == Mov::Fallo) {
        return false;
    }
    // Copia de las preferencias actuales (referencia para revertir a mano).
    const QString prefs = QDir(datos).filePath(kPreferencias);
    const QString copia = QDir(m.preDatos).filePath(kPreferencias);
    if (existe(prefs) && !existe(copia)) {
        if (!continuar("actuales.preferencias")) {
            return false;
        }
        QFile f(prefs);
        if (!f.open(QIODevice::ReadOnly) || !ar::escribirAtomico(copia, f.readAll())) {
            ultimoErrno = EIO;
            return false;
        }
    }
    if (!sincronizar({datos, m.preDatos, m.prePaquetes, padreRaiz})) {
        paso = "actuales.fsync";
        return false;
    }
    return true;
}

bool Aplicacion::aplicarPreferencias()
{
    const QString restauradas = QDir(m.stagingDatos).filePath(kPreferencias);
    const QString actuales = QDir(datos).filePath(kPreferencias);
    std::optional<QSettings> origen;
    if (existe(restauradas)) {
        origen.emplace(restauradas, QSettings::IniFormat);
    }
    QSettings destino(actuales, QSettings::IniFormat);
    // Solo la lista blanca (D13); el resto, incluida la seccion `respaldo`
    // (idInstalacion), es del equipo y no cambia.
    for (const FuenteRespaldoLocal::ClavePreferencia& c : FuenteRespaldoLocal::clavesPreferencias()) {
        const QString clave = QString::fromLatin1(c.clave);
        const QVariant v = origen ? origen->value(clave) : QVariant();
        if (v.typeId() == QMetaType::QString && c.valida(v.toString())) {
            destino.setValue(clave, v.toString());
        } else {
            destino.remove(clave); // la del equipo apuntaba a la base anterior
        }
    }
    destino.sync();
    if (destino.status() != QSettings::NoError) {
        ultimoErrno = EIO;
        return false;
    }
    return true;
}

bool Aplicacion::promover()
{
    const auto base = mover(QDir(m.stagingDatos).filePath(kBase), QDir(datos).filePath(kBase), "promover.base");
    if (base == Mov::Fallo) {
        return false;
    }
    if (base == Mov::NoHabia) {
        paso = "promover.sin_base";
        ultimoErrno = ENOENT;
        return false;
    }
    const auto paquetes = mover(QDir(m.stagingExtraccion).filePath(kPaquetes), raiz, "promover.paquetes");
    if (paquetes == Mov::Fallo) {
        return false;
    }
    if (paquetes == Mov::NoHabia) {
        // Respaldo sin paquetes: raiz nueva vacia.
        if (!continuar("promover.crear_raiz")) {
            return false;
        }
        if (const int e = m_ops.crearDirectorio(QFile::encodeName(raiz), 0700); e != 0 && e != EEXIST) {
            ultimoErrno = e;
            return false;
        }
    }
    // D22: registro de aplicadas; solo con el id registrado las carpetas
    // pre-restauracion cuentan como datos previos.
    if (!continuar("promover.registro")) {
        return false;
    }
    if (!datos_previos_restauracion::registrarAplicada(datos, m.id)) {
        paso = "promover.registro";
        ultimoErrno = EIO;
        return false;
    }
    if (!continuar("promover.preferencias") || !aplicarPreferencias()) {
        return false;
    }
    if (!sincronizar({datos, padreRaiz, m.stagingDatos, m.stagingExtraccion})) {
        paso = "promover.fsync";
        return false;
    }
    return true;
}

// Solo lo que vive en la carpeta de datos (staging de datos y marcador, de
// rutas fijas validadas): nunca la extraccion junto a una raiz no confiable.
ResultadoRestauracionArranque Aplicacion::descartarSoloDatos(const char* motivo)
{
    if (!continuar("descartar")) {
        return cortada();
    }
    const bool ok = ar::borrarArbol(m.stagingDatos) && mr::borrar(datos);
    qCInfo(lcRestauracion, "restauracion descartada (%s); la extraccion junto a la raiz no se toca", motivo);
    return ok ? resultado(Estado::Descartada, QString::fromLatin1(motivo)) : resultado(Estado::Error, QStringLiteral("descartar"));
}

ResultadoRestauracionArranque Aplicacion::descartar(const char* motivo)
{
    if (!continuar("descartar")) {
        return cortada();
    }
    const bool ok = ar::borrarArbol(m.stagingDatos) && ar::borrarArbol(m.stagingExtraccion) && mr::borrar(datos);
    qCInfo(lcRestauracion, "restauracion descartada (%s)", motivo);
    if (!ok) {
        return resultado(Estado::Error, QStringLiteral("descartar"));
    }
    return resultado(Estado::Descartada, QString::fromLatin1(motivo));
}

ResultadoRestauracionArranque Aplicacion::revertir(const char* motivo)
{
    if (interrumpida) {
        return cortada();
    }
    qCWarning(lcRestauracion, "restauracion: revirtiendo (paso %s, errno %d, motivo %s)", paso, ultimoErrno, motivo);
    if (m.fase != Fase::Revirtiendo) {
        m.faseAntesDeRevertir = m.fase;
        if (!escribirFase(Fase::Revirtiendo, "revertir.inicio")) {
            return interrumpida ? cortada() : resultado(Estado::Error, QStringLiteral("revertir.marcador"));
        }
    }
    const bool promovidoPosible = orden(m.faseAntesDeRevertir) >= orden(Fase::ActualesMovidos);
    const QString preRaiz = m.prePaquetes.isEmpty() ? QString() : QDir(m.prePaquetes).filePath(kPaquetes);
    bool ok = true;
    if (promovidoPosible) {
        // Lo que esta en el lugar original es lo restaurado: vuelve al staging.
        if (!existe(m.stagingDatos) && m_ops.crearDirectorio(QFile::encodeName(m.stagingDatos), 0700) != 0) {
            ok = false;
        }
        if (!existe(m.stagingExtraccion) && m_ops.crearDirectorio(QFile::encodeName(m.stagingExtraccion), 0700) != 0) {
            ok = false;
        }
        // Solo mientras lo actual siga en pre-restauracion: lo restaurado
        // vuelve al staging ANTES de que lo actual regrese, asi que despues de
        // un corte lo que ocupa el lugar original ya es lo actual.
        if (ok && existe(QDir(m.preDatos).filePath(kBase))) {
            ok = mover(QDir(datos).filePath(kBase), QDir(m.stagingDatos).filePath(kBase), "revertir.base") != Mov::Fallo;
        }
        if (ok && existe(preRaiz)) {
            ok = mover(raiz, QDir(m.stagingExtraccion).filePath(kPaquetes), "revertir.paquetes") != Mov::Fallo;
        }
        const QString copia = QDir(m.preDatos).filePath(kPreferencias);
        if (ok && existe(copia)) {
            if (!continuar("revertir.preferencias")) {
                return cortada();
            }
            ok = ::rename(QFile::encodeName(copia).constData(),
                          QFile::encodeName(QDir(datos).filePath(kPreferencias)).constData())
                == 0;
        }
    }
    // Solo se devuelve (o se borra) lo que este en carpetas con nuestra marca.
    const bool preDatosNuestra = !m.preDatos.isEmpty() && datos_previos_restauracion::esCarpetaDe(m.preDatos, m.id);
    const bool prePaquetesNuestra =
        !m.prePaquetes.isEmpty() && datos_previos_restauracion::esCarpetaDe(m.prePaquetes, m.id);
    if (ok && !m.preDatos.isEmpty() && (!preDatosNuestra || !prePaquetesNuestra)
        && (existe(m.preDatos) || existe(m.prePaquetes))) {
        // Carpetas pre que existen sin nuestra marca: no son de esta
        // restauracion (o la marca no llego a escribirse en una carpeta que
        // creamos vacia). Si estan vacias se dejan; con contenido, Error.
        const bool vacias = (!existe(m.preDatos) || datos_previos_restauracion::vaciaOSoloMarcaDe(m.preDatos, m.id))
            && (!existe(m.prePaquetes) || datos_previos_restauracion::vaciaOSoloMarcaDe(m.prePaquetes, m.id));
        if (!vacias) {
            qCCritical(lcRestauracion, "restauracion: pre-restauracion ajena al revertir; no se toca");
            return resultado(Estado::Error, QStringLiteral("revertir.pre_ajena"));
        }
    }
    if (ok && preDatosNuestra && prePaquetesNuestra) {
        for (const QString& nombre : archivosBase()) {
            ok = ok && mover(QDir(m.preDatos).filePath(nombre), QDir(datos).filePath(nombre), "revertir.actual_base") != Mov::Fallo;
        }
        ok = ok
            && mover(QDir(m.preDatos).filePath(kCredenciales), QDir(datos).filePath(kCredenciales),
                     "revertir.actual_credenciales")
                != Mov::Fallo;
        ok = ok && mover(preRaiz, raiz, "revertir.actual_paquetes") != Mov::Fallo;
        if (ok) {
            // La copia de preferencias (sin promocion) y las carpetas vacias.
            QFile::remove(QDir(m.preDatos).filePath(kPreferencias));
            datos_previos_restauracion::retirarAplicada(datos, m.id);
            datos_previos_restauracion::borrarMarca(m.prePaquetes);
            datos_previos_restauracion::borrarMarca(m.preDatos);
            m_ops.eliminarDirectorioVacio(QFile::encodeName(m.preDatos));
            if (m.prePaquetes != m.preDatos) {
                m_ops.eliminarDirectorioVacio(QFile::encodeName(m.prePaquetes));
            }
        }
    }
    if (interrumpida) {
        return cortada();
    }
    if (!ok) {
        qCCritical(lcRestauracion, "restauracion: no se pudo revertir (paso %s, errno %d)", paso, ultimoErrno);
        return resultado(Estado::Error, QStringLiteral("revertir.") + QString::fromLatin1(paso));
    }
    if (!sincronizar({datos, padreRaiz})) {
        return resultado(Estado::Error, QStringLiteral("revertir.fsync"));
    }
    if (!continuar("revertir.limpiar")) {
        return cortada();
    }
    if (!ar::borrarArbol(m.stagingDatos) || !ar::borrarArbol(m.stagingExtraccion) || !mr::borrar(datos)) {
        return resultado(Estado::Error, QStringLiteral("revertir.limpiar"));
    }
    return resultado(Estado::Revertida, QString::fromLatin1(motivo));
}

ResultadoRestauracionArranque Aplicacion::ejecutar()
{
    // Seguridad 1: en TODAS las fases la raiz debe ser de esta sesion y las
    // rutas de pre-restauracion deben tener su forma estricta; si no, el
    // marcador no es confiable y no se toca nada fuera de la carpeta de datos.
    if (!raizPermitida()) {
        if (orden(m.fase) <= orden(Fase::Solicitado)) {
            return descartarSoloDatos("raiz_distinta");
        }
        qCCritical(lcRestauracion, "restauracion: marcador con una raiz ajena; no se toca nada");
        return resultado(Estado::Error, QStringLiteral("marcador.raiz"));
    }
    if (!preValidas()) {
        qCCritical(lcRestauracion, "restauracion: marcador con rutas pre-restauracion ajenas; no se toca nada");
        return resultado(Estado::Error, QStringLiteral("marcador.pre"));
    }
    // Desde `moviendo_actuales` las carpetas pre ya llevan nuestra marca (se
    // escribe antes de mover nada): si no, no son de esta restauracion.
    {
        const Fase efectiva = m.fase == Fase::Revirtiendo ? m.faseAntesDeRevertir : m.fase;
        // Al revertir, una carpeta ya devuelta (vacia o retirada) tambien vale.
        const auto aceptable = [&](const QString& d) {
            return datos_previos_restauracion::esCarpetaDe(d, m.id)
                || (m.fase == Fase::Revirtiendo
                    && (!existe(d) || datos_previos_restauracion::vaciaOSoloMarcaDe(d, m.id)));
        };
        if (orden(efectiva) >= orden(Fase::MoviendoActuales) && m.fase != Fase::Promovido
            && (!aceptable(m.preDatos) || !aceptable(m.prePaquetes))) {
            qCCritical(lcRestauracion, "restauracion: marcador con pre-restauracion sin marca propia; no se toca nada");
            return resultado(Estado::Error, QStringLiteral("marcador.pre_ajena"));
        }
    }

    // `verificado`: el staging se comprobo en ESTE arranque y no se ha tocado.
    bool verificado = false;
    switch (m.fase) {
    case Fase::Preparando:
    case Fase::Preparado:
        return descartar("no_confirmada");
    case Fase::Revirtiendo:
        return revertir("continuar_reversion");
    case Fase::Solicitado:
        if (!stagingIntacto()) {
            return descartar("staging_cambio");
        }
        verificado = true;
        if (!elegirPre()) {
            return interrumpida ? cortada() : resultado(Estado::Error, QStringLiteral("pre.elegir"));
        }
        [[fallthrough]];
    case Fase::PreElegida:
    case Fase::MoviendoActuales:
        if (!verificado && !stagingIntacto()) {
            return revertir("staging_cambio");
        }
        verificado = true;
        if (!crearPre(m.fase == Fase::MoviendoActuales)) {
            if (preAjena) {
                qCCritical(lcRestauracion, "restauracion: pre-restauracion ocupada o ajena; no se toca nada");
                return resultado(Estado::Error, QStringLiteral("marcador.pre_ocupada"));
            }
            return interrumpida ? cortada() : revertir("pre.crear");
        }
        if (m.fase != Fase::MoviendoActuales && !escribirFase(Fase::MoviendoActuales, "fase.moviendo")) {
            return interrumpida ? cortada() : revertir("marcador");
        }
        if (!moverActuales()) {
            return revertir("mover_actuales");
        }
        if (!escribirFase(Fase::ActualesMovidos, "fase.actuales_movidos")) {
            return interrumpida ? cortada() : revertir("marcador");
        }
        [[fallthrough]];
    case Fase::ActualesMovidos:
        // Calidad 1: al retomar aqui el staging pudo cambiar mientras la app
        // estaba cerrada.
        if (!verificado && !stagingIntacto()) {
            return revertir("staging_cambio");
        }
        verificado = true;
        if (!escribirFase(Fase::Promoviendo, "fase.promoviendo")) {
            return interrumpida ? cortada() : revertir("marcador");
        }
        [[fallthrough]];
    case Fase::Promoviendo:
        // A mitad de la promocion: se comprueba cada parte donde este.
        if (!verificado && !restauradoIntacto()) {
            return revertir("staging_cambio");
        }
        if (!promover()) {
            return revertir("promover");
        }
        if (!escribirFase(Fase::Promovido, "fase.promovido")) {
            return interrumpida ? cortada() : revertir("marcador");
        }
        [[fallthrough]];
    case Fase::Promovido:
        if (!continuar("limpiar")) {
            return cortada();
        }
        // Aplicada: un fallo al limpiar solo deja restos privados; el
        // marcador se borra al final para reintentar la limpieza.
        if (!ar::borrarArbol(m.stagingDatos) || !ar::borrarArbol(m.stagingExtraccion) || !mr::borrar(datos)) {
            qCWarning(lcRestauracion, "restauracion aplicada; limpieza incompleta del staging");
        }
        (void)sincronizar({datos, padreRaiz}); // ya aplicada
        qCInfo(lcRestauracion, "restauracion aplicada");
        return resultado(Estado::Aplicada, QString());
    }
    return resultado(Estado::Error, QStringLiteral("fase"));
}

} // namespace

QString claveEstable(ResultadoRestauracionArranque::Estado e)
{
    switch (e) {
    case Estado::SinMarcador: return QStringLiteral("sin_marcador");
    case Estado::Aplicada: return QStringLiteral("aplicada");
    case Estado::Descartada: return QStringLiteral("descartada");
    case Estado::Revertida: return QStringLiteral("revertida");
    case Estado::Error: return QStringLiteral("error");
    case Estado::Interrumpida: return QStringLiteral("interrumpida");
    }
    return QStringLiteral("error");
}

QString ResultadoRestauracionArranque::mensajeError() const
{
    return QStringLiteral("No se pudo completar ni deshacer la restauracion de un respaldo. La aplicacion no se "
                          "abrira para no trabajar con datos mezclados; vuelve a abrirla para reintentar.\n\n"
                          "Detalle: %1")
        .arg(diagnostico);
}

RestauracionArranque::RestauracionArranque(Opciones opciones) : m_opciones(std::move(opciones))
{
    m_opciones.directorioDatos = QDir::cleanPath(QDir(m_opciones.directorioDatos).absolutePath());
}

ResultadoRestauracionArranque RestauracionArranque::aplicar()
{
    MarcadorRestauracion m;
    switch (mr::leer(m_opciones.directorioDatos, m)) {
    case mr::Lectura::Ausente:
        return {};
    case mr::Lectura::Invalido: {
        ResultadoRestauracionArranque r;
        r.estado = Estado::Error;
        r.diagnostico = QStringLiteral("marcador.invalido");
        return r;
    }
    case mr::Lectura::Leido:
        break;
    }
    if (m.directorioDatos != m_opciones.directorioDatos) {
        ResultadoRestauracionArranque r;
        r.estado = Estado::Error;
        r.diagnostico = QStringLiteral("marcador.otro_directorio");
        return r;
    }
    std::unique_ptr<PosixFileOps> propio;
    FileOps* ops = m_opciones.ops;
    if (!ops) {
        propio = std::make_unique<PosixFileOps>();
        ops = propio.get();
    }
    Aplicacion a(m_opciones, *ops, std::move(m));
    return a.ejecutar();
}

AplicadorRestauracion aplicadorRestauracionPosix(const QString& directorioDatos, bool conDataDir, const QString& home)
{
    return [directorioDatos, conDataDir, home] {
        RestauracionArranque::Opciones o;
        o.directorioDatos =
            directorioDatos.isEmpty() ? AppBootstrapper::directorioDatosPorDefecto() : directorioDatos;
        if (o.directorioDatos.isEmpty()) {
            return ResultadoRestauracionArranque{};
        }
        o.directorioDatos = QDir::cleanPath(QDir(o.directorioDatos).absolutePath());
        if (conDataDir) {
            o.raicesPermitidas = {FilesystemPackageStorage::resolverRaiz(o.directorioDatos, home)};
        } else {
            const QString paquetes = QLatin1String(FilesystemPackageStorage::kCarpetaPaquetes);
            o.raicesPermitidas = {
                FilesystemPackageStorage::resolverRaiz(std::nullopt, home),
                QDir(QDir(home).filePath(QLatin1String(FilesystemPackageStorage::kCarpetaHomeLegada))).filePath(paquetes),
            };
        }
        return RestauracionArranque(std::move(o)).aplicar();
    };
}

} // namespace satcfdi
