#pragma once

// SecretStore falso, determinista y en memoria (T005). Header-only, sin
// Keychain, archivos ni OpenSSL. Reutilizable por pruebas de aplicacion,
// presentacion (T005.1) e integracion.
//
// Comportamiento de prepararEFirma (en este orden):
// 1. `fallos[Operacion::Preparar]` inyectado -> esa categoria.
// 2. Ruta de .cer o .key vacia -> ArchivoIlegible (origen Certificado|Llave).
// 3. Contrasena distinta de `contrasenaValida` -> ContrasenaIncorrecta.
// 4. `rfcCertificado` distinto de rfcEsperado -> RfcNoCoincide.
// 5. ahora < vigenteDesde -> NoVigenteAun; ahora >= vigenteHasta -> Vencida.
// 6. Exito: crea una generacion VIVA (no confirmada) y devuelve una
//    CredencialPreparada cuyo descartador la elimina (cuenta `descartes`).
// Las demas operaciones aceptan fallos inyectados por Operacion; ademas:
// - obtenerEstado: generacion ausente -> MaterialFaltante; `danada` ->
//   MaterialDanado; si no, por vigencia (Lista/Vencida/NoVigenteAun).
// - obtenerMaterialFirma: ausente -> CredencialNoEncontrada; danada ->
//   CredencialDanada; si no, MaterialFirma con bytes ficticios.
// - eliminar: idempotente (ausente -> exito).
// - reconciliar: elimina las generaciones no incluidas en `vigentes`; las
//   huerfanas en `noBorrables` cuentan como fallosLimpieza.
//
// Fidelidad al contrato: `fallos` solo admite las categorias que
// ports/SecretStore.h permite a cada operacion (categoriasPermitidas()). Una
// categoria fuera de contrato es un error de la prueba: qFatal.
// `alEntrar(op)` (opcional) se invoca al entrar a cada operacion, FUERA del
// mutex del fake, para barreras/ordenes en pruebas de concurrencia.
//
// Hilo: thread-safe (QMutex). Se usa desde el hilo del dispatcher; la prueba
// lee contadores despues de que el future termino. El fake debe sobrevivir a
// toda CredencialPreparada que haya emitido.

#include "ports/SecretStore.h"

#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QTimeZone>
#include <QStringList>

#include <cstring>
#include <functional>

namespace fakes {

using namespace satcfdi;

class FakeSecretStore final : public SecretStore {
public:
    enum class Operacion { Preparar, Estado, Material, Eliminar, Reconciliar };

    struct Generacion {
        MetadataCredencial metadata;
        bool danada = false;
    };

    // Configuracion (asignar antes de usar).
    QByteArray contrasenaValida = QByteArrayLiteral("clave-de-prueba");
    QString rfcCertificado = QStringLiteral("AAA010101AAA");
    QDateTime vigenteDesde = QDateTime(QDate(2025, 1, 1), QTime(0, 0), QTimeZone::UTC);
    QDateTime vigenteHasta = QDateTime(QDate(2029, 1, 1), QTime(0, 0), QTimeZone::UTC);
    QHash<Operacion, ErrorSecretStore::Categoria> fallos;
    QSet<QString> noBorrables; // uuids que reconciliar no logra borrar
    // T005.1 (DA4): origen que acompana al fallo inyectado en Preparar (el
    // adaptador real lo llena; el servicio lo normaliza). Las validaciones
    // propias del fake ya traen origen: ruta .cer vacia -> Certificado, ruta
    // .key vacia -> Llave, contrasena distinta -> Contrasena.
    OrigenErrorEFirma origenFalloPreparar = OrigenErrorEFirma::Ninguno;
    std::function<void(Operacion)> alEntrar;

    // Categorias que el contrato permite a cada operacion.
    static QList<ErrorSecretStore::Categoria> categoriasPermitidas(Operacion op)
    {
        using C = ErrorSecretStore::Categoria;
        const QList<C> almacen{C::AlmacenBloqueado, C::AccesoDenegado, C::AlmacenMalConfigurado,
                               C::AlmacenNoDisponible, C::Interno};
        switch (op) {
        case Operacion::Preparar: {
            QList<C> r;
            for (C c : kCategoriasErrorSecretStore) {
                if (c != C::CredencialNoEncontrada && c != C::CredencialDanada) {
                    r.append(c);
                }
            }
            return r;
        }
        case Operacion::Estado:
        case Operacion::Reconciliar:
            return almacen;
        case Operacion::Material:
            return almacen + QList<C>{C::CredencialNoEncontrada, C::CredencialDanada};
        case Operacion::Eliminar:
            return almacen + QList<C>{C::FalloEscritura};
        }
        return {};
    }

