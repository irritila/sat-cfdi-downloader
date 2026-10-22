#pragma once

#include "ports/respaldo/Restauracion.h"

#include <QMutex>
#include <QString>

#include <functional>

namespace satcfdi {

// T025.5: AreaRestauracion sobre el filesystem local (D5, D13, ADR 0022).
//
// Stagings (ambos 0700, del euid, vacios al crearse):
// - extraccion: `<padre de la raiz de paquetes>/.restauracion-<id>`, en el
//   MISMO volumen que la raiz (canonica, sin enlaces), para promover
//   `paquetes/` con un rename. Una raiz que es punto de montaje (otro
//   volumen que su padre) se rechaza (Destino). Sufijo `.noindex` (Spotlight).
// Ambos se excluyen de Time Machine. Antes de crear nada se exige espacio
// libre para el respaldo (mas margen) en el volumen de la raiz, y para la base
// en el de los datos si son volumenes distintos.
// - datos: `<datos>/restauracion/staging-<id>`, en el volumen de la base.
// Marcador: MarcadorRestauracion en `<datos>/restauracion/marcador.json`.
//
// Hilo: cualquiera menos el grafico; las llamadas se serializan con un mutex.
// Vida: lo comparten el servicio y sus hilos de trabajo (shared_ptr).
class AreaRestauracionLocal final : public respaldo::AreaRestauracion {
public:
    struct Opciones {
        QString directorioDatos; // absoluto
        QString raizPaquetes;    // raiz efectiva de la sesion (T019), absoluta
        // Bytes libres del volumen de una ruta; vacio = statfs (pruebas: inyectable).
        std::function<qint64(const QString&)> espacioLibre;
    };

    explicit AreaRestauracionLocal(Opciones opciones);

    Resultado<respaldo::StagingRestauracion, respaldo::ErrorRestauracion> crearStaging(quint64 tamanoRespaldo) override;
    Resultado<Exito, respaldo::ErrorRestauracion> colocarBase(const respaldo::StagingRestauracion& s) override;
    Resultado<Exito, respaldo::ErrorRestauracion> fijarMtime(const respaldo::StagingRestauracion& s,
                                                             const QString& rutaLocal, qint64 ms) override;
    Resultado<Exito, respaldo::ErrorRestauracion> sincronizar(const respaldo::StagingRestauracion& s) override;
    Resultado<QByteArray, respaldo::ErrorRestauracion> huella(const respaldo::StagingRestauracion& s) override;
    Resultado<Exito, respaldo::ErrorRestauracion> marcarPreparado(const respaldo::StagingRestauracion& s,
                                                                  const respaldo::DatosPreparado& datos) override;
    Resultado<Exito, respaldo::ErrorRestauracion> marcarSolicitado(const respaldo::StagingRestauracion& s) override;
    void descartar(const respaldo::StagingRestauracion& s) override;

    // Huella reproducible de los stagings de un marcador (la usa tambien el
    // arranque para reverificar antes de sustituir). Vacia si un staging
    // tiene algo distinto de lo que se promueve.
    static QByteArray calcularHuella(const QString& stagingDatos, const QString& stagingExtraccion);
    // La misma huella por componentes en su ubicacion actual: SHA-256 y
    // tamano de la base, arbol de `paquetes/` (rutas, tamano, inodo, mtime y
    // ctime de cada ZIP; no depende de donde este la carpeta) y SHA-256 de
    // las preferencias. Permite reverificar a mitad de la promocion.
    static QByteArray huellaComponentes(const QString& base, const QString& paquetes, const QString& preferencias);

private:
    Opciones m_opciones;
    QMutex m_mutex;
};

// OrigenRestauracion sobre un archivo elegido por el usuario. abrir() usa
// QFile (lectura secuencial; un archivo de File Provider sin descargar se
// materializa al leer, porque el usuario lo eligio). huella() es un stat que
// no lo materializa.
class OrigenRestauracionArchivo final : public respaldo::OrigenRestauracion {
public:
    explicit OrigenRestauracionArchivo(QString ruta) : m_ruta(std::move(ruta)) {}

    Resultado<std::unique_ptr<QIODevice>, respaldo::ErrorRestauracion> abrir() override;
    Resultado<respaldo::HuellaArchivo, respaldo::ErrorRestauracion> huella() override;

private:
    QString m_ruta;
};

} // namespace satcfdi
