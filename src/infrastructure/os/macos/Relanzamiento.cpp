#include "infrastructure/os/macos/Relanzamiento.h"

namespace satcfdi::macos {

QString guionRelanzamiento()
{
    // $1 pid, $2 intentos, $3 modo, $4 destino, resto: argumentos de la app.
    return QStringLiteral(
        "pid=\"$1\"; intentos=\"$2\"; modo=\"$3\"; destino=\"$4\"; shift 4\n"
        "n=0\n"
        "while /bin/kill -0 \"$pid\" 2>/dev/null; do\n"
        "  n=$((n + 1))\n"
        "  if [ \"$n\" -gt \"$intentos\" ]; then exit 75; fi\n"
        "  /bin/sleep 0.2\n"
        "done\n"
        "if [ \"$modo\" = bundle ]; then\n"
        "  if [ \"$#\" -gt 0 ]; then exec /usr/bin/open -n \"$destino\" --args \"$@\"; fi\n"
        "  exec /usr/bin/open -n \"$destino\"\n"
        "fi\n"
        "exec \"$destino\" \"$@\"\n");
}

QStringList argumentosParaRelanzar(const QStringList& argumentosProceso)
{
    QStringList r;
    for (qsizetype i = 1; i < argumentosProceso.size(); ++i) {
        if (!argumentosProceso.at(i).startsWith(QLatin1String("-psn_"))) {
            r.append(argumentosProceso.at(i));
        }
    }
    return r;
}

bool bundleTrasladado(const QString& rutaBundle)
{
    return rutaBundle.split(QLatin1Char('/'), Qt::SkipEmptyParts).contains(QStringLiteral("AppTranslocation"));
}

std::optional<ComandoRelanzamiento> construirComandoRelanzamiento(qint64 pid, const QString& rutaBundle,
                                                   const QString& rutaEjecutable, const QStringList& argumentos,
                                                   int intentos)
{
    // El ejecutable de un bundle trasladado tambien vive bajo AppTranslocation.
    if (bundleTrasladado(rutaBundle) || bundleTrasladado(rutaEjecutable)) {
        return std::nullopt;
    }
    const bool bundle = !rutaBundle.isEmpty() && rutaBundle.endsWith(QLatin1String(".app"));
    ComandoRelanzamiento c;
    c.programa = QStringLiteral("/bin/sh");
    c.argumentos = QStringList{QStringLiteral("-c"),
                               guionRelanzamiento(),
                               QStringLiteral("satcfdi-relanzar"), // $0
                               QString::number(pid),
                               QString::number(intentos),
                               bundle ? QStringLiteral("bundle") : QStringLiteral("ejecutable"),
                               bundle ? rutaBundle : rutaEjecutable};
    c.argumentos.append(argumentos);
    return c;
}

} // namespace satcfdi::macos
