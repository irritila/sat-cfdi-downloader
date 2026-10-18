#include "MotoresTts.h"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QThread>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <numbers>

namespace video {

namespace {

constexpr int kReintentosMaximos = 2;     // solo 429 (no cuota) y 5xx
constexpr int kTimeoutMs = 120000;
constexpr int kEsperaMaximaS = 60;

class MotorOpenAi final : public MotorTts {
public:
    explicit MotorOpenAi(QByteArray clave)
        : m_clave(std::move(clave))
    {
    }

    QString sintetizar(const ConfigVoz& c, const QString& texto, QByteArray& audio) override
    {
        const QByteArray cuerpo = QJsonDocument(QJsonObject{{QStringLiteral("model"), c.modelo},
                                                            {QStringLiteral("voice"), c.voz},
                                                            {QStringLiteral("input"), texto},
                                                            {QStringLiteral("instructions"), c.instrucciones},
                                                            {QStringLiteral("response_format"), c.formato},
                                                            {QStringLiteral("speed"), c.velocidad}})
                                      .toJson(QJsonDocument::Compact);
        for (int intento = 0;; ++intento) {
            QNetworkRequest peticion(QUrl(QStringLiteral("https://api.openai.com/v1/audio/speech")));
            peticion.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
            peticion.setRawHeader("Authorization", "Bearer " + m_clave);
            peticion.setTransferTimeout(kTimeoutMs);
            QNetworkReply* r = m_red.post(peticion, cuerpo);
            QEventLoop bucle;
            QObject::connect(r, &QNetworkReply::finished, &bucle, &QEventLoop::quit);
            bucle.exec();
            const int http = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QNetworkReply::NetworkError error = r->error();
            const QByteArray respuesta = r->readAll();
            const QByteArray retryAfter = r->rawHeader("Retry-After");
            r->deleteLater();

            if (error == QNetworkReply::NoError && http == 200 && !respuesta.isEmpty()) {
                audio = respuesta;
                return {};
            }
            if (http == 0) {
                // Sin respuesta HTTP: red bloqueada, DNS, TLS o tiempo agotado.
                // Un timeout es ambiguo (pudo cobrarse): nunca se reintenta.
                return error == QNetworkReply::OperationCanceledError || error == QNetworkReply::TimeoutError
                           ? QStringLiteral("tiempo de espera agotado (la peticion pudo cobrarse; no se reintenta)")
                           : QStringLiteral("error de red (codigo Qt %1); no se reintenta").arg(int(error));
            }
            // Codigo de error de la API (solo para clasificar; el cuerpo no se muestra).
            const QString codigoApi = QJsonDocument::fromJson(respuesta)
                                          .object()
                                          .value(QStringLiteral("error"))
                                          .toObject()
                                          .value(QStringLiteral("code"))
                                          .toString();
            const bool cuota = http == 429 && codigoApi == QLatin1String("insufficient_quota");
            const bool reintentable = (http == 429 && !cuota) || http >= 500;
            if (!reintentable || intento >= kReintentosMaximos) {
                return QStringLiteral("HTTP %1 (%2)").arg(http).arg(categoria(http, cuota));
            }
            bool ok = false;
            int espera = retryAfter.trimmed().toInt(&ok);
            if (!ok || espera <= 0) {
                espera = 2 << intento; // 2 s, 4 s
            }
            espera = std::min(espera, kEsperaMaximaS);
            std::fprintf(stderr, "aviso: HTTP %d; reintento %d de %d en %d s\n", http, intento + 1,
                         kReintentosMaximos, espera);
            QThread::sleep(static_cast<unsigned long>(espera));
        }
    }

private:
    static QString categoria(int http, bool cuota)
    {
        if (cuota) {
            return QStringLiteral("cuota agotada; revisa la facturacion de la cuenta");
        }
        switch (http) {
        case 400:
            return QStringLiteral("solicitud invalida: revisa modelo, voz y parametros");
        case 401:
            return QStringLiteral("autenticacion: OPENAI_API_KEY invalida");
        case 403:
            return QStringLiteral("sin permiso para el modelo o la region");
        case 404:
            return QStringLiteral("modelo o recurso inexistente");
        case 429:
            return QStringLiteral("limite de peticiones");
        default:
            return http >= 500 ? QStringLiteral("error del servicio") : QStringLiteral("error de la API");
        }
    }

