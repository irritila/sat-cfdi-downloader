#include "domain/solicitudes/SolicitudCanonica.h"

#include "domain/common/Rfc.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <algorithm>

namespace satcfdi {

namespace {

using Codigo = ErrorSolicitudCanonica::Codigo;

bool tieneCaracterReservado(QStringView valor)
{
    for (const QChar c : valor) {
        if (c == u'\n' || c == u'\r' || c == u'=' || c == u',') {
            return true;
        }
    }
    return false;
}

std::optional<QString> opcionalRecortado(const std::optional<QString>& valor)
{
    if (!valor) {
        return std::nullopt;
    }
    QString t = valor->trimmed();
    if (t.isEmpty()) {
        return std::nullopt;
    }
    return t;
}

QString fechaSat(QDate fecha, bool final_)
{
    return fecha.toString(u"yyyy-MM-dd")
           + (final_ ? QStringLiteral("T23:59:59") : QStringLiteral("T00:00:00"));
}

void agregarLinea(QByteArray& destino, const char* clave, const QString& valor)
{
    if (!destino.isEmpty()) {
        destino.append('\n');
    }
    destino.append(clave);
    destino.append('=');
    destino.append(valor.toUtf8());
}

} // namespace

Resultado<SolicitudCanonica, QList<ErrorSolicitudCanonica>>
SolicitudCanonica::normalizar(const EntradaSolicitudCanonica& entrada)
{
    QList<ErrorSolicitudCanonica> errores;
    SolicitudCanonica c;
    c.m_tipo = entrada.tipoDescarga;

    // RFC solicitante.
    if (auto rfc = rfc::normalizarYValidar(entrada.rfcPerfil)) {
        c.m_rfcSolicitante = *rfc;
    } else {
        errores.append({Codigo::RfcSolicitanteInvalido, QStringLiteral("rfc_solicitante"),
                        QStringLiteral("El RFC del perfil no es válido.")});
    }

    // Contrapartes.
    QStringList contrapartes;
    for (const QString& texto : entrada.rfcContrapartes) {
        const QString n = rfc::normalizar(texto);
        if (n.isEmpty()) {
            continue;
        }
        if (!rfc::esValido(n)) {
            errores.append({Codigo::RfcContraparteInvalido, QStringLiteral("rfc_contraparte"),
                            QStringLiteral("El RFC de la contraparte no es válido.")});
            continue;
        }
        contrapartes.append(n);
    }
    std::sort(contrapartes.begin(), contrapartes.end(),
              [](const QString& a, const QString& b) { return a.toUtf8() < b.toUtf8(); });
    contrapartes.erase(std::unique(contrapartes.begin(), contrapartes.end()), contrapartes.end());

    if (c.m_tipo == TipoDescarga::Emitidos) {
        c.m_rfcEmisor = c.m_rfcSolicitante;
        c.m_rfcReceptores = contrapartes;
    } else {
        c.m_rfcReceptor = c.m_rfcSolicitante;
        if (contrapartes.size() > 1) {
            errores.append({Codigo::ContraparteMultipleNoPermitida, QStringLiteral("rfc_emisor"),
                            QStringLiteral("Recibidos admite un solo RFC emisor.")});
        } else if (contrapartes.size() == 1) {
            c.m_rfcEmisor = contrapartes.constFirst();
        }
    }

    // Fechas.
    if (!entrada.fechaInicial.isValid()) {
        errores.append({Codigo::FechaInicialRequerida, QStringLiteral("fecha_inicial_sat"),
                        QStringLiteral("Indica la fecha inicial.")});
    }
    if (!entrada.fechaFinal.isValid()) {
        errores.append({Codigo::FechaFinalRequerida, QStringLiteral("fecha_final_sat"),
                        QStringLiteral("Indica la fecha final.")});
    }
    if (entrada.fechaInicial.isValid() && entrada.fechaFinal.isValid()) {
        if (entrada.fechaFinal < entrada.fechaInicial) {
            errores.append({Codigo::RangoFechasInvalido, QStringLiteral("fecha_final_sat"),
                            QStringLiteral("La fecha final debe ser igual o posterior a la fecha inicial.")});
        }
        c.m_fechaInicialSat = fechaSat(entrada.fechaInicial, false);
        c.m_fechaFinalSat = fechaSat(entrada.fechaFinal, true);
    }

    // Tipo de comprobante.
    if (auto tipo = opcionalRecortado(entrada.tipoComprobante)) {
        const QString t = tipo->toUpper();
        static const QStringList validos = {QStringLiteral("I"), QStringLiteral("E"),
                                            QStringLiteral("T"), QStringLiteral("N"),
                                            QStringLiteral("P")};
        if (validos.contains(t)) {
            c.m_tipoComprobante = t;
        } else {
            errores.append({Codigo::TipoComprobanteInvalido, QStringLiteral("tipo_comprobante"),
                            QStringLiteral("El tipo de comprobante debe ser I, E, T, N o P.")});
        }
    }

    // Complemento.
    if (auto complemento = opcionalRecortado(entrada.complemento)) {
        if (tieneCaracterReservado(*complemento)) {
            errores.append({Codigo::CaracterReservado, QStringLiteral("complemento"),
                            QStringLiteral("El complemento contiene caracteres no permitidos.")});
        } else {
            c.m_complemento = std::move(*complemento);
        }
    }

    if (!errores.isEmpty()) {
        return Resultado<SolicitudCanonica, QList<ErrorSolicitudCanonica>>::fallo(
            std::move(errores));
    }
    c.m_dedupKey = DedupKey::calcularV1(c.serializacionCanonicaV1());
    return Resultado<SolicitudCanonica, QList<ErrorSolicitudCanonica>>::exito(std::move(c));
}

std::optional<QString> SolicitudCanonica::rfcReceptoresJson() const
{
    return serializarRfcReceptoresJson(m_rfcReceptores);
}

std::optional<QString> SolicitudCanonica::rfcContraparte() const
{
    if (m_tipo == TipoDescarga::Emitidos) {
        return m_rfcReceptores.isEmpty() ? std::nullopt
                                         : std::optional<QString>(m_rfcReceptores.constFirst());
    }
    return m_rfcEmisor;
}

QByteArray SolicitudCanonica::serializacionCanonicaV1() const
{
    QByteArray s;
    agregarLinea(s, "operacion_sat", claveEstable(operacionSat()));
    agregarLinea(s, "rfc_solicitante", m_rfcSolicitante);
    if (m_rfcEmisor) {
        agregarLinea(s, "rfc_emisor", *m_rfcEmisor);
    }
    if (m_rfcReceptor) {
        agregarLinea(s, "rfc_receptor", *m_rfcReceptor);
    }
    if (!m_rfcReceptores.isEmpty()) {
        agregarLinea(s, "rfc_receptores", m_rfcReceptores.join(u','));
    }
    agregarLinea(s, "fecha_inicial_sat", m_fechaInicialSat);
    agregarLinea(s, "fecha_final_sat", m_fechaFinalSat);
    agregarLinea(s, "tipo_solicitud", tipoSolicitudSat());
    agregarLinea(s, "estado_comprobante", estadoComprobanteSat());
    if (m_tipoComprobante) {
        agregarLinea(s, "tipo_comprobante", *m_tipoComprobante);
    }
    if (m_complemento) {
        agregarLinea(s, "complemento", *m_complemento);
    }
    return s;
}

std::optional<QString> serializarRfcReceptoresJson(const QStringList& rfcs)
{
    if (rfcs.isEmpty()) {
        return std::nullopt;
    }
    return QString::fromUtf8(
        QJsonDocument(QJsonArray::fromStringList(rfcs)).toJson(QJsonDocument::Compact));
}

std::optional<QStringList> parsearRfcReceptoresJson(QStringView json)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isArray()) {
        return std::nullopt;
    }
    QStringList resultado;
    for (const QJsonValue& v : doc.array()) {
        if (!v.isString()) {
            return std::nullopt;
        }
        resultado.append(v.toString());
    }
    return resultado;
}

} // namespace satcfdi
