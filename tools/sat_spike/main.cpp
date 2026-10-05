// satcfdi_sat_spike: CLI manual del spike SAT (T006). Compila solo con
// -DSATCFDI_BUILD_SAT_SPIKE=ON; fuera del bundle y no enlazada por la app.
// Ver SpikeCli.h y docs/development.md.
//
// SATCFDI_SPIKE_CONTRASENA_STDIN_PRUEBA solo la define el target de pruebas
// satcfdi_sat_spike_prueba: permite leer la contrasena de stdin sin TTY. El
// binario real exige una terminal interactiva y no tiene ninguna opcion para
// evitarlo.

#include "SpikeCli.h"

#include "infrastructure/crypto/EFirmaOpenSsl.h"
#include "infrastructure/sat/ClienteHttpSat.h"

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#include <csignal>
#include <cstdio>
#include <cstring>

#include <sys/resource.h>
#include <termios.h>
#include <unistd.h>

#ifndef SATCFDI_SOURCE_DIR
#error "SATCFDI_SOURCE_DIR debe definirse en CMake"
#endif

namespace {

constexpr int kSenalesTerminal[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP};

termios gTerminalOriginal{};
volatile std::sig_atomic_t gEcoApagado = 0; // la terminal esta sin eco por nosotros
volatile std::sig_atomic_t gLeyendo = 0;    // dentro del prompt

bool apagarEco()
{
    termios sinEco = gTerminalOriginal;
    sinEco.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &sinEco) != 0) {
        return false;
    }
    gEcoApagado = 1;
    termios efectiva{};
    return ::tcgetattr(STDIN_FILENO, &efectiva) == 0 && (efectiva.c_lflag & ECHO) == 0;
}

void restaurarTerminal()
{
    if (gEcoApagado != 0) {
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &gTerminalOriginal);
        gEcoApagado = 0;
    }
}

extern "C" void alContinuar(int)
{
    // Tras Ctrl-Z + fg en mitad del prompt: volver a apagar el eco.
    if (gLeyendo != 0) {
        apagarEco();
    }
}

extern "C" void alSenal(int senal)
{
    restaurarTerminal();
    struct sigaction porDefecto {};
    porDefecto.sa_handler = SIG_DFL;
    sigemptyset(&porDefecto.sa_mask);
    ::sigaction(senal, &porDefecto, nullptr);
    ::raise(senal);
    if (senal == SIGTSTP) {
        // Reanudado: reinstalar el manejador (el eco lo restituye alContinuar).
        struct sigaction propia {};
        propia.sa_handler = alSenal;
        sigemptyset(&propia.sa_mask);
        ::sigaction(SIGTSTP, &propia, nullptr);
    }
}

void instalarManejadores()
{
    struct sigaction sa {};
    sa.sa_handler = alSenal;
    sigemptyset(&sa.sa_mask);
    for (int s : kSenalesTerminal) {
        ::sigaction(s, &sa, nullptr);
    }
    struct sigaction cont {};
    cont.sa_handler = alContinuar;
    sigemptyset(&cont.sa_mask);
    ::sigaction(SIGCONT, &cont, nullptr);
}

// Lee una linea de stdin en `buffer` sin el salto. false = EOF.
bool leerLineaCruda(char* buffer, std::size_t tamano, std::size_t& largo)
{
    if (std::fgets(buffer, static_cast<int>(tamano), stdin) == nullptr) {
        return false;
    }
    largo = std::strlen(buffer);
    while (largo > 0 && (buffer[largo - 1] == '\n' || buffer[largo - 1] == '\r')) {
        buffer[--largo] = '\0';
    }
    return true;
}

