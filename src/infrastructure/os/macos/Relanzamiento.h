#pragma once

// T025.5: comando del proceso auxiliar de relanzamiento (C++ puro, sin AppKit,
// probado sin lanzar la app). Lo usa MacOSIntegration::solicitarRelanzamiento.
//
// Mecanismo: `/bin/sh -c <guion fijo> satcfdi-relanzar <pid> <intentos>
// <modo> <destino> [argumentos...]`, iniciado separado (QProcess::startDetached:
// sobrevive a la app, sin stdio). El guion:
// 1. Espera con `kill -0 <pid>` cada 0.2 s a que la instancia actual termine
//    (asi libera el canal de SingleInstanceCoordinator). Si tras <intentos>
//    sigue viva, sale con 75 sin relanzar.
// 2. modo `bundle`: `exec /usr/bin/open -n <bundle> [--args ...]`
//    (LaunchServices: misma app firmada, contexto Manual, ventana visible).
//    modo `ejecutable` (binario suelto de desarrollo): `exec <ejecutable> ...`.
// El guion es una constante: pid, rutas y argumentos viajan como parametros
// posicionales ("$@"), nunca interpolados, asi que no hay inyeccion.

#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <optional>

namespace satcfdi::macos {

struct ComandoRelanzamiento {
    QString programa;      // /bin/sh
    QStringList argumentos;

    bool operator==(const ComandoRelanzamiento&) const = default;
};

inline constexpr int kIntervaloEsperaRelanzamientoMs = 200;
inline constexpr int kEsperaMaximaRelanzamientoS = 120;
inline constexpr int kIntentosRelanzamiento = kEsperaMaximaRelanzamientoS * 1000 / kIntervaloEsperaRelanzamientoMs;
// Codigo de salida del guion si el proceso no termino a tiempo.
inline constexpr int kSalidaRelanzamientoPlazo = 75;

// Guion fijo del auxiliar.
QString guionRelanzamiento();

// Argumentos a conservar: QCoreApplication::arguments() sin argv[0] y sin
// `-psn_*` (numero de proceso que agrega LaunchServices).
QStringList argumentosParaRelanzar(const QStringList& argumentosProceso);

// R6 (T025.5): App Translocation. Una app en cuarentena abierta desde su
// ubicacion de descarga corre desde una copia de solo lectura en
// `/private/var/folders/.../AppTranslocation/<UUID>/d/<App>.app`; esa ruta
// es efimera y `open -n` sobre ella no es fiable (puede no existir ya o volver
// a trasladarse). La marca que expone el sistema es el componente
// `AppTranslocation` de la ruta del bundle (cubre tambien el caso con
// com.apple.quarantine, que solo se traslada en esa forma). true -> no se
// relanza: el usuario debe mover la app (p. ej. a Aplicaciones) y reabrirla.
bool bundleTrasladado(const QString& rutaBundle);

// `rutaBundle` no vacia (termina en .app) -> modo bundle; si no, modo
// ejecutable con `rutaEjecutable`. nullopt si el bundle esta trasladado
// (bundleTrasladado): no se programa nada.
std::optional<ComandoRelanzamiento> construirComandoRelanzamiento(qint64 pid, const QString& rutaBundle,
                                                   const QString& rutaEjecutable, const QStringList& argumentos,
                                                   int intentos = kIntentosRelanzamiento);

} // namespace satcfdi::macos
