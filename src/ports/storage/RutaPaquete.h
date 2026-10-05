#pragma once

#include "domain/common/Resultado.h"
#include "domain/common/Rfc.h"
#include "domain/common/UuidCanonico.h"
#include "ports/storage/TiposAlmacenamiento.h"

#include <QCryptographicHash>
#include <QDate>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

// Derivacion PURA y determinista de rutas de paquetes (T008 D1, D2, D5) y
// reconocimiento de la estructura propia (D6, D9). Sin E/S. Header-only para
// que el adaptador, los fakes y las pruebas compartan exactamente la misma
// gramatica.
//
//   <RFC>/<yyyy-mm>/<UUID solicitud>/<archivo>.zip
//
// - RFC: canonico (normalizado y valido); 12-13 caracteres.
// - yyyy-mm: mes de FechaInicial (D1).
// - UUID: id local canonico (D2).
// - archivo: `<id>` si id_paquete_sat cumple [A-Za-z0-9._-]{1,100} y no
//   empieza por '.'; si no, cada codigo no permitido -> '_', un '.' inicial ->
//   '_', prefijo truncado a 100 y sufijo `--<16 hex SHA-256(id UTF-8)>`.
//   Nombre final <= 122 bytes; temporal <= 145 bytes (< 255).
// - Temporal (D6): `.<archivo>.zip.<16 hex>.part` en la misma carpeta.
namespace satcfdi::rutapaquete {

inline constexpr int kMaxPrefijo = 100;
inline constexpr int kMaxComponente = 255; // bytes

inline bool esCaracterPermitido(char32_t c)
{
    return (c >= U'A' && c <= U'Z') || (c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9') || c == U'.'
           || c == U'_' || c == U'-';
}

// Nombre del archivo final (sin validar vacio: ver derivarRutaRelativa).
inline QString nombreArchivoFinal(const QString& idPaqueteSat)
{
    const QList<uint> codigos = idPaqueteSat.toUcs4();
    bool valido = !codigos.isEmpty() && codigos.size() <= kMaxPrefijo && codigos.first() != U'.';
    for (uint c : codigos) {
        valido = valido && esCaracterPermitido(char32_t(c));
    }
    if (valido) {
        return idPaqueteSat + QStringLiteral(".zip");
    }
    QString prefijo;
    for (uint c : codigos) {
        if (prefijo.size() == kMaxPrefijo) {
            break;
        }
        prefijo.append(esCaracterPermitido(char32_t(c)) ? QChar(char16_t(c)) : QLatin1Char('_'));
    }
    if (prefijo.startsWith(QLatin1Char('.'))) {
        prefijo[0] = QLatin1Char('_');
    }
    const QByteArray hash = QCryptographicHash::hash(idPaqueteSat.toUtf8(), QCryptographicHash::Sha256).toHex();
    return prefijo + QStringLiteral("--") + QString::fromLatin1(hash.left(16)) + QStringLiteral(".zip");
}

// "yyyy-mm" de una fecha SAT "yyyy-MM-ddTHH:mm:ss" valida; nullopt si no.
inline std::optional<QString> periodoDeFechaInicial(const QString& fechaInicialSat)
{
    static const QRegularExpression kFecha(
        QStringLiteral("^(\\d{4})-(\\d{2})-(\\d{2})T([01]\\d|2[0-3]):[0-5]\\d:[0-5]\\d$"));
    const auto m = kFecha.matchView(fechaInicialSat);
    if (!m.hasMatch() || !QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt()).isValid()) {
        return std::nullopt;
    }
    return m.captured(1) + QLatin1Char('-') + m.captured(2);
}

inline bool esRfcComponente(QStringView c)
{
    return !c.isEmpty() && rfc::normalizar(c) == c && rfc::esValido(c);
}

inline bool esPeriodoComponente(QStringView c)
{
    static const QRegularExpression kPeriodo(QStringLiteral("^\\d{4}-(0[1-9]|1[0-2])$"));
    return kPeriodo.matchView(c).hasMatch();
}

// Nombre que derivarRutaRelativa puede producir para un final.
inline bool esNombreFinal(QStringView nombre)
{
    static const QRegularExpression kFinal(QStringLiteral(
        "^(?:[A-Za-z0-9_-][A-Za-z0-9._-]{0,99}|[A-Za-z0-9_-][A-Za-z0-9._-]{0,99}--[0-9a-f]{16})\\.zip$"));
    return kFinal.matchView(nombre).hasMatch();
}

// Si `nombre` es un temporal propio, devuelve el nombre del final asociado.
inline std::optional<QString> finalDeTemporal(QStringView nombre)
{
    static const QRegularExpression kTemporal(QStringLiteral("^\\.(.+\\.zip)\\.[0-9a-f]{16}\\.part$"));
    const auto m = kTemporal.matchView(nombre);
    if (!m.hasMatch() || !esNombreFinal(m.capturedView(1))) {
        return std::nullopt;
    }
    return m.captured(1);
}

// Ruta relativa de un paquete (D5). EntradaInvalida si algun campo no es
// valido; nunca toca el filesystem.
inline Resultado<QString, ErrorAlmacenamiento> derivarRutaRelativa(const UbicacionPaquete& u)
{
    using R = Resultado<QString, ErrorAlmacenamiento>;
    const auto periodo = periodoDeFechaInicial(u.fechaInicialSat);
    if (!esRfcComponente(u.rfcSolicitante) || !periodo || !uuid::esCanonico(u.solicitudId) || u.idPaqueteSat.isEmpty()) {
        return R::fallo(ErrorAlmacenamiento::EntradaInvalida);
    }
    const QStringList componentes = {u.rfcSolicitante, *periodo, u.solicitudId, nombreArchivoFinal(u.idPaqueteSat)};
    for (const QString& c : componentes) {
        if (c.toUtf8().size() > kMaxComponente) {
            return R::fallo(ErrorAlmacenamiento::EntradaInvalida);
        }
    }
    return R::exito(componentes.join(QLatin1Char('/')));
}

// Componentes de una ruta relativa PROPIA de final o temporal:
// exactamente <RFC>/<yyyy-mm>/<UUID>/<nombre>, sin '..', vacios ni '/' inicial.
struct ComponentesRuta {
    QString rfc;
    QString periodo;
    QString solicitudId;
    QString nombre;
};

inline std::optional<ComponentesRuta> separarRutaPropia(QStringView rutaRelativa)
{
    const QList<QStringView> partes = rutaRelativa.split(QLatin1Char('/'));
    if (partes.size() != 4 || !esRfcComponente(partes.at(0)) || !esPeriodoComponente(partes.at(1))
        || !uuid::esCanonico(partes.at(2))) {
        return std::nullopt;
    }
    return ComponentesRuta{partes.at(0).toString(), partes.at(1).toString(), partes.at(2).toString(),
                           partes.at(3).toString()};
}

// Ruta relativa valida de un archivo FINAL (D8: sin '..' ni absolutos, y
// ademas con la forma exacta de D5).
inline bool esRutaFinalValida(QStringView rutaRelativa)
{
    const auto c = separarRutaPropia(rutaRelativa);
    return c && esNombreFinal(c->nombre);
}

} // namespace satcfdi::rutapaquete
