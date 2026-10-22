#include "application/respaldo/AlimentadorRespaldo.h"

namespace satcfdi::respaldo {

DatosNuevoRespaldo datosNuevoRespaldo(const CapturaRespaldo& captura, const QUuid& idInstalacion,
                                      const QDateTime& ahoraUtc)
{
    DatosNuevoRespaldo d;
    d.idInstalacion = idInstalacion;
    d.creadoEnUtc = ahoraUtc.toUTC();
    d.metadatos = captura.metadatos.serializar();
    return d;
}

Resultado<ResumenAlimentacion, ErrorRespaldo> alimentarEscritor(EscritorRespaldo& escritor,
                                                                const CapturaRespaldo& captura,
                                                                const Cancelacion& cancelacion)
{
    using R = Resultado<ResumenAlimentacion, ErrorRespaldo>;
    ResumenAlimentacion resumen;
    resumen.omitidos = captura.paquetesOmitidos;
    for (const EntradaCaptura& entrada : captura.entradas) {
        if (cancelacion.solicitada()) {
            return R::fallo(ErrorRespaldo::de(ErrorRespaldo::Categoria::Cancelado, QStringLiteral("cancelado")));
        }
        if (entrada.faltabaAlCongelar()) {
            if (entrada.tipo != EntradaCaptura::Tipo::Paquete) {
                return R::fallo(
                    ErrorRespaldo::de(ErrorRespaldo::Categoria::FuenteFaltante, QStringLiteral("captura.sin_huella")));
            }
            auto f = escritor.anotarFaltante(entrada.rutaEnRespaldo);
            if (!f) {
                return R::fallo(std::move(f).error());
            }
            ++resumen.faltantes;
            continue;
        }
        auto r = escritor.agregarArchivo(entrada.rutaEnRespaldo, entrada.raiz, entrada.rutaRelativa, cancelacion,
                                         entrada.huella);
        if (r) {
            ++resumen.presentes;
            continue;
        }
        if (r.error().categoria == ErrorRespaldo::Categoria::FuenteFaltante) {
            return R::fallo(
                ErrorRespaldo::de(ErrorRespaldo::Categoria::FuenteCambio, QStringLiteral("fuente.desaparecio")));
        }
        return R::fallo(std::move(r).error());
    }
    return R::exito(resumen);
}

} // namespace satcfdi::respaldo
