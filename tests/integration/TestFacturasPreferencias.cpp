// T023.4 (integracion): preferencia del ultimo perfil de Facturas con el root
// real y PreferenciasFacturasArchivo: se escribe en <data-dir>/preferencias.ini,
// se recupera al recrear el root y, si el ID ya no es visible, se usa el
// primer perfil. Datos sinteticos; sin SAT.

#include "app_core/AppBootstrapper.h"
#include "app_core/AppCompositionRoot.h"

#include "application/profiles/PerfilesSatService.h"
#include "presentation/viewmodels/AppViewModel.h"
#include "presentation/viewmodels/FacturasViewModel.h"
#include "presentation/viewmodels/PresentacionViewModels.h"

#include "fakes/FakeSecretStore.h"

#include <QDir>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <optional>

using namespace satcfdi;

namespace {

template <typename T>
std::optional<T> esperar(QFuture<T> f)
{
    if (!QTest::qWaitFor([&] { return f.isFinished(); }, 5000) || f.isCanceled()) {
        return std::nullopt;
    }
    return f.result();
}

struct Datos {
    QTemporaryDir tmp;
    fakes::FakeSecretStore secretos;
    QString rutaBase;
    std::unique_ptr<AppCompositionRoot> root;

    bool arrancar()
    {
        root.reset();
        AppBootstrapper::Opciones opciones;
        opciones.directorioDatos = tmp.path();
        const auto arranque = AppBootstrapper(opciones).preparar();
        if (!arranque.esExito()) {
            return false;
        }
        rutaBase = arranque.valor().rutaBase;
        root = std::make_unique<AppCompositionRoot>(rutaBase, secretos, OpcionesMonitoreo{});
        return true;
    }
    FacturasViewModel* facturas() const { return root->viewModels().facturas(); }
    QString ini() const { return QDir(tmp.path()).filePath(QStringLiteral("preferencias.ini")); }
};

} // namespace

class TestFacturasPreferencias : public QObject {
    Q_OBJECT

private slots:
    void ultimoPerfilPersisteYCaeAlPrimero();
};

void TestFacturasPreferencias::ultimoPerfilPersisteYCaeAlPrimero()
{
    Datos d;
    QVERIFY(d.tmp.isValid());
    QVERIFY(d.arrancar());
    QCOMPARE(d.root->viewModels().app()->pagina(), AppViewModel::Pagina::Facturas);
    const auto a = esperar(d.root->perfiles().crear(QStringLiteral("XAXX010101000"), QStringLiteral("Perfil A")));
    const auto b = esperar(d.root->perfiles().crear(QStringLiteral("XEXX010101000"), QStringLiteral("Perfil B")));
    QVERIFY(a && a->esExito() && b && b->esExito());
    const QString idB = b->valor().id.texto();
    // Facturas recarga los perfiles al cambiar la lista; primero sin preferencia.
    QTRY_COMPARE(d.facturas()->perfiles()->count(), 2);
    QTRY_COMPARE(d.facturas()->perfilRfc(), QStringLiteral("XAXX010101000"));
    d.facturas()->seleccionarPerfil(1);
    QCOMPARE(d.facturas()->perfilId(), idB);
    QCOMPARE(QSettings(d.ini(), QSettings::IniFormat).value(QStringLiteral("facturas/ultimoPerfil")).toString(), idB);

    // Recrear el root: se recupera el ultimo perfil usado.
    QVERIFY(d.arrancar());
    QTRY_COMPARE(d.facturas()->perfiles()->count(), 2);
    QTRY_COMPARE(d.facturas()->perfilId(), idB);

    // ID ya no visible (p. ej. perfil eliminado): se usa el primer perfil.
    d.root.reset();
    {
        QSettings s(d.ini(), QSettings::IniFormat);
        s.setValue(QStringLiteral("facturas/ultimoPerfil"), QStringLiteral("0f000000-0000-4000-8000-0000000000ee"));
        s.sync();
    }
    QVERIFY(d.arrancar());
    QTRY_COMPARE(d.facturas()->perfiles()->count(), 2);
    QTRY_COMPARE(d.facturas()->perfilRfc(), QStringLiteral("XAXX010101000"));
    // El archivo vive en el directorio de datos (no en las preferencias del sistema).
    QVERIFY(QFile::exists(d.ini()));
}

int ejecutarTestFacturasPreferencias(int argc, char* argv[])
{
    TestFacturasPreferencias prueba;
    return QTest::qExec(&prueba, argc, argv);
}

#include "TestFacturasPreferencias.moc"
