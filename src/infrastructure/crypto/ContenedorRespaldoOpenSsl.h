#pragma once

// Implementacion OpenSSL 3.2+ del contenedor de respaldo AFB1 (T025.1,
// ADR 0021). El header no expone tipos de OpenSSL. Formato binario y
// limites: ContenedorRespaldoFormato.h.

#include "ports/respaldo/ContenedorRespaldo.h"

namespace satcfdi::crypto {

class ContenedorRespaldoOpenSsl final : public respaldo::ContenedorRespaldo {
public:
    using ErrorRespaldo = respaldo::ErrorRespaldo;
    using KekRespaldo = respaldo::KekRespaldo;
    using ClaveRecuperacion = respaldo::ClaveRecuperacion;
    using ParametrosKdf = respaldo::ParametrosKdf;
    using CabeceraRespaldo = respaldo::CabeceraRespaldo;
    using InventarioRespaldo = respaldo::InventarioRespaldo;
    using DatosNuevoRespaldo = respaldo::DatosNuevoRespaldo;
    using EscritorRespaldo = respaldo::EscritorRespaldo;
    using ProgresoRespaldo = respaldo::ProgresoRespaldo;

    Resultado<Exito, ErrorRespaldo> comprobarDisponible() const override;
    Resultado<QByteArray, ErrorRespaldo> generarSal() const override;
    Resultado<KekRespaldo, ErrorRespaldo> derivarKekContrasena(const BufferSecreto& contrasena,
                                                               const ParametrosKdf& parametros) const override;
    Resultado<ClaveRecuperacion, ErrorRespaldo> generarClaveRecuperacion() const override;
    Resultado<BufferSecreto, ErrorRespaldo> formatearClaveRecuperacion(const ClaveRecuperacion& clave) const override;
    Resultado<ClaveRecuperacion, ErrorRespaldo> leerClaveRecuperacion(const BufferSecreto& texto) const override;
    Resultado<KekRespaldo, ErrorRespaldo> derivarKekRecuperacion(const ClaveRecuperacion& clave) const override;

    Resultado<std::unique_ptr<EscritorRespaldo>, ErrorRespaldo>
    crearEscritor(QIODevice& destino, const KekRespaldo& kekContrasena, const KekRespaldo& kekRecuperacion,
                  DatosNuevoRespaldo datos, ProgresoRespaldo progreso = {}) const override;

    Resultado<CabeceraRespaldo, ErrorRespaldo> leerCabecera(QIODevice& origen) const override;
    Resultado<CabeceraRespaldo, ErrorRespaldo> autenticarCabecera(QIODevice& origen,
                                                                  const KekRespaldo& kek) const override;
    Resultado<InventarioRespaldo, ErrorRespaldo> verificar(QIODevice& origen, const KekRespaldo& kek,
                                                           const respaldo::OpcionesLectura& opciones = {}) const override;
    Resultado<InventarioRespaldo, ErrorRespaldo> extraer(QIODevice& origen, const KekRespaldo& kek,
                                                         const QString& staging,
                                                         const respaldo::OpcionesLectura& opciones = {}) const override;
};

} // namespace satcfdi::crypto
