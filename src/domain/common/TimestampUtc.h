#pragma once

#include <QDateTime>
#include <QString>
#include <QStringView>

#include <optional>

namespace satcfdi::timestamp {

// Timestamps internos persistidos (ADR 0016): UTC ISO-8601 con milisegundos y
// sufijo Z, `yyyy-MM-ddTHH:mm:ss.zzzZ`. Es el unico formato que infraestructura
// escribe en columnas `*_en`; las fechas de filtro SAT NO usan este formato
// (ver SolicitudCanonica).

// Convierte a UTC y formatea. Precondicion: `instante.isValid()`.
QString aTexto(const QDateTime& instante);

// Acepta exactamente `yyyy-MM-ddTHH:mm:ss.zzzZ` (tambien el que produce
// `strftime('%Y-%m-%dT%H:%M:%fZ')` en SQLite). Devuelve QDateTime UTC.
std::optional<QDateTime> desdeTexto(QStringView texto);

// Instante actual en UTC truncado a milisegundos (redondeo estable con aTexto).
QDateTime ahoraUtc();

} // namespace satcfdi::timestamp
