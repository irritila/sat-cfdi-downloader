#pragma once

#include "infrastructure/sat/XmlC14n.h"

#include <QString>
#include <QUrl>

#include <array>

namespace satcfdi::sat {

// Operaciones SOAP del spike (T006). Nombres propios del protocolo SAT; no
// confundir con satcfdi::OperacionSat del dominio (emitidos/recibidos).
enum class Operacion {
    Autentica,
    SolicitaDescargaEmitidos,
    SolicitaDescargaRecibidos,
    VerificaSolicitudDescarga,
    Descargar,
};

inline constexpr std::array<Operacion, 5> kOperaciones = {
    Operacion::Autentica, Operacion::SolicitaDescargaEmitidos, Operacion::SolicitaDescargaRecibidos,
    Operacion::VerificaSolicitudDescarga, Operacion::Descargar,
};

// Datos de transporte de cada operacion.
// Fuente: WSDL productivos descargados el 2026-10-04 (docs/meetings/
// T006-desarrollo/wsdl/) para Autentica, Solicita* y Verifica*. Descargar:
// docs/web-service.md §7 (el WSDL no se publica: HTTP 400); los nombres de
// elementos de su sobre y respuesta son SUPUESTO a confirmar en la corrida real.
struct DescriptorOperacion {
    Operacion operacion;
    QString nombre;         // nombre de la operacion SOAP
    QUrl endpoint;          // soap:address
    QString soapAction;     // header SOAPAction (sin comillas)
    QString espacioNombres; // namespace del cuerpo
    bool requiereToken;     // header Authorization: WRAP access_token="..."
    // C14N por defecto (configurable por llamada, ver OpcionesFirma):
    // Autentica exclusiva (doc §4.2); el resto inclusiva (ejemplo doc §6.1).
    VarianteC14n c14nPorDefecto;
};

const DescriptorOperacion& descriptor(Operacion operacion);

} // namespace satcfdi::sat
