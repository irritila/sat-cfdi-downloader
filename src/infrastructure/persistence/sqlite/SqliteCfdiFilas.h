#pragma once

#include "domain/common/Resultado.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/repositories/CfdiCatalogRepository.h"

#include <QString>
#include <QStringView>
#include <QVariant>

class QSqlQuery;

// Lectura de filas de cfdi_indexado compartida por el catalogo por solicitud
// (T015.2) y el catalogo mensual (T023.2). INTERNO de src/infrastructure.
namespace satcfdi::sqlite {

// Columnas que lee leerCfdi(), en orden (27). Sin alias: nombres sin prefijo.
extern const QString kColumnasCfdi;
inline constexpr int kNumColumnasCfdi = 27;
// Las mismas columnas prefijadas con `alias` ("c" -> "c.paquete_solicitud_id, ...").
QString columnasCfdi(QStringView alias);

// Lectura POSICIONAL de kColumnasCfdi a partir de la columna `desde`.
Resultado<CfdiListado, ErrorPersistencia> leerCfdi(const QSqlQuery& q, int desde = 0);

// Texto de columna opcional: NULL -> cadena vacia.
QString leerTexto(const QVariant& valor);

// Patron LIKE "%texto%" con comodines escapados (ESCAPE '\').
QString patronLike(const QString& texto);

} // namespace satcfdi::sqlite
