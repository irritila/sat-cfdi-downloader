#pragma once

#include "domain/common/Resultado.h"
#include "domain/paquetes/PaquetePersistido.h"
#include "domain/solicitudes/SolicitudPersistida.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QString>
#include <QStringView>

class QSqlQuery;

// Lectura de filas completas de solicitud_masiva y paquete_solicitud,
// compartida por los repositorios SQLite (T003 y T007). INTERNO de
// src/infrastructure: no se expone fuera.
namespace satcfdi::sqlite {

// Columnas de solicitud_masiva que lee leerSolicitud(), sin alias (la lectura
// es por nombre de columna).
QString columnasSolicitud();
Resultado<SolicitudPersistida, ErrorPersistencia> leerSolicitud(const QSqlQuery& q);

// Columnas de paquete_solicitud que lee leerPaquete(), en orden y opcionalmente
// prefijadas con `alias` (p. ej. "p" -> "p.id, p.solicitud_masiva_id, ...").
// La lectura es POSICIONAL a partir de la columna `desde` (18 columnas; reintento_pendiente_en desde la migracion 004).
QString columnasPaquete(QStringView alias = {});
inline constexpr int kNumColumnasPaquete = 18;
Resultado<PaquetePersistido, ErrorPersistencia> leerPaquete(const QSqlQuery& q, int desde = 0);

} // namespace satcfdi::sqlite
