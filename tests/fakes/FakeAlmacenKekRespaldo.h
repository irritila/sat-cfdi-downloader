#pragma once

// T025.4: AlmacenKekRespaldo en memoria. Guarda copias de la KEK (tipo,
// parametros con sal y bytes) por referencia, permite inyectar un error por
// operacion y observar cuantas hay. Thread-safe (el servicio lo usa desde su
// hilo; la prueba lo inspecciona desde el grafico). Sin Keychain real.
//
// Ambitos (como el adaptador real): cada fake opera solo en su ambito (id de
// instalacion). Varios fakes pueden compartir un `Llavero` para modelar dos
// instalaciones del mismo usuario; no se ven ni se borran entre si. Ambito
// invalido -> AlmacenMalConfigurado "ambito.invalido" en toda llamada.

#include "ports/respaldo/AlmacenKekRespaldo.h"

#include <QMutex>
#include <QMutexLocker>

#include <map>
#include <memory>
#include <optional>
#include <utility>

namespace fakes {

class FakeAlmacenKekRespaldo final : public satcfdi::respaldo::AlmacenKekRespaldo {
public:
    using ErrorAlmacenKek = satcfdi::respaldo::ErrorAlmacenKek;
    using KekRespaldo = satcfdi::respaldo::KekRespaldo;
    using ReferenciaKek = satcfdi::respaldo::ReferenciaKek;
    enum class Operacion { Guardar, Cargar, Borrar, Listar };

    // Almacen compartible entre fakes: (ambito, referencia) -> KEK.
    struct Llavero {
        mutable QMutex mutex;
        std::map<std::pair<QString, QString>, KekRespaldo> items;
        int cantidadTotal() const
        {
            QMutexLocker l(&mutex);
            return int(items.size());
        }
    };

    static QString ambitoPredeterminado() { return QStringLiteral("0b6f2c1e-7d3a-4f5b-9c8e-1a2b3c4d5e6f"); }

    explicit FakeAlmacenKekRespaldo(QString ambito = ambitoPredeterminado(),
                                    std::shared_ptr<Llavero> llavero = std::make_shared<Llavero>())
        : m_ambito(std::move(ambito))
        , m_llavero(std::move(llavero))
    {
    }

    const QString& ambito() const noexcept { return m_ambito; }
    const std::shared_ptr<Llavero>& llavero() const noexcept { return m_llavero; }

    // Error a devolver en la proxima llamada (y siguientes) a `op`.
    void fallarEn(Operacion op, ErrorAlmacenKek::Categoria categoria)
    {
        QMutexLocker l(&m_mutex);
        m_fallos[op] = ErrorAlmacenKek::de(categoria, QStringLiteral("fake"));
    }
    void quitarFallos()
    {
        QMutexLocker l(&m_mutex);
        m_fallos.clear();
    }

    satcfdi::Resultado<ReferenciaKek, ErrorAlmacenKek> guardar(const KekRespaldo& kek) override
    {
        using R = satcfdi::Resultado<ReferenciaKek, ErrorAlmacenKek>;
        QMutexLocker l(&m_mutex);
        ++llamadasGuardar;
        if (auto e = fallo(Operacion::Guardar)) {
            return R::fallo(*e);
        }
        if (!satcfdi::respaldo::esAmbitoKekValido(m_ambito)) {
            return R::fallo(satcfdi::respaldo::errorAmbitoInvalido());
        }
        if (!kek.valida() || (kek.tipo() == KekRespaldo::Tipo::Contrasena && !kek.parametros().validos())) {
            return R::fallo(ErrorAlmacenKek::de(ErrorAlmacenKek::Categoria::KekInvalida, QStringLiteral("kek.invalida")));
        }
        const ReferenciaKek r = ReferenciaKek::generar();
        QMutexLocker ll(&m_llavero->mutex);
        m_llavero->items.emplace(std::make_pair(m_ambito, r.texto()), copia(kek));
        return R::exito(r);
    }

