#pragma once

#include "domain/cfdi/CoberturaMensual.h"
#include "domain/cfdi/MesCalendario.h"
#include "domain/cfdi/TipoComprobante.h"
#include "domain/common/Resultado.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/repositories/CfdiCatalogRepository.h"

#include <QHash>
#include <QList>
#include <QString>

#include <array>
#include <optional>

namespace satcfdi {

// Consulta del catalogo de un perfil en un mes (T023.2, ADR 0019, D13).
struct ConsultaCatalogoMensual {
    PerfilId perfil;
    MesCalendario mes = *MesCalendario::crear(2000, 1);
    std::optional<TipoDescarga> direccion; // nullopt = ambas
    // Texto libre normalizado como en FiltroListaCfdi (RFC, nombres, UUID,
    // serie y folio). Vacio = sin filtro.
    QString texto;
    std::optional<TipoComprobante> tipo;
    OrdenListaCfdi orden = OrdenListaCfdi::FechaDescendente;
    int limite = 0;         // 0 = sin limite
    int desplazamiento = 0; // filas omitidas antes de `limite`
    // Conteos por mes del anio de `mes` (12 valores). Se puede omitir cuando
    // solo cambia la pagina o el texto y la UI conserva los anteriores.
    bool conConteosAnuales = true;
};

struct CfdiMensual {
    CfdiListado cfdi;                       // datos fiscales: no van a logs
    TipoDescarga direccion = TipoDescarga::Emitidos; // de la solicitud fuente
    SolicitudId solicitud;                  // solicitud fuente canonica del UUID
    bool fuenteGuiada = false;              // la fuente pertenece a un lote guiado
};

// Conteos sobre el mismo universo deduplicado y los mismos filtros que la
// lista, salvo la dimension que agrupan:
// - emitidos/recibidos: perfil, mes, texto y tipo (sin direccion);
// - porMes[0..11]: perfil, anio de `mes`, direccion, texto y tipo (sin mes).
struct ConteosCatalogoMensual {
    int emitidos = 0;
    int recibidos = 0;
    std::optional<std::array<int, 12>> porMes; // nullopt si !conConteosAnuales
};

struct CatalogoMensual {
    QList<CfdiMensual> cfdis; // pagina pedida
    int total = 0;            // filas de la lista sin limite ni desplazamiento
    ConteosCatalogoMensual conteos;
};

// T026.1: un mes de la instantanea de exportacion (T026 D11).
struct MesInstantaneaExportacion {
    MesCalendario mes = *MesCalendario::crear(2000, 1);
    // consultar() del mes sin texto, tipo, direccion ni paginacion (ambas
    // direcciones, misma deduplicacion D13), en orden FechaAscendente.
    QList<CfdiMensual> cfdis;
    // leerEvidencia() del mes (indexacionProgramada en false).
    QList<EvidenciaSolicitudMensual> evidencia;
};

// Paquete fuente de los CFDI de la instantanea: lo que T026.2 necesita para
// resolver el ZIP y revalidar sus metadatos antes de releer (T026 D11).
struct PaqueteFuenteExportacion {
    QString rutaLocal;               // relativa a la raiz de paquetes (T008 D5); no va a logs
    std::optional<HuellaZip> huella; // del ZIP indexado (tamano, mtime, SHA-256); nullopt sin huella

    friend bool operator==(const PaqueteFuenteExportacion&, const PaqueteFuenteExportacion&) = default;
};

struct InstantaneaExportacion {
    QList<MesInstantaneaExportacion> meses; // en el orden pedido
    // paqueteId -> fuente, para cada paquete de algun CFDI de `meses`, leido
    // en la misma transaccion.
    QHash<QString, PaqueteFuenteExportacion> paquetes;
};

// Lecturas del flujo mensual (migraciones 006 y 007). Universo: CFDI
// indexados de solicitudes y paquetes visibles de un perfil visible, cuya
// `fecha` (fecha del comprobante) cae en el mes. CFDI sin fecha no pertenecen
// a ningun mes.
//
// Deduplicacion (D13): por UUID no vacio normalizado (trim, mayusculas). La
// fuente canonica se elige, en orden, por: solicitud vinculada a un lote
// guiado; indexacion mas antigua (indice_cfdi_paquete.actualizado_en); menor
// id de paquete; menor posicion. Si la fuente se elimina, la siguiente toma
// su lugar. CFDI sin UUID no se deduplican.
//
// Hilo: tareas del PersistenceDispatcher; solo lectura (Tx opcional).
// Errores: Almacenamiento, Interno (fila ilegible).
class CatalogoMensualRepository {
public:
    virtual ~CatalogoMensualRepository() = default;

    virtual Resultado<CatalogoMensual, ErrorPersistencia> consultar(const ConsultaCatalogoMensual& consulta) = 0;

    // Evidencia para evaluarCoberturaMensual: solicitudes visibles del perfil
    // (visible) cuyo rango SAT intersecta el mes, con sus paquetes visibles y
    // el estado de indice de los Descargado. `versionParser`: version vigente
    // para marcar indiceVigente. Una fila de indice con estado desconocido se
    // informa como indiceLegible = false (no aborta la lectura).
    // indexacionProgramada queda en false: la completa la aplicacion.
    // Orden: creada_en, id; paquetes por id.
    virtual Resultado<QList<EvidenciaSolicitudMensual>, ErrorPersistencia>
    leerEvidencia(const PerfilId& perfil, MesCalendario mes, int versionParser) = 0;

    // T023.5: CFDI del perfil (todos los meses, con fecha) por solicitud
    // fuente canonica, con la misma deduplicacion que consultar(): la suma por
    // mes coincide con conteos.emitidos + conteos.recibidos del mes sin
    // filtros. Solicitudes sin CFDI no aparecen.
    virtual Resultado<QHash<SolicitudId, int>, ErrorPersistencia> conteosPorSolicitud(const PerfilId& perfil) = 0;

    // T026.1 (T026 D11): instantanea para planear una exportacion. Para cada
    // mes distinto de `meses` (un repetido se lee una vez), todos sus CFDI y su evidencia, leidos en
    // UNA transaccion de lectura (todas las lecturas ven el mismo estado de la
    // base; si el llamador ya tiene un UnitOfWork activo se lee en el). Sin
    // paginacion: el universo esperado es de miles de CFDI (README).
    // Errores: los de consultar() y leerEvidencia(); ninguna lectura parcial.
    virtual Resultado<InstantaneaExportacion, ErrorPersistencia>
    capturarParaExportar(const PerfilId& perfil, const QList<MesCalendario>& meses, int versionParser) = 0;
};

} // namespace satcfdi
