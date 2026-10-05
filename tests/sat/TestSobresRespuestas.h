#pragma once

#include "SatPruebasComun.h"

#include <QObject>

#include <optional>

// Criterios 3, 4 y 6 de T006: goldens sanitizados de cada sobre, parser de
// respuestas/Faults por fixture y enmascarado de evidencia (control positivo e
// idempotencia).
class TestSobresRespuestas : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void goldensSanitizadosDeCadaSobre_data();
    void goldensSanitizadosDeCadaSobre();
    void filtrosOpcionalesYOrdenDeAtributos();
    void parametrosInvalidos();
    void parsearAutenticaConTtl();
    void parsearSolicitudes();
    void parsearVerificacionSinConfundirCodigos();
    void parsearDescargaConPaquete();
    void parsearFaultSintetico();
    void respuestasInvalidas();
    void enmascaradoControlPositivoEIdempotente();
    void enmascaradoDeSobreConMaterialReal();
    void parserExigeNamespaceYContenedor();
    void autenticaLimpiaElCuerpo();
    void enmascaradoPorContextoIdsOpacos();

private:
    std::optional<satpruebas::MaterialPrueba> m_material;
};
