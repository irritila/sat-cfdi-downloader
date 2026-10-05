#pragma once

#include "domain/solicitudes/SolicitudId.h"

#include <QFuture>
#include <QString>

namespace satcfdi {

// Existencia del ZIP final de un paquete Descargado (T008 D11).
enum class ExistenciaPaquete {
    Presente,
    NoEncontrado,
    ErrorComprobacion,
};

QString claveEstable(ExistenciaPaquete existencia);

// Consulta de existencia que la presentacion solicita (T008 D11). El
// composition root la implementa sobre OperacionExecutor: solo lectura del
// filesystem en el hilo del ejecutor, sin cambiar estado ni escribir logs. El
// future se continua en el hilo grafico (then(contexto, ...)). Un future
// cancelado o una excepcion cuentan como ErrorComprobacion. Sin ella (shell
// sin almacenamiento) no se consulta.
class ConsultaExistenciaPaquetes {
public:
    virtual ~ConsultaExistenciaPaquetes() = default;
    virtual QFuture<ExistenciaPaquete> consultar(const SolicitudId& solicitud, const QString& idPaqueteSat) = 0;
};

} // namespace satcfdi