    // Inspeccion.
    int generacionesVivas() const
    {
        QMutexLocker l(&m_mutex);
        return static_cast<int>(m_generaciones.size());
    }
    bool existe(const CredencialRef& ref) const
    {
        QMutexLocker l(&m_mutex);
        return m_generaciones.contains(ref.uuid());
    }
    QStringList uuidsVivos() const
    {
        QMutexLocker l(&m_mutex);
        QStringList r = m_generaciones.keys();
        r.sort();
        return r;
    }
    int preparadas() const { QMutexLocker l(&m_mutex); return m_preparadas; }
    int descartes() const { QMutexLocker l(&m_mutex); return m_descartes; }
    int reconciliaciones() const { QMutexLocker l(&m_mutex); return m_reconciliaciones; }
    QList<CredencialRef> eliminadas() const { QMutexLocker l(&m_mutex); return m_eliminadas; }
    // Generaciones cuyo material se entrego, en orden.
    QList<CredencialRef> materialesEntregados() const { QMutexLocker l(&m_mutex); return m_entregados; }

    // Siembra una generacion (p. ej. un residuo previo) sin pasar por preparar.
    CredencialRef sembrar(bool danada = false)
    {
        QMutexLocker l(&m_mutex);
        const CredencialRef ref = CredencialRef::generar();
        m_generaciones.insert(ref.uuid(), Generacion{metadataLocked(), danada});
        return ref;
    }
    void quitar(const CredencialRef& ref)
    {
        QMutexLocker l(&m_mutex);
        m_generaciones.remove(ref.uuid());
    }
    void marcarDanada(const CredencialRef& ref)
    {
        QMutexLocker l(&m_mutex);
        if (auto it = m_generaciones.find(ref.uuid()); it != m_generaciones.end()) {
            it->danada = true;
        }
    }

    // --- SecretStore ---------------------------------------------------------

    Resultado<CredencialPreparada, ErrorSecretStore>
    prepararEFirma(EntradaEFirma&& entradaMovida, QStringView rfcEsperado, const QDateTime& ahoraUtc) override
    {
        using R = Resultado<CredencialPreparada, ErrorSecretStore>;
        EntradaEFirma entrada = std::move(entradaMovida); // se limpia al salir
        entrar(Operacion::Preparar);
        QMutexLocker l(&m_mutex);
        if (auto f = fallo(Operacion::Preparar)) {
            return R::fallo(f->conOrigen(origenFalloPreparar));
        }
        if (entrada.rutaCertificado.isEmpty()) {
            return R::fallo(ErrorSecretStore::de(ErrorSecretStore::Categoria::ArchivoIlegible)
                                .conOrigen(OrigenErrorEFirma::Certificado));
        }
        if (entrada.rutaLlavePrivada.isEmpty()) {
            return R::fallo(ErrorSecretStore::de(ErrorSecretStore::Categoria::ArchivoIlegible)
                                .conOrigen(OrigenErrorEFirma::Llave));
        }
        if (!entrada.contrasena.igualA(contrasenaValida.constData(),
                                       static_cast<std::size_t>(contrasenaValida.size()))) {
            return R::fallo(ErrorSecretStore::de(ErrorSecretStore::Categoria::ContrasenaIncorrecta)
                                .conOrigen(OrigenErrorEFirma::Contrasena));
        }
        if (rfcEsperado != rfcCertificado) {
            return R::fallo(ErrorSecretStore::de(ErrorSecretStore::Categoria::RfcNoCoincide));
        }
        if (auto v = vigencia(ahoraUtc); v != EstadoCredencial::Lista) {
            return R::fallo(ErrorSecretStore::de(v == EstadoCredencial::Vencida
                                                     ? ErrorSecretStore::Categoria::Vencida
                                                     : ErrorSecretStore::Categoria::NoVigenteAun));
        }
        const CredencialRef ref = CredencialRef::generar();
        const MetadataCredencial meta = metadataLocked();
        m_generaciones.insert(ref.uuid(), Generacion{meta, false});
        ++m_preparadas;
        return R::exito(CredencialPreparada(ref, meta, [this](const CredencialRef& r) {
            QMutexLocker l2(&m_mutex);
            m_generaciones.remove(r.uuid());
            ++m_descartes;
        }));
    }

    Resultado<EstadoCredencial, ErrorSecretStore> obtenerEstado(const CredencialRef& ref,
                                                                const QDateTime& ahoraUtc) override
    {
        using R = Resultado<EstadoCredencial, ErrorSecretStore>;
        entrar(Operacion::Estado);
        QMutexLocker l(&m_mutex);
        if (auto f = fallo(Operacion::Estado)) {
            return R::fallo(*f);
        }
        const auto it = m_generaciones.constFind(ref.uuid());
        if (it == m_generaciones.cend()) {
            return R::exito(EstadoCredencial::MaterialFaltante);
        }
        if (it->danada) {
            return R::exito(EstadoCredencial::MaterialDanado);
        }
        return R::exito(vigencia(ahoraUtc, it->metadata));
    }