    satcfdi::Resultado<KekRespaldo, ErrorAlmacenKek> cargar(const ReferenciaKek& referencia) override
    {
        using R = satcfdi::Resultado<KekRespaldo, ErrorAlmacenKek>;
        QMutexLocker l(&m_mutex);
        ++llamadasCargar;
        if (auto e = fallo(Operacion::Cargar)) {
            return R::fallo(*e);
        }
        if (!satcfdi::respaldo::esAmbitoKekValido(m_ambito)) {
            return R::fallo(satcfdi::respaldo::errorAmbitoInvalido());
        }
        if (referencia.nula()) {
            return R::fallo(ErrorAlmacenKek::de(ErrorAlmacenKek::Categoria::ReferenciaInvalida));
        }
        QMutexLocker ll(&m_llavero->mutex);
        const auto it = m_llavero->items.find(std::make_pair(m_ambito, referencia.texto()));
        if (it == m_llavero->items.end()) {
            return R::fallo(ErrorAlmacenKek::de(ErrorAlmacenKek::Categoria::NoEncontrada));
        }
        return R::exito(copia(it->second));
    }

    satcfdi::Resultado<satcfdi::Exito, ErrorAlmacenKek> borrar(const ReferenciaKek& referencia) override
    {
        using R = satcfdi::Resultado<satcfdi::Exito, ErrorAlmacenKek>;
        QMutexLocker l(&m_mutex);
        ++llamadasBorrar;
        if (auto e = fallo(Operacion::Borrar)) {
            return R::fallo(*e);
        }
        if (!satcfdi::respaldo::esAmbitoKekValido(m_ambito)) {
            return R::fallo(satcfdi::respaldo::errorAmbitoInvalido());
        }
        if (referencia.nula()) {
            return R::fallo(ErrorAlmacenKek::de(ErrorAlmacenKek::Categoria::ReferenciaInvalida));
        }
        QMutexLocker ll(&m_llavero->mutex);
        m_llavero->items.erase(std::make_pair(m_ambito, referencia.texto()));
        return R::exito(satcfdi::Exito{});
    }

    satcfdi::Resultado<QList<ReferenciaKek>, ErrorAlmacenKek> listar() override
    {
        using R = satcfdi::Resultado<QList<ReferenciaKek>, ErrorAlmacenKek>;
        QMutexLocker l(&m_mutex);
        if (auto e = fallo(Operacion::Listar)) {
            return R::fallo(*e);
        }
        if (!satcfdi::respaldo::esAmbitoKekValido(m_ambito)) {
            return R::fallo(satcfdi::respaldo::errorAmbitoInvalido());
        }
        QList<ReferenciaKek> r;
        QMutexLocker ll(&m_llavero->mutex);
        for (const auto& [clave, kek] : m_llavero->items) {
            if (clave.first == m_ambito) {
                r.append(*ReferenciaKek::desdeTexto(clave.second));
            }
        }
        return R::exito(r);
    }

    // --- Observacion (solo el ambito propio; el total, en llavero()). ---
    int cantidad() const
    {
        QMutexLocker ll(&m_llavero->mutex);
        int n = 0;
        for (const auto& [clave, kek] : m_llavero->items) {
            n += clave.first == m_ambito ? 1 : 0;
        }
        return n;
    }
    bool contiene(const ReferenciaKek& r) const
    {
        QMutexLocker ll(&m_llavero->mutex);
        return m_llavero->items.count(std::make_pair(m_ambito, r.texto())) != 0;
    }
    int llamadasGuardar = 0;
    int llamadasCargar = 0;
    int llamadasBorrar = 0;

private:
    static KekRespaldo copia(const KekRespaldo& k)
    {
        return KekRespaldo::desdeBytes(k.tipo(), k.parametros(),
                                       satcfdi::BufferSecreto::desdeBytes(k.clave().datos(), k.clave().tamano()));
    }
    std::optional<ErrorAlmacenKek> fallo(Operacion op) const
    {
        const auto it = m_fallos.find(op);
        return it == m_fallos.end() ? std::nullopt : std::optional<ErrorAlmacenKek>(it->second);
    }

    mutable QMutex m_mutex;
    QString m_ambito;
    std::shared_ptr<Llavero> m_llavero;
    std::map<Operacion, ErrorAlmacenKek> m_fallos;
};

} // namespace fakes
