#pragma once

// CfdiArchiveReader en memoria (T015.1) para pruebas de orquestacion e indice.
// Respeta el contrato del puerto: entrega los XML en orden, cancela si el
// callback devuelve false (exito con lo leido) y, si hay un error programado,
// devuelve solo el error sin invocar el callback. Sin libzip ni disco.
//
//   fakes::FakeCfdiArchiveReader lector;
//   lector.agregarXml("<cfdi:Comprobante .../>");          // indice 0
//   lector.agregarOmision(satcfdi::MotivoOmision::NoXml);   // indice 1 (consume indice)
//   lector.programarError({Tipo::Ilegible, Causa::Truncado});

#include "ports/cfdi/CfdiArchiveReader.h"

#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>

#include <optional>
#include <utility>

namespace fakes {

class FakeCfdiArchiveReader final : public satcfdi::CfdiArchiveReader {
public:
    void agregarXml(QByteArray contenido)
    {
        QMutexLocker bloqueo(&m_mutex);
        m_xml.append(satcfdi::EntradaXmlCfdi{m_siguienteIndice++, std::move(contenido)});
    }

    void agregarOmision(satcfdi::MotivoOmision motivo)
    {
        QMutexLocker bloqueo(&m_mutex);
        ++m_omitidas[motivo];
        ++m_siguienteIndice;
    }

    void programarError(satcfdi::ErrorPaqueteZip error)
    {
        QMutexLocker bloqueo(&m_mutex);
        m_error = error;
    }

    QStringList rutasRecorridas() const
    {
        QMutexLocker bloqueo(&m_mutex);
        return m_rutas;
    }

    satcfdi::Resultado<satcfdi::ResumenLecturaZip, satcfdi::ErrorPaqueteZip> recorrer(
        const QString& rutaZip,
        const std::function<bool(const satcfdi::EntradaXmlCfdi&)>& alXml) override
    {
        using R = satcfdi::Resultado<satcfdi::ResumenLecturaZip, satcfdi::ErrorPaqueteZip>;
        QList<satcfdi::EntradaXmlCfdi> xml;
        satcfdi::ResumenLecturaZip resumen;
        {
            QMutexLocker bloqueo(&m_mutex);
            m_rutas.append(rutaZip);
            if (m_error)
                return R::fallo(*m_error);
            xml = m_xml;
            resumen.omitidas = m_omitidas;
        }
        for (const auto& entrada : std::as_const(xml)) {
            ++resumen.xmlEntregados;
            if (!alXml(entrada))
                break;
        }
        return R::exito(std::move(resumen));
    }

private:
    mutable QMutex m_mutex;
    QList<satcfdi::EntradaXmlCfdi> m_xml;
    QMap<satcfdi::MotivoOmision, int> m_omitidas;
    std::optional<satcfdi::ErrorPaqueteZip> m_error;
    QStringList m_rutas;
    int m_siguienteIndice = 0;
};

} // namespace fakes
