#pragma once

#include "domain/cfdi/IndiceCfdi.h"
#include "domain/cfdi/ResumenCfdi.h"
#include "domain/cfdi/TipoComprobante.h"
#include "domain/common/Resultado.h"
#include "domain/solicitudes/SolicitudId.h"
#include "ports/cfdi/CfdiArchiveReader.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

namespace satcfdi {

// Paquete visible y Descargado con su estado de indice (T015.2).
struct PaqueteIndexable {
    QString paqueteId;
    SolicitudId solicitudId;
    QString rutaLocal;                          // relativa a la raiz de paquetes
    std::optional<EstadoIndicePaquete> estado;  // nullopt: sin fila de indice
    std::optional<HuellaZip> huella;            // huella del ultimo ZIP leido
    int versionParser = 0;                      // 0 sin fila
};

// Un CFDI del paquete: posicion de su entrada en el ZIP (para que T015.4
// relea el XML) y su resumen. NoEsCfdi no se indexa (solo se cuenta).
struct CfdiIndexado {
    int posicion = 0;
    ResumenCfdi resumen;
    QString textoBusqueda; // normalizarTextoBusqueda / textoBusquedaCfdi
};

// Conteos por paquete (D11: lo unico que va a logs, junto con la causa).
struct ConteosIndicePaquete {
    int xmlLeidos = 0;        // XML entregados por el lector
    int cfdiIlegibles = 0;    // EstadoParseo::Ilegible (indexados con estado)
    int cfdiNoSoportados = 0; // EstadoParseo::VersionNoSoportada (indexados)
    int xmlNoCfdi = 0;        // EstadoParseo::NoEsCfdi (no indexados)
    QMap<MotivoOmision, int> omitidas;

    int totalOmitidas() const
    {
        int total = 0;
        for (int n : omitidas) {
            total += n;
        }
        return total;
    }
};

// Resultado de indexar un paquete, escrito en UNA transaccion: borra lo
// derivado del paquete, inserta `cfdis` y fija estado, huella y conteos.
struct IndicePaquete {
    QString paqueteId;
    EstadoIndicePaquete estado = EstadoIndicePaquete::Pendiente;
    std::optional<HuellaZip> huella;  // Indexado e Ilegible la exigen; OrigenNoDisponible no la lleva
    int versionParser = 1;
    ConteosIndicePaquete conteos;
    // Causa tecnica segura para logs ("no-existe", "corrupto", ...). Solo con
    // OrigenNoDisponible o Ilegible; vacia en otro caso.
    QString causa;
    QList<CfdiIndexado> cfdis; // solo con Indexado
    QDateTime actualizadoEn;
};

enum class OrdenListaCfdi {
    FechaDescendente, // mas recientes primero; sin fecha al final
    FechaAscendente,  // mas antiguos primero; sin fecha al final
};

struct FiltroListaCfdi {
    // Texto libre; se normaliza (normalizarTextoBusqueda) y se busca como
    // subcadena en RFC, nombres, UUID, serie y folio. Vacio = sin filtro.
    QString texto;
    // Tipo de comprobante; nullopt = todos.
    std::optional<TipoComprobante> tipo;
    OrdenListaCfdi orden = OrdenListaCfdi::FechaDescendente;
    // Maximo de filas; 0 = sin limite.
    int limite = 0;
};

struct CfdiListado {
    QString paqueteId;
    int posicion = 0;
    ResumenCfdi resumen; // contiene datos fiscales: no va a logs (D11)
};

// Resumen del indice de una solicitud (paquetes Descargado visibles).
struct ResumenIndiceSolicitud {
    int paquetesDescargados = 0;
    int paquetesIndexados = 0;
    int paquetesPendientes = 0;     // sin fila, Pendiente o version de parser distinta
    int paquetesNoDisponibles = 0;  // OrigenNoDisponible
    int paquetesIlegibles = 0;      // Ilegible
    int cfdiTotal = 0;              // CFDI indexados (incluye ilegibles y no soportados)
    int cfdiIlegibles = 0;
    int cfdiNoSoportados = 0;
    int cfdiParciales = 0;
    int xmlNoCfdi = 0;
    int entradasOmitidas = 0;
    // Lo completa la aplicacion: hay trabajo de indexacion en curso o en
    // cola que alcanza a esta solicitud.
    bool indexando = false;

    friend bool operator==(const ResumenIndiceSolicitud&, const ResumenIndiceSolicitud&) = default;
};

// Catalogo de CFDI (migracion 006, T015 D2/D10). Datos fiscales locales (D3).
// Hilo: tareas del PersistenceDispatcher (conexion por hilo). Escrituras:
// exigen UnitOfWork activo (error Transaccion si no lo hay).
class CfdiCatalogRepository {
public:
    virtual ~CfdiCatalogRepository() = default;

    // Paquetes visibles (paquete y solicitud sin eliminado_en) en estado
    // Descargado, de una solicitud o de todas (nullopt), ordenados por
    // solicitud y paquete. El orden es informativo: IndexadorCfdi agrupa por
    // solicitud por su cuenta y no depende de el.
    virtual Resultado<QList<PaqueteIndexable>, ErrorPersistencia>
    listarPaquetesIndexables(const std::optional<SolicitudId>& solicitud) = 0;

    // Reemplaza lo derivado del paquete. Revalida dentro de la transaccion que
    // el paquete sigue visible y Descargado; si no, no escribe y devuelve
    // false. Integridad si el estado y la huella no son coherentes.
    virtual Resultado<bool, ErrorPersistencia> reemplazarIndicePaquete(const IndicePaquete& indice) = 0;

    // Solo fija tamano y fecha de modificacion (SHA-256 igual: mismo
    // contenido). false si no hay fila con esa huella SHA-256.
    virtual Resultado<bool, ErrorPersistencia> actualizarMetadatosHuella(const QString& paqueteId,
                                                                         const HuellaZip& huella,
                                                                         const QDateTime& actualizadoEn) = 0;

    // CFDI de una solicitud con filtros y orden.
    virtual Resultado<QList<CfdiListado>, ErrorPersistencia> listarCfdi(const SolicitudId& solicitud,
                                                                       const FiltroListaCfdi& filtro) = 0;

    // Resumen del indice de la solicitud; `versionParser` vigente para contar
    // como pendientes los indices de otra version.
    virtual Resultado<ResumenIndiceSolicitud, ErrorPersistencia> resumirSolicitud(const SolicitudId& solicitud,
                                                                                  int versionParser) = 0;
};

} // namespace satcfdi
