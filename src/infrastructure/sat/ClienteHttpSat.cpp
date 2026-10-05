#include "infrastructure/sat/ClienteHttpSat.h"

#include <QMetaEnum>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

namespace satcfdi::sat {

QString claveEstable(FaseResultado fase)
{
    switch (fase) {
    case FaseResultado::AntesDeEnvio: return QStringLiteral("AntesDeEnvio");
    case FaseResultado::DespuesDeEnvio: return QStringLiteral("DespuesDeEnvio");
    case FaseResultado::RespuestaExplicita: return QStringLiteral("RespuestaExplicita");
    }
    return {};
}

ClienteHttpSat::ClienteHttpSat(QObject* parent)
    : QObject(parent)
{
    m_red.setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
}

ClienteHttpSat::~ClienteHttpSat()
{
    // Las promesas pendientes se cancelan al destruirse (QPromise).
    m_cola.clear();
}

QFuture<ResultadoHttp> ClienteHttpSat::enviar(const PeticionSat& peticion, const TokenSat* token)
{
    Pendiente p;
    p.peticion = peticion;
    if (token != nullptr && !token->vacio()) {
        p.autorizacion = token->encabezadoAutorizacion();
    }
    p.promesa = std::make_shared<QPromise<ResultadoHttp>>();
    p.promesa->start();
    QFuture<ResultadoHttp> futuro = p.promesa->future();
    m_cola.push_back(std::move(p));
    if (!m_enCurso) {
        iniciarSiguiente();
    }
    return futuro;
}

void ClienteHttpSat::cancelarEnCurso()
{
    // Copia: abort() puede emitir finished de forma sincrona, y ese manejador
    // vacia m_abortarEnCurso.
    const std::function<void()> abortar = m_abortarEnCurso;
    if (abortar) {
        abortar();
    }
}

void ClienteHttpSat::iniciarSiguiente()
{
    if (m_cola.empty()) {
        m_enCurso = false;
        return;
    }
    m_enCurso = true;
    Pendiente actual = std::move(m_cola.front());
    m_cola.pop_front();

    QNetworkRequest req(actual.peticion.url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("text/xml; charset=utf-8"));
    req.setRawHeader("SOAPAction", '"' + actual.peticion.soapAction.toUtf8() + '"');
    if (!actual.autorizacion.isEmpty()) {
        req.setRawHeader("Authorization", actual.autorizacion);
    }
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    // setRawHeader comparte el buffer (implicit sharing): sobrescribirlo aqui
    // forzaria una copia nueva. Solo se suelta la referencia propia; la de Qt
    // vive mientras exista la peticion (best effort, ver header).
    actual.autorizacion = QByteArray();

    QNetworkReply* reply = m_red.post(req, actual.peticion.sobre);
    struct Estado {
        bool enviado = false;
        bool vencido = false;
        bool cancelado = false;
    };
    auto estado = std::make_shared<Estado>();
    m_abortarEnCurso = [estado, r = QPointer<QNetworkReply>(reply)]() {
        estado->cancelado = true;
        if (r) {
            r->abort();
        }
    };
    auto* temporizador = new QTimer(reply);
    temporizador->setSingleShot(true);
    connect(reply, &QNetworkReply::requestSent, reply, [estado]() { estado->enviado = true; });
    connect(temporizador, &QTimer::timeout, reply, [estado, reply]() {
        estado->vencido = true;
        reply->abort();
    });
    auto promesa = actual.promesa;
    connect(reply, &QNetworkReply::finished, this, [this, reply, estado, promesa, temporizador]() {
        temporizador->stop();
        m_abortarEnCurso = {};
        ResultadoHttp r;
        r.deadlineVencido = estado->vencido;
        r.cancelado = estado->cancelado;
        const QNetworkReply::NetworkError err = reply->error();
        r.diagnostico = QString::fromLatin1(QMetaEnum::fromType<QNetworkReply::NetworkError>().valueToKey(err));
        const QVariant codigo = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        // HTTP completo: hay codigo de estado, no vencio el deadline y el
        // error (si lo hay) es de nivel HTTP (>= 200 en QNetworkReply).
        const bool httpCompleto = codigo.isValid() && !estado->vencido && !estado->cancelado
                                  && (err == QNetworkReply::NoError || static_cast<int>(err) >= 200);
        if (httpCompleto) {
            r.fase = FaseResultado::RespuestaExplicita;
            r.estadoHttp = codigo.toInt();
            r.cuerpo = reply->readAll();
        } else {
            r.fase = estado->enviado ? FaseResultado::DespuesDeEnvio : FaseResultado::AntesDeEnvio;
        }
        reply->deleteLater();
        promesa->addResult(std::move(r));
        promesa->finish();
        iniciarSiguiente();
    });
    temporizador->start(m_deadline);
}

} // namespace satcfdi::sat
