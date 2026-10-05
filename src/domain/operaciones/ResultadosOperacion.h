#pragma once

#include "domain/solicitudes/EstadosSolicitud.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace satcfdi {

// Resultados SEMANTICOS de OperacionesSat (T007 D3). Sin SOAP, token,
// MaterialFirma ni bytes ZIP. El adaptador (T009) los construye ya
// clasificados; el ejecutor aplica las transiciones con PoliticasOperacion.

// Respuesta explicita de SolicitaDescarga*: CUALQUIER CodEstatus recibido
// (incluidos rechazos documentados) llega aqui; las fallas sin respuesta
// explicita parseable llegan como FallaOperacion.
struct ResultadoEnvio {
    QString codEstatus;
    QString mensaje;                     // texto SAT (se sanea al registrar)
    std::optional<QString> idSolicitudSat;
};

// VerificaSolicitudDescarga con CodEstatus == "5000". Cualquier otro
// CodEstatus llega como FallaOperacion{RespuestaExplicita, codigo}.
struct ResultadoVerificacion {
    QString codEstatus = QStringLiteral("5000");
    QString mensaje;
    EstadoSolicitudSat estadoSolicitudSat = EstadoSolicitudSat::Aceptada;
    std::optional<QString> codigoEstadoSolicitud;
    std::optional<qint64> numeroCfdi;
    QStringList idsPaquetes;             // en el orden de SAT; puede repetir ids ya conocidos
};

// Descargar con CodEstatus == "5000" Y archivo final confirmado por
// PackageStorage (T008). Cualquier otro CodEstatus (5004, 5007, 5008...) llega
// como FallaOperacion{RespuestaExplicita, codigo}; fallas de guardado, como
// FallaOperacion{Almacenamiento, causa}.
struct ResultadoDescarga {
    QString rutaFinal;                   // relativa a la carpeta de paquetes (T008), no absoluta
    QString codEstatus = QStringLiteral("5000");
    QString mensaje;
    // El archivo quedo en su destino final pero sin confirmar durabilidad
    // (p. ej. fsync del directorio fallido): se marca Descargado y se registra.
    std::optional<QString> advertenciaDurabilidad;
};

// existeArchivoFinal (D14): solo el archivo FINAL del paquete.
struct ResultadoArchivoFinal {
    bool existe = false;
    std::optional<QString> rutaFinal;    // relativa, si existe
};

} // namespace satcfdi
