#pragma once

#include "infrastructure/storage/FileOps.h"

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QStringList>

#include <cerrno>
#include <functional>

namespace satcfdi::fakes {

// T019: filesystem en memoria para la politica de MigracionRaizPaquetes.
// Rutas absolutas como claves. Un enlace guarda su objetivo y, por
// simplicidad, sus hijos "a traves del enlace" se guardan bajo la ruta del
// propio enlace (lstat del enlace devuelve Enlace; lstat de enlace/x
// devuelve el hijo). Fallos programables por operacion y sufijo de ruta.
// Las operaciones de archivo (abrir, escribir, eliminar, promover, ...)
// estan PROHIBIDAS: se cuentan en `prohibidas` y devuelven EPERM.
class FileOpsSimulado final : public FileOps {
public:
    enum class Op { Tipo, Listar, CrearDirectorio, EliminarDirectorioVacio, Renombrar };

    struct Nodo {
        TipoEntrada tipo = TipoEntrada::Directorio;
        unsigned modo = 0755;
        QByteArray objetivo; // solo enlaces
        bool operator==(const Nodo&) const = default;
    };

    QMap<QByteArray, Nodo> nodos;
    // Operaciones que cambian el filesystem, en orden ("mkdir <ruta> <modo>",
    // "rmdir <ruta>", "rename <origen> <destino>").
    QStringList cambios;
    int prohibidas = 0;
    // Llamadas a listar por ruta y gancho previo a cada listado (n = numero
    // de llamada para esa ruta, desde 1): permite cambiar el arbol entre dos
    // lecturas (TOCTOU).
    QMap<QByteArray, int> listados;
    std::function<void(const QByteArray& ruta, int n)> alListar;

    void dir(const QByteArray& r, unsigned modo = 0755) { nodos[r] = Nodo{TipoEntrada::Directorio, modo, {}}; }
    void archivo(const QByteArray& r, unsigned modo = 0644) { nodos[r] = Nodo{TipoEntrada::Archivo, modo, {}}; }
    void enlace(const QByteArray& r, const QByteArray& objetivo) { nodos[r] = Nodo{TipoEntrada::Enlace, 0755, objetivo}; }

    // Falla `op` con `errnoSimulado` cuando la ruta (origen, en rename)
    // termina en `sufijo` (vacio = cualquiera).
    void fallar(Op op, int errnoSimulado, const QByteArray& sufijo = {})
    {
        m_fallos.append({op, errnoSimulado, sufijo});
    }
    // crearDirectorio de una ruta con `sufijo` CREA el directorio y devuelve
    // `errnoSimulado` (como mkdir bien y chmod mal).
    void crearYFallar(const QByteArray& sufijo, int errnoSimulado)
    {
        m_crearYFallar = {sufijo, errnoSimulado};
    }