    Resultado<MaterialFirma, ErrorSecretStore> obtenerMaterialFirma(const CredencialRef& ref) override
    {
        using R = Resultado<MaterialFirma, ErrorSecretStore>;
        entrar(Operacion::Material);
        QMutexLocker l(&m_mutex);
        if (auto f = fallo(Operacion::Material)) {
            return R::fallo(*f);
        }
        const auto it = m_generaciones.constFind(ref.uuid());
        if (it == m_generaciones.cend()) {
            return R::fallo(ErrorSecretStore::de(ErrorSecretStore::Categoria::CredencialNoEncontrada));
        }
        if (it->danada) {
            return R::fallo(ErrorSecretStore::de(ErrorSecretStore::Categoria::CredencialDanada));
        }
        m_entregados.append(ref);
        static constexpr char kCert[] = "fake-cert-der";
        static constexpr char kLlave[] = "fake-key-der";
        return R::exito(MaterialFirma(
            BufferSecreto::desdeBytes(kCert, sizeof(kCert) - 1),
            BufferSecreto::desdeBytes(kLlave, sizeof(kLlave) - 1),
            BufferSecreto::desdeBytes(contrasenaValida.constData(),
                                      static_cast<std::size_t>(contrasenaValida.size()))));
    }

    Resultado<Exito, ErrorSecretStore> eliminar(const CredencialRef& ref) override
    {
        using R = Resultado<Exito, ErrorSecretStore>;
        entrar(Operacion::Eliminar);
        QMutexLocker l(&m_mutex);
        if (auto f = fallo(Operacion::Eliminar)) {
            return R::fallo(*f);
        }
        m_generaciones.remove(ref.uuid());
        m_eliminadas.append(ref);
        return R::exito({});
    }

    Resultado<ResumenReconciliacion, ErrorSecretStore> reconciliar(const QList<CredencialRef>& vigentes) override
    {
        using R = Resultado<ResumenReconciliacion, ErrorSecretStore>;
        entrar(Operacion::Reconciliar);
        QMutexLocker l(&m_mutex);
        ++m_reconciliaciones;
        if (auto f = fallo(Operacion::Reconciliar)) {
            return R::fallo(*f);
        }
        QSet<QString> vivos;
        for (const CredencialRef& r : vigentes) {
            vivos.insert(r.uuid());
        }
        ResumenReconciliacion resumen;
        for (auto it = m_generaciones.begin(); it != m_generaciones.end();) {
            if (vivos.contains(it.key())) {
                ++resumen.generacionesConservadas;
                ++it;
            } else if (noBorrables.contains(it.key())) {
                ++resumen.fallosLimpieza;
                ++it;
            } else {
                ++resumen.generacionesEliminadas;
                it = m_generaciones.erase(it);
            }
        }
        return R::exito(resumen);
    }

private:
    void entrar(Operacion op) const
    {
        if (alEntrar) {
            alEntrar(op);
        }
    }

    std::optional<ErrorSecretStore> fallo(Operacion op) const
    {
        if (fallos.contains(op)) {
            if (!categoriasPermitidas(op).contains(fallos.value(op))) {
                qFatal("FakeSecretStore: categoria %s fuera de contrato para la operacion %d",
                       qPrintable(claveEstable(fallos.value(op))), static_cast<int>(op));
            }
            return ErrorSecretStore::de(fallos.value(op), QStringLiteral("fallo inyectado"));
        }
        return std::nullopt;
    }

    MetadataCredencial metadataLocked() const
    {
        return MetadataCredencial{QStringLiteral("30001000000500003416"), vigenteDesde, vigenteHasta};
    }

    EstadoCredencial vigencia(const QDateTime& ahora) const
    {
        return vigencia(ahora, metadataLocked());
    }

    static EstadoCredencial vigencia(const QDateTime& ahora, const MetadataCredencial& m)
    {
        if (ahora < m.vigenteDesde) {
            return EstadoCredencial::NoVigenteAun;
        }
        if (ahora >= m.vigenteHasta) {
            return EstadoCredencial::Vencida;
        }
        return EstadoCredencial::Lista;
    }

    mutable QMutex m_mutex;
    QHash<QString, Generacion> m_generaciones;
    QList<CredencialRef> m_eliminadas;
    QList<CredencialRef> m_entregados;
    int m_preparadas = 0;
    int m_descartes = 0;
    int m_reconciliaciones = 0;
};

} // namespace fakes