    QByteArray m_clave;
    QNetworkAccessManager m_red;
};

// WAV PCM s16 mono 24 kHz (como la API) con 300 ms de silencio, una "voz"
// sintetica (tono con modulacion silabica) proporcional al texto y 400 ms de
// silencio. Determinista: mismo texto, mismos bytes.
class MotorPrueba final : public MotorTts {
public:
    MotorPrueba()
        : m_fallarEn(qEnvironmentVariableIntValue("SATCFDI_VOZ_PRUEBA_FALLAR_EN"))
    {
    }

    QString sintetizar(const ConfigVoz&, const QString& texto, QByteArray& audio) override
    {
        ++m_llamadas;
        if (m_fallarEn > 0 && m_llamadas == m_fallarEn) {
            return QStringLiteral("HTTP 400 (fallo simulado del motor de prueba)");
        }
        // Frases separadas por 350 ms de silencio (para ejercitar la
        // alineacion); SATCFDI_VOZ_PRUEBA_SIN_PAUSAS=1 las une sin pausa
        // (la alineacion debe fallar). Ritmo de la estimacion sin voz:
        // 15 caracteres/s.
        constexpr int kFrecuencia = 24000;
        const bool sinPausas = qEnvironmentVariableIntValue("SATCFDI_VOZ_PRUEBA_SIN_PAUSAS") == 1;
        static const QRegularExpression kFrase(QStringLiteral("(?<=[.?!:])\\s+"));
        const QStringList frases = texto.split(kFrase, Qt::SkipEmptyParts);
        const int pausa = sinPausas ? 0 : kFrecuencia * 350 / 1000;
        const int antes = kFrecuencia * 300 / 1000;
        const int despues = kFrecuencia * 400 / 1000;
        int total = antes + despues;
        for (const QString& f : frases) {
            total += int(f.size()) * kFrecuencia / 15 + pausa;
        }
        QByteArray datos(qsizetype(total) * 2, '\0');
        // Tono fijo por texto, con un hash propio (qHash cambia entre procesos).
        int suma = 0;
        for (const QChar c : texto) {
            suma = (suma * 31 + c.unicode()) % 9973;
        }
        const double tono = 180.0 + double(suma % 60);
        int pos = antes;
        for (const QString& f : frases) {
            const int voz = int(f.size()) * kFrecuencia / 15;
            for (int i = 0; i < voz; ++i) {
                const double t = double(i) / kFrecuencia;
                const double silaba = 0.6 - 0.4 * std::cos(2 * std::numbers::pi * 4.0 * t);
                const double v = 0.3 * silaba * std::sin(2 * std::numbers::pi * tono * t);
                qToLittleEndian<qint16>(qint16(std::lround(v * 32767)), datos.data() + qsizetype(pos + i) * 2);
            }
            pos += voz + pausa;
        }
        audio = cabeceraWav(kFrecuencia, quint32(datos.size())) + datos;
        return {};
    }

private:
    static QByteArray cabeceraWav(quint32 frecuencia, quint32 bytes)
    {
        QByteArray h(44, '\0');
        char* p = h.data();
        std::memcpy(p, "RIFF", 4);
        qToLittleEndian<quint32>(36 + bytes, p + 4);
        std::memcpy(p + 8, "WAVEfmt ", 8);
        qToLittleEndian<quint32>(16, p + 16);
        qToLittleEndian<quint16>(1, p + 20); // PCM
        qToLittleEndian<quint16>(1, p + 22); // mono
        qToLittleEndian<quint32>(frecuencia, p + 24);
        qToLittleEndian<quint32>(frecuencia * 2, p + 28);
        qToLittleEndian<quint16>(2, p + 32);
        qToLittleEndian<quint16>(16, p + 34);
        std::memcpy(p + 36, "data", 4);
        qToLittleEndian<quint32>(bytes, p + 40);
        return h;
    }

    int m_fallarEn = 0;
    int m_llamadas = 0;
};

} // namespace

std::unique_ptr<MotorTts> motorOpenAi(QByteArray clave)
{
    return std::make_unique<MotorOpenAi>(std::move(clave));
}

std::unique_ptr<MotorTts> motorPrueba()
{
    return std::make_unique<MotorPrueba>();
}

} // namespace video
