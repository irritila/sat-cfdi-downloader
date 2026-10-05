#include "infrastructure/sat/SatOperaciones.h"

namespace satcfdi::sat {

namespace {

const QString kNsAutenticacion = QStringLiteral("http://DescargaMasivaTerceros.gob.mx");
const QString kNsServicios = QStringLiteral("http://DescargaMasivaTerceros.sat.gob.mx");

DescriptorOperacion crear(Operacion op, const char* nombre, const char* endpoint, const char* accion,
                          const QString& ns, bool token, VarianteC14n c14n)
{
    return DescriptorOperacion{op,  QString::fromLatin1(nombre), QUrl(QString::fromLatin1(endpoint)),
                               QString::fromLatin1(accion), ns, token, c14n};
}

} // namespace

const DescriptorOperacion& descriptor(Operacion operacion)
{
    static const std::array<DescriptorOperacion, 5> kDescriptores = {
        crear(Operacion::Autentica, "Autentica",
              "https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/Autenticacion/Autenticacion.svc",
              "http://DescargaMasivaTerceros.gob.mx/IAutenticacion/Autentica", kNsAutenticacion, false,
              VarianteC14n::Exclusiva),
        crear(Operacion::SolicitaDescargaEmitidos, "SolicitaDescargaEmitidos",
              "https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/SolicitaDescargaService.svc",
              "http://DescargaMasivaTerceros.sat.gob.mx/ISolicitaDescargaService/SolicitaDescargaEmitidos",
              kNsServicios, true, VarianteC14n::Inclusiva),
        crear(Operacion::SolicitaDescargaRecibidos, "SolicitaDescargaRecibidos",
              "https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/SolicitaDescargaService.svc",
              "http://DescargaMasivaTerceros.sat.gob.mx/ISolicitaDescargaService/SolicitaDescargaRecibidos",
              kNsServicios, true, VarianteC14n::Inclusiva),
        crear(Operacion::VerificaSolicitudDescarga, "VerificaSolicitudDescarga",
              "https://cfdidescargamasivasolicitud.clouda.sat.gob.mx/VerificaSolicitudDescargaService.svc",
              "http://DescargaMasivaTerceros.sat.gob.mx/IVerificaSolicitudDescargaService/VerificaSolicitudDescarga",
              kNsServicios, true, VarianteC14n::Inclusiva),
        crear(Operacion::Descargar, "Descargar",
              "https://cfdidescargamasiva.clouda.sat.gob.mx/DescargaMasivaService.svc",
              "http://DescargaMasivaTerceros.sat.gob.mx/IDescargaMasivaTercerosService/Descargar", kNsServicios,
              true, VarianteC14n::Inclusiva),
    };
    return kDescriptores.at(static_cast<std::size_t>(operacion));
}

} // namespace satcfdi::sat
