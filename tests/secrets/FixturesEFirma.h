#pragma once

// Fixtures sinteticos de e.firma generados en tiempo de prueba (T005, DA7,
// DC2) con fixtures/generar_fixtures.sh y el openssl CLI que fija CMake. Nada
// binario se versiona: cada ejecucion genera un par nuevo en un QTemporaryDir
// con una contrasena aleatoria (centinela unico por ejecucion, DS5).

#include "ports/secrets/SecretStoreTypes.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QString>
#include <QTemporaryDir>
#include <QTimeZone>

#ifndef SATCFDI_OPENSSL_EXECUTABLE
#error "SATCFDI_OPENSSL_EXECUTABLE debe definirse en CMake"
#endif
#ifndef SATCFDI_SCRIPT_FIXTURES
#error "SATCFDI_SCRIPT_FIXTURES debe definirse en CMake"
#endif

namespace fixtures {

// Vigencia fija del script: 2025-01-01 .. 2029-01-01 UTC.
inline QDateTime utc(int anio, int mes, int dia)
{
    return QDateTime(QDate(anio, mes, dia), QTime(0, 0), QTimeZone::UTC);
}
inline QDateTime ahoraVigente() { return utc(2026, 6, 1); }
inline QDateTime antesDeVigencia() { return utc(2024, 12, 31); }
inline QDateTime despuesDeVigencia() { return utc(2029, 1, 1); } // == notAfter -> Vencida

inline const QString kRfc = QStringLiteral("GOMA800101AB1");
inline const QString kOtroRfc = QStringLiteral("XEXX010101AB2");
inline const QString kSerie = QStringLiteral("3030303031303030303030373030303030303031");

class FixturesEFirma {
public:
    // Genera los fixtures; error() describe el fallo (sin secretos).
    FixturesEFirma()
    {
        if (!m_dir.isValid()) {
            m_error = QStringLiteral("QTemporaryDir invalido");
            return;
        }
        // Contrasena aleatoria con un caracter no ASCII (UTF-8 multibyte).
        m_contrasena = QStringLiteral("Clave-%1-ñ-%2")
                           .arg(QRandomGenerator::system()->generate64(), 16, 16, QLatin1Char('0'))
                           .arg(QRandomGenerator::system()->generate(), 8, 16, QLatin1Char('0'))
                           .toUtf8();
        QProcess proceso;
        QProcessEnvironment entorno = QProcessEnvironment::systemEnvironment();
        entorno.insert(QStringLiteral("FIXTURE_PASSWORD"), QString::fromUtf8(m_contrasena));
        proceso.setProcessEnvironment(entorno);
        proceso.setProcessChannelMode(QProcess::MergedChannels);
        proceso.start(QStringLiteral("/bin/sh"),
                      {QStringLiteral(SATCFDI_SCRIPT_FIXTURES), QStringLiteral(SATCFDI_OPENSSL_EXECUTABLE), m_dir.path()});
        if (!proceso.waitForFinished(120000) || proceso.exitStatus() != QProcess::NormalExit
            || proceso.exitCode() != 0) {
            m_error = QStringLiteral("generar_fixtures.sh fallo: ") + QString::fromUtf8(proceso.readAll());
        }
    }

    bool valido() const { return m_error.isEmpty(); }
    const QString& error() const { return m_error; }
    QString directorio() const { return m_dir.path(); }
    QString ruta(const QString& nombre) const { return m_dir.filePath(nombre); }

    QByteArray leer(const QString& nombre) const
    {
        QFile f(ruta(nombre));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    // Contrasena valida (UTF-8). Centinela DS5.
    const QByteArray& contrasena() const { return m_contrasena; }
    satcfdi::BufferSecreto bufferContrasena() const
    {
        return satcfdi::BufferSecreto::desdeBytes(m_contrasena.constData(), static_cast<std::size_t>(m_contrasena.size()));
    }
    static satcfdi::BufferSecreto bufferDe(const QByteArray& bytes)
    {
        return satcfdi::BufferSecreto::desdeBytes(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    }

    satcfdi::EntradaEFirma entrada(const QString& cer, const QString& key) const
    {
        satcfdi::EntradaEFirma e;
        e.rutaCertificado = ruta(cer);
        e.rutaLlavePrivada = ruta(key);
        e.contrasena = bufferContrasena();
        return e;
    }

private:
    QTemporaryDir m_dir;
    QString m_error;
    QByteArray m_contrasena;
};

} // namespace fixtures