    int tipo(const QByteArray& ruta, TipoEntrada& t) override
    {
        if (const int e = inyectar(Op::Tipo, ruta)) {
            return e;
        }
        const auto it = nodos.constFind(ruta);
        if (it == nodos.cend()) {
            return ENOENT;
        }
        t = it->tipo;
        return 0;
    }
    int tipoResuelto(const QByteArray& ruta, TipoEntrada& t) override { return tipo(ruta, t); }
    int crearDirectorio(const QByteArray& ruta, unsigned modo) override
    {
        if (const int e = inyectar(Op::CrearDirectorio, ruta)) {
            return e;
        }
        if (nodos.contains(ruta)) {
            return EEXIST;
        }
        if (!nodos.contains(padre(ruta))) {
            return ENOENT;
        }
        dir(ruta, modo);
        cambios.append(QStringLiteral("mkdir %1 %2").arg(QString::fromUtf8(ruta)).arg(modo, 0, 8));
        if (m_crearYFallar.second != 0 && ruta.endsWith(m_crearYFallar.first)) {
            return m_crearYFallar.second;
        }
        return 0;
    }
    int eliminarDirectorioVacio(const QByteArray& ruta) override
    {
        if (const int e = inyectar(Op::EliminarDirectorioVacio, ruta)) {
            return e;
        }
        const auto it = nodos.constFind(ruta);
        if (it == nodos.cend()) {
            return ENOENT;
        }
        if (it->tipo != TipoEntrada::Directorio) {
            return ENOTDIR;
        }
        if (!hijos(ruta).isEmpty()) {
            return ENOTEMPTY;
        }
        nodos.remove(ruta);
        cambios.append(QStringLiteral("rmdir %1").arg(QString::fromUtf8(ruta)));
        return 0;
    }
    int renombrarSinReemplazo(const QByteArray& origen, const QByteArray& destino) override
    {
        if (const int e = inyectar(Op::Renombrar, origen)) {
            return e;
        }
        if (!nodos.contains(origen)) {
            return ENOENT;
        }
        if (nodos.contains(destino)) {
            return EEXIST;
        }
        if (!nodos.contains(padre(destino))) {
            return ENOENT;
        }
        QMap<QByteArray, Nodo> nuevos;
        for (auto it = nodos.cbegin(); it != nodos.cend(); ++it) {
            if (it.key() == origen) {
                nuevos.insert(destino, it.value());
            } else if (it.key().startsWith(origen + '/')) {
                nuevos.insert(destino + it.key().mid(origen.size()), it.value());
            } else {
                nuevos.insert(it.key(), it.value());
            }
        }
        nodos = nuevos;
        cambios.append(QStringLiteral("rename %1 %2").arg(QString::fromUtf8(origen), QString::fromUtf8(destino)));
        return 0;
    }
    int listar(const QByteArray& directorio, QList<EntradaDirectorio>& entradas) override
    {
        const int n = ++listados[directorio];
        if (alListar) {
            alListar(directorio, n);
        }
        if (const int e = inyectar(Op::Listar, directorio)) {
            return e;
        }
        const auto it = nodos.constFind(directorio);
        if (it == nodos.cend()) {
            return ENOENT;
        }
        if (it->tipo == TipoEntrada::Archivo) {
            return ENOTDIR;
        }
        entradas.clear();
        for (const QByteArray& h : hijos(directorio)) {
            entradas.append(EntradaDirectorio{h.mid(directorio.size() + 1), nodos.value(h).tipo});
        }
        return 0;
    }

    // Prohibidas en la migracion.
    int abrirExclusivo(const QByteArray&, unsigned, int&) override { return prohibir(); }
    int escribir(int, const char*, std::size_t) override { return prohibir(); }
    int sincronizarArchivo(int) override { return prohibir(); }
    int cerrar(int) override { return prohibir(); }
    int promoverSinReemplazo(const QByteArray&, const QByteArray&) override { return prohibir(); }
    int sincronizarDirectorio(const QByteArray&) override { return prohibir(); }
    int eliminarArchivo(const QByteArray&) override { return prohibir(); }
    void aleatorio(unsigned char*, std::size_t) override { prohibir(); }

private:
    struct Fallo {
        Op op;
        int error;
        QByteArray sufijo;
    };

    static QByteArray padre(const QByteArray& r) { return r.left(r.lastIndexOf('/')); }
    QList<QByteArray> hijos(const QByteArray& dir) const
    {
        QList<QByteArray> r;
        for (auto it = nodos.cbegin(); it != nodos.cend(); ++it) {
            if (it.key().startsWith(dir + '/') && !it.key().mid(dir.size() + 1).contains('/')) {
                r.append(it.key());
            }
        }
        return r;
    }
    int inyectar(Op op, const QByteArray& ruta) const
    {
        for (const Fallo& f : m_fallos) {
            if (f.op == op && (f.sufijo.isEmpty() || ruta.endsWith(f.sufijo))) {
                return f.error;
            }
        }
        return 0;
    }
    int prohibir()
    {
        ++prohibidas;
        return EPERM;
    }

    QList<Fallo> m_fallos;
    std::pair<QByteArray, int> m_crearYFallar{{}, 0};
};

} // namespace satcfdi::fakes