// Contrasena por prompt sin eco (termios, verificado). Nunca argv ni entorno.
std::optional<satcfdi::BufferSecreto> leerContrasena()
{
    const bool tty = ::isatty(STDIN_FILENO) != 0;
#ifndef SATCFDI_SPIKE_CONTRASENA_STDIN_PRUEBA
    if (!tty) {
        std::fputs("error: la contrasena requiere una terminal interactiva\n", stderr);
        return std::nullopt;
    }
#endif
    if (tty) {
        if (::tcgetattr(STDIN_FILENO, &gTerminalOriginal) != 0) {
            std::fputs("error: no se pudo leer la configuracion de la terminal; no se pide la contrasena\n", stderr);
            return std::nullopt;
        }
        gLeyendo = 1;
        if (!apagarEco()) {
            gLeyendo = 0;
            restaurarTerminal();
            std::fputs("error: no se pudo desactivar el eco de la terminal; no se pide la contrasena\n", stderr);
            return std::nullopt;
        }
    }
    std::fputs("Contrasena de la e.firma (sin eco): ", stderr);
    std::fflush(stderr);
    char buffer[satcfdi::crypto::kMaxContrasena + 2];
    std::size_t largo = 0;
    const bool leida = leerLineaCruda(buffer, sizeof buffer, largo);
    gLeyendo = 0;
    bool terminalRestaurada = true;
    if (tty) {
        restaurarTerminal();
        termios efectiva{};
        terminalRestaurada = ::tcgetattr(STDIN_FILENO, &efectiva) == 0
                             && (efectiva.c_lflag & ECHO) == (gTerminalOriginal.c_lflag & ECHO);
    }
    std::fputs("\n", stderr);
    if (!terminalRestaurada) {
        // Falla cerrado: no se usa la contrasena y la CLI termina con error.
        std::fputs("error: no se pudo confirmar que el eco de la terminal se restauro; ejecute `stty echo` "
                   "antes de seguir usando esta terminal\n",
                   stderr);
        satcfdi::secretos::limpiarMemoria(buffer, sizeof buffer);
        return std::nullopt;
    }
    std::optional<satcfdi::BufferSecreto> r;
    if (leida && largo > 0) {
        r = satcfdi::BufferSecreto::desdeBytes(buffer, largo);
    }
    satcfdi::secretos::limpiarMemoria(buffer, sizeof buffer);
    return r;
}

std::optional<QString> leerLinea()
{
    char buffer[512];
    std::size_t largo = 0;
    if (!leerLineaCruda(buffer, sizeof buffer, largo)) {
        return std::nullopt;
    }
    return QString::fromUtf8(buffer, static_cast<qsizetype>(largo));
}

} // namespace

int main(int argc, char* argv[])
{
    // Sin core dumps: el proceso tendra la llave descifrada en memoria. Si no
    // se puede garantizar, no se continua.
    const rlimit sinCore{0, 0};
    rlimit efectivo{};
    if (::setrlimit(RLIMIT_CORE, &sinCore) != 0 || ::getrlimit(RLIMIT_CORE, &efectivo) != 0 || efectivo.rlim_cur != 0
        || efectivo.rlim_max != 0) {
        std::fputs("error: no se pudo desactivar los core dumps (RLIMIT_CORE=0); se aborta\n", stderr);
        return satcfdi::spike::kSalidaUso;
    }
    instalarManejadores();

    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Adenium"));
    QCoreApplication::setApplicationName(QStringLiteral("SAT CFDI Downloader"));

    satcfdi::spike::DependenciasSpike deps;
    deps.leerContrasena = &leerContrasena;
    deps.leerLinea = &leerLinea;
    deps.imprimir = [](const QString& t) {
        std::fputs(t.toUtf8().constData(), stdout);
        std::fputs("\n", stdout);
        std::fflush(stdout);
    };
    deps.imprimirError = [](const QString& t) {
        std::fputs(t.toUtf8().constData(), stderr);
        std::fputs("\n", stderr);
    };
    deps.crearCliente = [] { return std::make_unique<satcfdi::sat::ClienteHttpSat>(); };
    deps.relojUtc = [] { return QDateTime::currentDateTimeUtc(); };
    deps.directorioSalidaPorDefecto =
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("sat-spike"));
    deps.raizRepositorio = QStringLiteral(SATCFDI_SOURCE_DIR);
    return satcfdi::spike::ejecutarSpike(QCoreApplication::arguments(), deps);
}
