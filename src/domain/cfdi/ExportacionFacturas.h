#pragma once

#include "domain/cfdi/IndiceCfdi.h"
#include "domain/cfdi/MesCalendario.h"
#include "domain/perfiles/PerfilId.h"
#include "domain/solicitudes/EstadosSolicitud.h"
#include "domain/solicitudes/SolicitudId.h"

#include <QByteArray>
#include <QDate>
#include <QHash>
#include <QLatin1StringView>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringView>

#include <optional>
#include <variant>

namespace satcfdi {

// Exportacion de facturas a un ZIP (T026, D7-D9, D14-D18). Reglas puras: sin
// SQL, archivos, reloj ni QML. Los UUID y nombres que se manejan aqui son
// datos fiscales: no van a logs (D20).

// Identidad de un CFDI del catalogo (D14): paquete y posicion de su entrada.
struct ReferenciaFactura {
    QString paqueteId;
    int posicion = -1;

    friend bool operator==(const ReferenciaFactura&, const ReferenciaFactura&) = default;
};

inline size_t qHash(const ReferenciaFactura& r, size_t semilla = 0) noexcept
{
    return qHashMulti(semilla, r.paqueteId, r.posicion);
}

// Factura elegida en la pantalla Facturas (D14). `mes` y `direccion` son los
// del catalogo al elegirla; `uuid` es el del resumen (solo se usa si es
// canonico: normalizarUuid).
struct FacturaSeleccionada {
    ReferenciaFactura referencia;
    MesCalendario mes = *MesCalendario::crear(2000, 1);
    TipoDescarga direccion = TipoDescarga::Emitidos;
    QString uuid;
};

// Alcances (D3, D8). Direccion nullopt = Ambas. Con seleccion no hay
// direccion: se exporta exactamente lo elegido, de una o ambas direcciones.
struct AlcanceMesExportacion {
    PerfilId perfil;
    MesCalendario mes = *MesCalendario::crear(2000, 1);
    std::optional<TipoDescarga> direccion;
};

// Rango inclusivo de meses; `desde` <= `hasta` (otro orden es invalido).
struct AlcanceRangoExportacion {
    PerfilId perfil;
    MesCalendario desde = *MesCalendario::crear(2000, 1);
    MesCalendario hasta = *MesCalendario::crear(2000, 1);
    std::optional<TipoDescarga> direccion;
};

struct AlcanceSeleccionExportacion {
    PerfilId perfil;
    QList<FacturaSeleccionada> facturas;
};

using AlcanceExportacion = std::variant<AlcanceMesExportacion, AlcanceRangoExportacion, AlcanceSeleccionExportacion>;

const PerfilId& perfilDe(const AlcanceExportacion& alcance);

// Meses que abarca el alcance, ascendentes y sin repetir (seleccion: los de
// sus facturas). Vacio si el rango esta invertido.
QList<MesCalendario> mesesDe(const AlcanceExportacion& alcance);

// Nombre resuelto de una factura en el ZIP (D7, D18): XML y PDF comparten la
// base. Rutas relativas, sin `..`, absolutas ni nombres de paquete.
struct NombreEntradaFactura {
    QString carpeta; // "emitidas/2026-09"
    QString base;    // "<UUID>", "sin-uuid-<n>", con sufijo "-<n>" si colisiona

    QString xml() const { return carpeta + QLatin1Char('/') + base + QStringLiteral(".xml"); }
    QString pdf() const { return carpeta + QLatin1Char('/') + base + QStringLiteral(".pdf"); }

    friend bool operator==(const NombreEntradaFactura&, const NombreEntradaFactura&) = default;
};

// Asigna nombres en el orden del plan (D18). Una instancia por exportacion
// (la usa PlanificadorExportacion para facturas planeadas y omitidas, de modo
// que ningun nombre se repite); no es thread-safe. Solo usa el UUID canonico de normalizarUuid; cualquier
// otro valor o UUID vacio da `sin-uuid-<n>` (n desde 1 por carpeta). Una base
// ya usada en la carpeta recibe `-<n>` con n desde 2.
class AsignadorNombresExportacion {
public:
    NombreEntradaFactura siguiente(TipoDescarga direccion, MesCalendario mes, QStringView uuid);

private:
    struct Carpeta {
        QSet<QString> usadas;
        int sinUuid = 0;
    };
    QHash<QString, Carpeta> m_carpetas;
};

// Factura que se intentara exportar, en el orden del plan.
struct FacturaPlaneada {
    ReferenciaFactura origen;
    MesCalendario mes = *MesCalendario::crear(2000, 1);
    TipoDescarga direccion = TipoDescarga::Emitidos;
    // UUID canonico esperado (normalizarUuid); nullopt si el indice no lo
    // tiene o no es canonico. T026.2 lo compara con el XML releido (D11).
    std::optional<QString> uuidCanonico;
    // Nombre en el ZIP (D18), asignado en el orden del plan. T026.2 usa
    // `nombre.base` como identificador si la omite al releer.
    NombreEntradaFactura nombre;
    // Paquete fuente: ruta relativa a la raiz de paquetes y huella del ZIP
    // indexado. T026.2 revalida tamano y mtime (y el SHA-256 si difieren)
    // antes de releer; si no coinciden, la factura se omite (D11). Datos
    // locales: no van a logs.
    QString rutaPaquete;
    HuellaZip huellaPaquete;
    // T026.2 (C2): solicitud fuente canonica del CFDI (CfdiMensual::solicitud).
    // ExportacionFacturasService::notificarSolicitudEliminada omite las
    // facturas pendientes de esa solicitud (D19). Nula en las omitidas del plan.
    SolicitudId solicitud;

