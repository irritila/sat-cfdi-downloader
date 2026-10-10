#pragma once

#include "application/persistence/PuertosPersistencia.h"
#include "application/profiles/PerfilConPreparacion.h"
#include "domain/common/Resultado.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QFuture>
#include <QList>
#include <QObject>

namespace satcfdi {

class PerfilesSatService;
class CredencialesSatService;

// Consulta de preparacion de perfiles (T005.1, DA2). Compone
// PerfilesSatService (perfiles no eliminados) y
// CredencialesSatService::obtenerResumen() (estado + vigenteHasta de la
// metadata 002, sin descifrar).
//
// Hilo/ownership: QObject del hilo grafico; referencias NO propietarias a los
// servicios (deben vivir mas que la consulta). Todos los metodos se llaman y
// completan en el hilo grafico. Sin senales propias: los consumidores
// escuchan perfilesCambiaron/credencialCambio y vuelven a consultar.
//
// Flujo recomendado para la lista (dos fases): listarPersistidos() publica
// todos en Verificando; verificar(perfil) por perfil (y para reintentar).
class ConsultaPreparacionPerfiles : public QObject {
    Q_OBJECT

public:
    using ResultadoLista = Resultado<QList<PerfilConPreparacion>, ErrorPersistencia>;

    // `reloj` (T014.3): para diasParaVencer; por defecto el del sistema.
    ConsultaPreparacionPerfiles(PerfilesSatService& perfiles, CredencialesSatService& credenciales,
                                QObject* parent = nullptr, RelojUtc reloj = relojSistema());

    // Fase 1: no eliminados (activos e inactivos, por RFC) en Verificando,
    // sin consultar credenciales. Error solo si falla listarNoEliminados.
    QFuture<ResultadoLista> listarPersistidos();

    // Fase 2 / reintento para un perfil. NUNCA falla: cualquier error del
    // servicio de credenciales (almacen, persistencia, perfil inexistente),
    // excepcion o cancelacion -> EstadoNoDisponible.
    QFuture<PerfilConPreparacion> verificar(const PerfilResumen& perfil);

    // Ambas fases: todos los no eliminados ya verificados, en orden de RFC.
    QFuture<ResultadoLista> listarVerificados();

    // Selector de nueva solicitud: SOLO listoParaSolicitudes (activo && Lista).
    // No es frontera de seguridad: T009 revalida al crear la solicitud.
    QFuture<ResultadoLista> listarListosParaSolicitudes();

private:
    PerfilesSatService& m_perfiles;
    CredencialesSatService& m_credenciales;
    RelojUtc m_reloj;
};

} // namespace satcfdi
