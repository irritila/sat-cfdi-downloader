#pragma once

#include "application/cfdi/HistorialDescargas.h"
#include "application/persistence/PuertosPersistencia.h"
#include "domain/cfdi/CoberturaMensual.h"
#include "domain/cfdi/MesCalendario.h"
#include "domain/common/Resultado.h"
#include "domain/perfiles/PerfilId.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/repositories/CatalogoMensualRepository.h"
#include "ports/repositories/LoteDescargaGuiadaRepository.h"

#include <QFuture>
#include <QList>
#include <QObject>

#include <optional>

namespace satcfdi {

class PersistenceDispatcher;
class ProgresoIndexacionCfdi;

// Contrato de aplicacion del flujo mensual (T023.2, ADR 0019) para T023.3,
// T023.4 y T023.5: catalogo del perfil por mes, disponibilidad del mes e
// historial de lotes. Sin UI ni creacion de solicitudes (T023.3).
//
// Asincronia: todo devuelve QFuture. El SQL corre en el PersistenceDispatcher
// (solo lectura, su conexion); las continuaciones que tocan el indexador y la
// regla pura corren en el hilo de este objeto (grafico). El consumidor
// continua con QFuture::then(contextoGrafico, ...) y nunca espera en el hilo
// grafico.
//
// Reloj: `disponibilidad` captura el instante UNA vez al iniciar (D19) y lo
// pasa a la regla pura, que lo convierte a America/Mexico_City.
//
// Ownership/hilo: QObject con afinidad al hilo grafico; lo crea y posee el
// composition root despues del dispatcher y del indexador, y lo destruye
// antes de cerrar el dispatcher. Las referencias son no propietarias. Si se
// destruye con trabajo pendiente, sus continuaciones no se ejecutan.
class CatalogoMensualCfdiService final : public QObject {
    Q_OBJECT

public:
    using ResultadoCatalogo = Resultado<CatalogoMensual, ErrorPersistencia>;
    using ResultadoDisponibilidad = Resultado<EvaluacionCoberturaMensual, ErrorPersistencia>;
    using ResultadoLotes = Resultado<QList<LoteGuiado>, ErrorPersistencia>;
    using ResultadoEntradas = Resultado<QList<EntradaHistorial>, ErrorPersistencia>;
    using ResultadoUbicacion = Resultado<std::optional<UbicacionSolicitud>, ErrorPersistencia>;

    enum class Revalidacion {
        No, // evalua con el indice tal como esta
        Si, // pide al indexador revalidar los ZIP de las solicitudes con paquetes Descargado y relee
    };

    CatalogoMensualCfdiService(PersistenceDispatcher& dispatcher, CatalogoMensualRepository& catalogo,
                               LoteDescargaGuiadaRepository& lotes, ProgresoIndexacionCfdi& indexacion,
                               RelojUtc reloj, QObject* parent = nullptr);

    // CFDI deduplicados del perfil en el mes con conteos (ver
    // CatalogoMensualRepository). Perfil nulo -> catalogo vacio.
    QFuture<ResultadoCatalogo> consultar(const ConsultaCatalogoMensual& consulta);

    // Estado, causa y cobertura por direccion del mes (evaluarCoberturaMensual).
    // Con Revalidacion::Si: lee la evidencia, pide revisarSolicitud() para las
    // solicitudes con paquetes Descargado, espera a que terminen, relee y
    // evalua; `indexacionProgramada` se toma del indexador en ese momento.
    QFuture<ResultadoDisponibilidad> disponibilidad(const PerfilId& perfil, MesCalendario mes,
                                                    Revalidacion revalidacion = Revalidacion::Si);

    // Historial de lotes guiados del perfil (de un mes o de todos).
    QFuture<ResultadoLotes> lotes(const PerfilId& perfil, const std::optional<MesCalendario>& mes = std::nullopt);

    // T023.5: historial del perfil (HistorialDescargas.h): lotes, solicitudes
    // sin lote y conteos por fuente en una tarea del dispatcher; despues, en
    // este hilo, la evaluacion sin revalidar de cada mes con lotes (una por
    // mes). Orden: creadoEn DESC (lotes y anteriores mezclados), id DESC.
    // Perfil nulo o eliminado -> lista vacia. Un mes cuya evidencia no se pudo
    // leer usa resultadoLote para sus lotes (no falla todo el historial).
    QFuture<ResultadoEntradas> historial(const PerfilId& perfil);

    // T023.5: ubicacion de una solicitud visible (destino de notificaciones).
    QFuture<ResultadoUbicacion> ubicar(const SolicitudId& solicitud);

    // Mes en curso y mes inicial por omision (anterior al en curso; en enero,
    // diciembre del anio previo), segun el reloj inyectado; nullopt si el
    // reloj cae fuera del dominio de MesCalendario.
    std::optional<MesCalendario> mesEnCurso() const;
    std::optional<MesCalendario> mesInicialPorOmision() const;

private:
    using ResultadoEvidencia = Resultado<QList<EvidenciaSolicitudMensual>, ErrorPersistencia>;

    QFuture<ResultadoEvidencia> leerEvidencia(const PerfilId& perfil, MesCalendario mes);
    ResultadoDisponibilidad evaluar(const PerfilId& perfil, MesCalendario mes, const QDateTime& instante,
                                    ResultadoEvidencia evidencia) const;

    PersistenceDispatcher& m_dispatcher;
    CatalogoMensualRepository& m_catalogo;
    LoteDescargaGuiadaRepository& m_lotes;
    ProgresoIndexacionCfdi& m_indexacion;
    RelojUtc m_reloj;
};

} // namespace satcfdi