    friend bool operator==(const FacturaPlaneada&, const FacturaPlaneada&) = default;
};

// Motivos de omision de un CFDI (catalogo fijo D17). Al planear solo se
// producen NoDisponibleEnEsteEquipo (paquete que no esta Descargado, D4) y
// YaNoDisponible (factura elegida que ya no esta, D14); el resto los agrega
// T026.2 al releer o renderizar.
enum class MotivoOmisionFactura {
    NoDisponibleEnEsteEquipo, // "no disponible en este equipo"
    Ilegible,                 // "ilegible"
    CambioDesdeIndexacion,    // "cambió desde que se indexó"
    YaNoDisponible,           // "ya no disponible"
    SinPdf,                   // "sin PDF" (el XML si se incluyo)
};

// Lineas de mes (D9): de un paquete no descargado no se conoce el contenido;
// no cuentan como CFDI omitidos.
enum class MotivoOmisionMes {
    DescargasPendientes, // "descargas pendientes"
    CoberturaIncompleta, // "cobertura incompleta"
};

// Texto exacto del motivo en omitidas.txt (UTF-8).
QString textoMotivo(MotivoOmisionFactura motivo);
QString textoMotivo(MotivoOmisionMes motivo);

// Carpeta de la direccion en el ZIP y en omitidas.txt (D7): "emitidas" o
// "recibidas".
QLatin1StringView carpetaDireccion(TipoDescarga direccion);

// CFDI omitido. `identificador`: UUID canonico (normalizarUuid) o base
// asignada por AsignadorNombresExportacion (`sin-uuid-<n>`, con sufijo
// `-<n>` de colision); el plan siempre usa la base asignada. Cualquier otro
// valor (vacio, no canonico, texto del XML) se escribe como `sin-uuid`: el
// formateador nunca serializa un valor que no haya validado.
struct OmisionFacturaExportacion {
    TipoDescarga direccion = TipoDescarga::Emitidos;
    MesCalendario mes = *MesCalendario::crear(2000, 1);
    QString identificador;
    MotivoOmisionFactura motivo = MotivoOmisionFactura::YaNoDisponible;

    friend bool operator==(const OmisionFacturaExportacion&, const OmisionFacturaExportacion&) = default;
};

struct OmisionMesExportacion {
    TipoDescarga direccion = TipoDescarga::Emitidos;
    MesCalendario mes = *MesCalendario::crear(2000, 1);
    MotivoOmisionMes motivo = MotivoOmisionMes::CoberturaIncompleta;

    friend bool operator==(const OmisionMesExportacion&, const OmisionMesExportacion&) = default;
};

// Lo que recibe formatearOmitidas: las del plan mas las que T026.2 agregue.
using OmisionExportacion = std::variant<OmisionFacturaExportacion, OmisionMesExportacion>;

struct PlanExportacion {
    QList<FacturaPlaneada> facturas; // orden determinista (ver PlanificadorExportacion)
    QList<OmisionExportacion> omisiones;

    // CFDI locales que se intentaran exportar (D16): 0 deshabilita Exportar.
    int conteoPrevisto() const noexcept { return int(facturas.size()); }
};

// Nombre sugerido del ZIP sin RFC (D15): `Archivo-Fiscal-<AAAA-MM>.zip`,
// `Archivo-Fiscal-<AAAA-MM>_<AAAA-MM>.zip` para un rango de varios meses (un
// rango de un solo mes usa la forma de mes) y
// `Archivo-Fiscal-seleccion-<AAAA-MM-DD>.zip` con `fecha` (dia civil de la
// exportacion, lo captura el llamador) para facturas elegidas.
QString nombreSugeridoZip(const AlcanceExportacion& alcance, QDate fecha);

// Saneo del nombre que escribe el usuario (D15). Siempre devuelve un nombre
// valido: NFC; `/` y `:` se sustituyen por `-`; se quitan caracteres de
// control y de formato (p. ej. U+202E), espacios en los extremos y puntos
// iniciales; se retiran las extensiones `.zip` finales (sin distinguir
// mayusculas) y se agrega exactamente una `.zip`; el tallo se recorta sin
// partir caracteres para que el total no pase de 255 bytes UTF-8. Un tallo
// vacio usa `Archivo-Fiscal`.
QString sanearNombreZip(QStringView nombre);

// Contenido de omitidas.txt (D17): UTF-8 sin BOM, LF tras cada linea. Lineas
// de CFDI `<direccion>/<AAAA-MM>/<identificador>: <motivo>` y de mes
// `<direccion>/<AAAA-MM>: <motivo>`. Orden: direccion (emitidas primero),
// mes, lineas de mes antes que las de CFDI, identificador y motivo. Solo se
// escriben una vez las lineas de mes repetidas y las de CFDI repetidas con
// identificador validado (la misma factura); cada omision con identificador no
// validado conserva su linea, para que el numero de lineas de CFDI sea el
// conteo de omisiones (T026 D9). nullopt si no hay lineas (no se crea
// el archivo).
std::optional<QByteArray> formatearOmitidas(const QList<OmisionExportacion>& omisiones);

} // namespace satcfdi
