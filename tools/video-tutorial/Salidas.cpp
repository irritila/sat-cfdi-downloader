#include "Salidas.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cerrno>
#include <cstdio>
#include <cstring>

namespace video {

namespace {

// FFMETADATA1: '=', ';', '#', '\' y salto de linea se escapan con '\'.
QString escaparMetadato(const QString& s)
{
    QString r;
    for (const QChar c : s) {
        if (c == QLatin1Char('=') || c == QLatin1Char(';') || c == QLatin1Char('#') || c == QLatin1Char('\\')
            || c == QLatin1Char('\n')) {
            r.append(QLatin1Char('\\'));
        }
        r.append(c);
    }
    return r;
}

} // namespace

QString tiempoSrt(int cuadro)
{
    // ms exactos redondeados: cuadro * 1000 / 30.
    const qint64 ms = (qint64(cuadro) * 1000 + kFps / 2) / kFps;
    return QStringLiteral("%1:%2:%3,%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg((ms / 60000) % 60, 2, 10, QLatin1Char('0'))
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

QByteArray subtitulosSrt(const Guion& guion)
{
    QString r;
    int n = 0;
    for (const Capitulo& c : guion.capitulos) {
        for (const Escena& e : c.escenas) {
            for (const Linea& l : e.lineas) {
                const int inicio = e.inicio + l.inicio;
                r += QStringLiteral("%1\n%2 --> %3\n%4\n\n")
                         .arg(++n)
                         .arg(tiempoSrt(inicio), tiempoSrt(inicio + l.duracion), l.texto);
            }
        }
    }
    return r.toUtf8();
}

QString comentarioHuellas(const Guion& guion)
{
    return QStringLiteral("satcfdi-d16 guion=%1 voz=%2").arg(guion.huella, guion.huellaVoz);
}

QByteArray metadatosCapitulos(const Guion& guion)
{
    QString r = QStringLiteral(";FFMETADATA1\ntitle=%1\n").arg(escaparMetadato(guion.titulo));
    // D16 (T024.4): huellas del guion y de la voz; verificar-d16.cmake las
    // compara con el manifiesto (rechaza un video o una voz anteriores).
    if (!guion.huella.isEmpty()) {
        r += QStringLiteral("comment=%1\n").arg(escaparMetadato(comentarioHuellas(guion)));
    }
    for (const Capitulo& c : guion.capitulos) {
        r += QStringLiteral("\n[CHAPTER]\nTIMEBASE=1/%1\nSTART=%2\nEND=%3\ntitle=%4\n")
                 .arg(kFps)
                 .arg(c.inicio)
                 .arg(c.fin)
                 .arg(escaparMetadato(c.titulo));
    }
    return r.toUtf8();
}

QByteArray lineaDeTiempoJson(const Guion& guion, bool rapido)
{
    QJsonArray capitulos;
    for (const Capitulo& c : guion.capitulos) {
        QJsonArray escenas;
        for (const Escena& e : c.escenas) {
            QJsonArray lineas;
            for (const Linea& l : e.lineas) {
                lineas.append(QJsonObject{{QStringLiteral("id"), l.id},
                                          {QStringLiteral("inicio"), e.inicio + l.inicio},
                                          {QStringLiteral("duracion"), l.duracion},
                                          {QStringLiteral("texto"), l.texto},
                                          {QStringLiteral("voz"), l.textoHablado()},
                                          {QStringLiteral("bloque"), e.bloques.at(l.bloque).id},
                                          // Inicio exacto de su audio (muestras a 48 kHz, globales).
                                          {QStringLiteral("inicioMuestra"),
                                           double(qint64(e.inicio) * kMuestrasPorCuadro + l.inicioMuestra)}});
            }
            QJsonArray bloques;
            for (const Bloque& b : e.bloques) {
                bloques.append(QJsonObject{{QStringLiteral("id"), b.id},
                                           {QStringLiteral("muestras"), double(b.muestras)},
                                           {QStringLiteral("texto"), b.textoHablado}});
            }
            escenas.append(QJsonObject{{QStringLiteral("id"), e.id},
                                       {QStringLiteral("inicio"), e.inicio},
                                       {QStringLiteral("duracion"), e.duracion},
                                       {QStringLiteral("bloques"), bloques},
                                       {QStringLiteral("lineas"), lineas}});
        }
        capitulos.append(QJsonObject{{QStringLiteral("id"), c.id},
                                     {QStringLiteral("titulo"), c.titulo},
                                     {QStringLiteral("inicio"), c.inicio},
                                     {QStringLiteral("fin"), c.fin},
                                     {QStringLiteral("escenas"), escenas}});
    }
    const QJsonObject raiz{{QStringLiteral("esquema"), kEsquema},
                           {QStringLiteral("fps"), kFps},
                           {QStringLiteral("rapido"), rapido},
                           {QStringLiteral("totalCuadros"), guion.totalCuadros},
                           {QStringLiteral("huellaGuion"), guion.huella},
                           {QStringLiteral("huellaVoz"), guion.huellaVoz},
                           {QStringLiteral("ventana"), QJsonArray{guion.ventanaAncho, guion.ventanaAlto}},
                           // T024.4: cuadros de portada (antes del capitulo 1) y cierre (al final).
                           {QStringLiteral("portada"), guion.cuadrosPortada},
                           {QStringLiteral("cierre"), guion.cuadrosCierre},
                           {QStringLiteral("capitulos"), capitulos}};
    return QJsonDocument(raiz).toJson(QJsonDocument::Indented);
}

QString publicar(const QString& dirSalida, const QList<ArchivoPublicado>& archivos)
{
    QTemporaryDir preparacion(QDir(dirSalida).filePath(QStringLiteral(".publicando-XXXXXX")));
    if (!preparacion.isValid()) {
        return QStringLiteral("no se pudo preparar la publicacion en %1").arg(dirSalida);
    }
    QStringList preparados;
    for (const ArchivoPublicado& a : archivos) {
        const QString ruta = QDir(preparacion.path()).filePath(a.nombre);
        if (!a.origen.isEmpty()) {
            if (!QFile::copy(a.origen, ruta)) {
                return QStringLiteral("no se pudo copiar %1").arg(a.nombre);
            }
        } else {
            QFile f(ruta);
            if (!f.open(QIODevice::WriteOnly) || f.write(a.contenido) != a.contenido.size() || !f.flush()) {
                return QStringLiteral("no se pudo escribir %1").arg(a.nombre);
            }
        }
        preparados.append(ruta);
    }
    // Todo preparado: ahora los renombres (atomicos por archivo).
    for (qsizetype i = 0; i < archivos.size(); ++i) {
        const QByteArray origen = QFile::encodeName(preparados.at(i));
        const QByteArray destino = QFile::encodeName(QDir(dirSalida).filePath(archivos.at(i).nombre));
        if (std::rename(origen.constData(), destino.constData()) != 0) {
            return QStringLiteral("no se pudo publicar %1: %2").arg(archivos.at(i).nombre, QString::fromLocal8Bit(std::strerror(errno)));
        }
    }
    return {};
}

} // namespace video
