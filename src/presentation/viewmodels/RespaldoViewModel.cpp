#include "RespaldoViewModel.h"

#include "FormatoFechas.h"

#include <QFileInfo>

#include <utility>

namespace satcfdi {

namespace {

using EstadoRespaldo = respaldo::ResultadoRespaldo::Estado;

double fraccion(quint64 bytes, quint64 total)
{
    if (total == 0) {
        return -1;
    }
    const double f = static_cast<double>(bytes) / static_cast<double>(total);
    return f < 0 ? 0 : (f > 1 ? 1 : f);
}

QString textoDesdeBuffer(const BufferSecreto& buffer)
{
    return QString::fromUtf8(reinterpret_cast<const char*>(buffer.datos()), static_cast<qsizetype>(buffer.tamano()));
}

// Caracteres (puntos de codigo), sin copiar el texto.
int caracteres(const QString& texto)
{
    int n = 0;
    for (const QChar c : texto) {
        if (!c.isLowSurrogate()) {
            ++n;
        }
    }
    return n;
}

std::shared_ptr<BufferSecreto> bufferDesde(const QString& texto)
{
    return std::make_shared<BufferSecreto>(BufferSecreto::desdeTexto(texto));
}

QString plural(int n, const QString& singular, const QString& varios)
{
    return (n == 1 ? singular : varios).arg(n);
}

// Id autenticado en hexadecimal, en grupos de 4 ("1A2B-3C4D-...").
QString formatoId(const QByteArray& id)
{
    const QString hex = QString::fromLatin1(id.toHex()).toUpper();
    QStringList grupos;
    for (qsizetype i = 0; i < hex.size(); i += 4) {
        grupos << hex.mid(i, 4);
    }
    return grupos.join(QLatin1Char('-'));
}

QString tamano(quint64 bytes)
{
    return QLocale(QLocale::Spanish, QLocale::Mexico)
        .formattedDataSize(static_cast<qint64>(bytes), 1, QLocale::DataSizeSIFormat);
}

} // namespace

RespaldoViewModel::RespaldoViewModel(QObject* parent)
    : QObject(parent)
{
}

RespaldoViewModel::~RespaldoViewModel() = default;

// ---- Puertos ----

void RespaldoViewModel::setControlRespaldo(ControlRespaldoPresentacion* control)
{
    if (m_respaldo) {
        disconnect(m_respaldo, nullptr, this, nullptr);
    }
    m_respaldo = control;
    if (control) {
        connect(control, &ControlRespaldoPresentacion::estadoCambiado, this, &RespaldoViewModel::leerEstado);
        connect(control, &ControlRespaldoPresentacion::progreso, this, &RespaldoViewModel::alProgresoRespaldo);
        connect(control, &ControlRespaldoPresentacion::respaldoTerminado, this,
                [this](const ResultadoRespaldoPresentacion& r) {
                    fijarResultado(r);
                    if (!respaldando()) {
                        reiniciarProgresoRespaldo();
                    }
                });
        if (auto ultimo = control->ultimoResultado()) {
            fijarResultado(*ultimo);
        }
    }
    leerEstado();
}

void RespaldoViewModel::setControlRestauracion(ControlRestauracionPresentacion* control)
{
    if (m_restauracion) {
        disconnect(m_restauracion, nullptr, this, nullptr);
    }
    m_restauracion = control;
    if (control) {
        connect(control, &ControlRestauracionPresentacion::progreso, this, &RespaldoViewModel::alProgresoRestauracion);
    }
    ++m_genRestauracion;
    m_resumen.reset();
    m_nombreArchivo.clear();
    m_errorRestauracion.clear();
    m_solicitud = {};
    m_estadoRestauracion = EstadoRestauracion::Inactiva;
    reiniciarProgresoRestauracion();
    emit restauracionChanged();
}

void RespaldoViewModel::setDatosPrevios(DatosPreviosRestauracionPresentacion* datos)
{
    m_datosPrevios = datos;
    ++m_genDatosPrevios;
    m_hayDatosPrevios = false;
    m_descartando = false;
    m_descarte = {};
    m_arranque = {};
    if (datos) {
        const ResultadoArranquePresentacion arranque = datos->restauracionAlArrancar();
        m_arranque = catalogo::mensajeArranque(arranque.estado, FormatoFechas::fechaHora(arranque.creadoRespaldoUtc));
    }
    emit avisosChanged();
    consultarDatosPrevios();
}

void RespaldoViewModel::setImpresion(ImpresionTexto impresion)
{
    m_impresion = std::move(impresion);
    emit configuracionChanged();
}

void RespaldoViewModel::setCopiaSecreta(CopiaTextoSecreto copia)
{
    m_copia = std::move(copia);
    emit configuracionChanged();
}

// ---- Configuracion ----

bool RespaldoViewModel::disponible() const
{
    return m_respaldo != nullptr && m_estado.disponible;
}

bool RespaldoViewModel::cargando() const
{
    return m_respaldo != nullptr && !m_estado.cargado;
}

QString RespaldoViewModel::textoPendienteDiario() const
{
    if (!m_estado.diarioActivado) {
        return {};
    }
    return catalogo::textoPendienteDiario(m_estado.pendienteDiario);
}

QString RespaldoViewModel::textoUltimoExito() const
{
    if (m_estado.ultimoExitoUtc) {
        return tr("Último respaldo correcto: %1").arg(FormatoFechas::fechaHora(*m_estado.ultimoExitoUtc));
    }
    return m_estado.cargado ? tr("Aún no hay respaldos.") : QString();
}

bool RespaldoViewModel::puedeRespaldar() const
{
    return disponible() && m_estado.cargado && m_estado.carpetaConfigurada && m_estado.contrasenaConfigurada
           && !respaldando() && !m_ocupado;
}

void RespaldoViewModel::leerEstado()
{
    m_estado = m_respaldo ? m_respaldo->estado() : EstadoRespaldoPresentacion{};
    emit configuracionChanged();
}

void RespaldoViewModel::activar()
{
    leerEstado();
    consultarDatosPrevios();
}

void RespaldoViewModel::fijarMensaje(const QString& variante, const QString& texto)
{
    m_varianteMensaje = texto.isEmpty() ? QString() : variante;
    m_mensaje = texto;
    emit mensajeChanged();
}

void RespaldoViewModel::fijarFormulario(bool procesando, const QString& error)
{
    m_procesandoFormulario = procesando;
    m_errorFormulario = error;
    emit formularioChanged();
}

void RespaldoViewModel::fijarOcupado(bool ocupado)
{
    if (m_ocupado != ocupado) {
        m_ocupado = ocupado;
        emit configuracionChanged();
    }
}

void RespaldoViewModel::limpiarFormulario()
{
    if (!m_procesandoFormulario) {
        fijarFormulario(false, {});
    }
}

void RespaldoViewModel::elegirCarpeta(const QUrl& carpeta)
{
    if (!disponible() || m_ocupado) {
        return;
    }
    if (!carpeta.isLocalFile()) {
        fijarMensaje(QStringLiteral("error"), tr("Elige una carpeta de este equipo."));
        return;
    }
    fijarMensaje({}, {});
    fijarOcupado(true);
    m_respaldo->configurarCarpeta(carpeta.toLocalFile())
        .then(this, [this](const ControlRespaldoPresentacion::ResultadoOperacion& r) {
            fijarOcupado(false);
            if (r) {
                fijarMensaje(QStringLiteral("exito"), tr("Carpeta del respaldo guardada."));
            } else {
                fijarMensaje(QStringLiteral("error"), catalogo::mensajeErrorGestor(r.error().categoria));
            }
            leerEstado();
        });
}

void RespaldoViewModel::crearContrasena(const QString& contrasena, const QString& confirmacion)
{
    if (!disponible() || m_procesandoFormulario) {
        return;
    }
    if (contrasena.isEmpty()) {
        fijarFormulario(false, tr("Escribe una contraseña."));
        return;
    }
    if (caracteres(contrasena) < kMinimoContrasena) {
        fijarFormulario(false, tr("La contraseña debe tener al menos %1 caracteres.").arg(kMinimoContrasena));
        return;
    }
    if (contrasena != confirmacion) {
        fijarFormulario(false, tr("Las contraseñas no coinciden."));
        return;
    }
    fijarFormulario(true, {});
    fijarOcupado(true);
    m_respaldo->configurarContrasena(bufferDesde(contrasena))
        .then(this, [this](const ControlRespaldoPresentacion::ResultadoClave& r) {
            fijarOcupado(false);
            if (!r) {
                fijarFormulario(false, catalogo::mensajeErrorGestor(r.error().categoria));
                leerEstado();
                return;
            }
            fijarFormulario(false, {});
            leerEstado();
            const std::shared_ptr<BufferSecreto>& clave = r.valor();
            if (clave && !clave->vacio()) {
                m_claveRecuperacion = std::make_unique<BufferSecreto>(std::move(*clave));
                m_olvidarAlImprimir = false;
                emit clavePendienteChanged();
                emit claveRecuperacionLista();
            }
        });
}

void RespaldoViewModel::cambiarContrasena(const QString& verificacion, Verificacion tipo, const QString& nueva,
                                          const QString& confirmacion)
{
    if (!disponible() || m_procesandoFormulario) {
        return;
    }
    if (verificacion.isEmpty()) {
        fijarFormulario(false, tipo == Verificacion::VerificarConClave
                                   ? tr("Escribe la clave de recuperación.")
                                   : tr("Escribe la contraseña anterior."));
        return;
    }
    if (nueva.isEmpty()) {
        fijarFormulario(false, tr("Escribe la contraseña nueva."));
        return;
    }
    if (caracteres(nueva) < kMinimoContrasena) {
        fijarFormulario(false,
                        tr("La contraseña nueva debe tener al menos %1 caracteres.").arg(kMinimoContrasena));
        return;
    }
    if (nueva != confirmacion) {
        fijarFormulario(false, tr("Las contraseñas nuevas no coinciden."));
        return;
    }
    respaldo::VerificacionCambio v;
    v.tipo = tipo == Verificacion::VerificarConClave ? respaldo::VerificacionCambio::Tipo::ClaveRecuperacion
                                                     : respaldo::VerificacionCambio::Tipo::ContrasenaAnterior;
    v.texto = bufferDesde(verificacion);
    fijarFormulario(true, {});
    fijarOcupado(true);
    m_respaldo->cambiarContrasena(bufferDesde(nueva), std::move(v))
        .then(this, [this](const ControlRespaldoPresentacion::ResultadoOperacion& r) {
            fijarOcupado(false);
            if (!r) {
                fijarFormulario(false, catalogo::mensajeErrorGestor(r.error().categoria));
                leerEstado();
                return;
            }
            fijarFormulario(false, {});
            fijarMensaje(QStringLiteral("exito"),
                         tr("Contraseña cambiada. Tu clave de recuperación sigue siendo la misma."));
            leerEstado();
            emit contrasenaCambiada();
        });
}

void RespaldoViewModel::activarDiario(bool activado)
{
    if (!disponible() || m_ocupado || activado == m_estado.diarioActivado) {
        return;
    }
    fijarOcupado(true);
    m_respaldo->activarDiario(activado).then(this, [this](const ControlRespaldoPresentacion::ResultadoOperacion& r) {
        fijarOcupado(false);
        if (r) {
            fijarMensaje({}, {});
        } else {
            fijarMensaje(QStringLiteral("error"), catalogo::mensajeErrorGestor(r.error().categoria));
        }
        leerEstado();
    });
}

// ---- Respaldo ----

void RespaldoViewModel::respaldarAhora()
{
    if (!puedeRespaldar()) {
        return;
    }
    fijarMensaje({}, {});
    m_respaldoManual = true;
    reiniciarProgresoRespaldo();
    emit configuracionChanged();
    m_respaldo->respaldarAhora().then(this, [this](const ResultadoRespaldoPresentacion& r) {
        m_respaldoManual = false;
        if (r.estado == EstadoRespaldo::YaEnCurso) {
            fijarMensaje(QStringLiteral("neutro"), catalogo::causaRespaldo(r));
        } else {
            fijarResultado(r);
        }
        leerEstado();
        if (!respaldando()) {
            reiniciarProgresoRespaldo();
        }
    });
}

void RespaldoViewModel::alProgresoRespaldo(respaldo::RespaldoService::Fase fase, quint64 bytes, quint64 total)
{
    m_textoFaseRespaldo = catalogo::textoFase(fase);
    m_fraccionRespaldo = fraccion(bytes, total);
    emit progresoRespaldoChanged();
}

void RespaldoViewModel::reiniciarProgresoRespaldo()
{
    m_textoFaseRespaldo.clear();
    m_fraccionRespaldo = -1;
    emit progresoRespaldoChanged();
}

void RespaldoViewModel::fijarResultado(const ResultadoRespaldoPresentacion& resultado)
{
    if (resultado.estado == EstadoRespaldo::YaEnCurso) {
        return; // no describe un respaldo terminado
    }
    if (m_resultado == resultado) {
        return;
    }
    m_resultado = resultado;
    emit resultadoChanged();
}

QString RespaldoViewModel::varianteResultado() const
{
    return m_resultado ? catalogo::mensajeRespaldo(*m_resultado).variante : QString();
}

QString RespaldoViewModel::tituloResultado() const
{
    return m_resultado ? catalogo::mensajeRespaldo(*m_resultado).titulo : QString();
}

QString RespaldoViewModel::descripcionResultado() const
{
    if (!m_resultado) {
        return {};
    }
    const QString fecha = FormatoFechas::fechaHora(m_resultado->fechaUtc);
    QString prefijo;
    if (!fecha.isEmpty()) {
        prefijo = m_resultado->exito() && m_resultado->bytes > 0
                      ? tr("%1 · %2.").arg(fecha, tamano(m_resultado->bytes))
                      : tr("%1.").arg(fecha);
    }
    const QString texto = catalogo::mensajeRespaldo(*m_resultado).descripcion;
    return prefijo.isEmpty() ? texto : prefijo + QLatin1Char(' ') + texto;
}

QStringList RespaldoViewModel::advertenciasResultado() const
{
    return m_resultado ? catalogo::advertenciasRespaldo(*m_resultado) : QStringList();
}

QString RespaldoViewModel::avisoCopiasSinRegistro() const
{
    return m_resultado ? catalogo::avisoCopiasSinRegistro(m_resultado->copiasSinRegistro) : QString();
}

// ---- Clave de recuperacion ----

QString RespaldoViewModel::tomarClaveRecuperacion() const
{
    return m_claveRecuperacion ? textoDesdeBuffer(*m_claveRecuperacion) : QString();
}

bool RespaldoViewModel::copiarClaveRecuperacion()
{
    if (m_imprimiendo || !m_copia || !m_claveRecuperacion || m_claveRecuperacion->vacio()) {
        return false;
    }
    const CopiaTextoSecreto copia = m_copia;
    return copia(textoDesdeBuffer(*m_claveRecuperacion));
}

bool RespaldoViewModel::imprimirClaveRecuperacion()
{
    if (m_imprimiendo || !m_impresion || !m_claveRecuperacion || m_claveRecuperacion->vacio()) {
        return false;
    }
    const QString texto = textoDesdeBuffer(*m_claveRecuperacion) + QStringLiteral("\n\n")
                          + tr("Con esta clave puedes abrir tus respaldos de Archivo Fiscal si olvidas la "
                               "contraseña. Guárdala en un lugar seguro.");
    // Sincrono y app-modal: el bucle de eventos sigue vivo (reentrada).
    const ImpresionTexto impresion = m_impresion; // por si se reemplaza durante el panel
    m_imprimiendo = true;
    emit configuracionChanged();
    const bool enviado = impresion(tr("Clave de recuperación del respaldo"), texto);
    m_imprimiendo = false;
    emit configuracionChanged();
    if (m_olvidarAlImprimir) {
        m_olvidarAlImprimir = false;
        m_claveRecuperacion.reset();
        emit clavePendienteChanged();
    }
    return enviado;
}

void RespaldoViewModel::olvidarClaveRecuperacion()
{
    if (m_imprimiendo) {
        m_olvidarAlImprimir = true;
        return;
    }
    if (m_claveRecuperacion) {
        m_claveRecuperacion.reset(); // ~BufferSecreto limpia la memoria
        emit clavePendienteChanged();
    }
}

// ---- Restauracion ----

void RespaldoViewModel::fijarEstadoRestauracion(EstadoRestauracion estado)
{
    m_estadoRestauracion = estado;
    emit restauracionChanged();
}

void RespaldoViewModel::prepararRestauracion(const QUrl& archivo, const QString& secreto, Credencial credencial)
{
    if (!m_restauracion || m_estadoRestauracion != EstadoRestauracion::Inactiva) {
        return;
    }
    m_solicitud = {};
    if (!archivo.isLocalFile()) {
        m_errorRestauracion = tr("Elige un archivo de respaldo de este equipo.");
        emit restauracionChanged();
        return;
    }
    const QString ruta = archivo.toLocalFile();
    if (!ruta.endsWith(QStringLiteral(".afbak"), Qt::CaseInsensitive)) {
        m_errorRestauracion = tr("Elige un archivo de respaldo de Archivo Fiscal (.afbak).");
        emit restauracionChanged();
        return;
    }
    if (secreto.isEmpty()) {
        m_errorRestauracion = tr("Escribe la contraseña o la clave de recuperación del respaldo.");
        emit restauracionChanged();
        return;
    }
    respaldo::PeticionRestauracion::Credencial c = respaldo::PeticionRestauracion::Credencial::Automatica;
    if (credencial == Credencial::CredencialContrasena) {
        c = respaldo::PeticionRestauracion::Credencial::Contrasena;
    } else if (credencial == Credencial::CredencialClave) {
        c = respaldo::PeticionRestauracion::Credencial::ClaveRecuperacion;
    }
    const quint64 gen = ++m_genRestauracion;
    m_errorRestauracion.clear();
    m_resumen.reset();
    m_nombreArchivo = QFileInfo(ruta).fileName();
    reiniciarProgresoRestauracion();
    fijarEstadoRestauracion(EstadoRestauracion::Preparando);
    m_restauracion->preparar(ruta, c, bufferDesde(secreto))
        .then(this, [this, gen](const ControlRestauracionPresentacion::ResultadoPreparacion& r) {
            if (gen != m_genRestauracion) {
                return;
            }
            reiniciarProgresoRestauracion();
            if (r) {
                m_resumen = r.valor();
                fijarEstadoRestauracion(EstadoRestauracion::Preparada);
                return;
            }
            // Cancelar es una decision del usuario: sin mensaje de error.
            m_errorRestauracion = r.error().categoria == respaldo::ErrorRestauracion::Categoria::Cancelado
                                      ? QString()
                                      : catalogo::mensajeErrorRestauracion(r.error());
            m_nombreArchivo.clear();
            fijarEstadoRestauracion(EstadoRestauracion::Inactiva);
        });
}

QUrl RespaldoViewModel::carpetaInicialRestauracion() const
{
    return m_respaldo ? m_respaldo->urlCarpeta() : QUrl();
}

void RespaldoViewModel::cancelarRestauracion()
{
    if (m_restauracion && m_estadoRestauracion == EstadoRestauracion::Preparando) {
        m_restauracion->cancelar(); // resuelve preparar() con Cancelado
    }
}

void RespaldoViewModel::descartarRestauracion()
{
    if (!m_restauracion || m_estadoRestauracion != EstadoRestauracion::Preparada) {
        return;
    }
    ++m_genRestauracion;
    m_restauracion->descartar();
    m_resumen.reset();
    m_nombreArchivo.clear();
    m_errorRestauracion.clear();
    fijarEstadoRestauracion(EstadoRestauracion::Inactiva);
}

void RespaldoViewModel::confirmarRestauracion()
{
    if (!m_restauracion || m_estadoRestauracion != EstadoRestauracion::Preparada) {
        return;
    }
    const quint64 gen = ++m_genRestauracion;
    m_errorRestauracion.clear();
    m_solicitud = {};
    fijarEstadoRestauracion(EstadoRestauracion::Solicitando);
    m_restauracion->solicitar().then(this, [this, gen](const respaldo::ResultadoSolicitudRestauracion& r) {
        if (gen != m_genRestauracion) {
            return;
        }
        using E = respaldo::ResultadoSolicitudRestauracion::Estado;
        switch (r.estado) {
        case E::Relanzando:
            m_solicitud = catalogo::mensajeSolicitudRestauracion(r);
            fijarEstadoRestauracion(EstadoRestauracion::Relanzando);
            return;
        case E::SinRelanzamiento:
            m_solicitud = catalogo::mensajeSolicitudRestauracion(r);
            fijarEstadoRestauracion(EstadoRestauracion::SinRelanzamiento);
            return;
        case E::FalloTrasDetener:
            m_solicitud = catalogo::mensajeSolicitudRestauracion(r);
            fijarEstadoRestauracion(EstadoRestauracion::FalloTrasDetener);
            return;
        case E::Rechazada:
            break;
        }
        m_errorRestauracion = catalogo::mensajeSolicitudRestauracion(r).descripcion;
        if (m_restauracion && m_restauracion->preparada()) {
            fijarEstadoRestauracion(EstadoRestauracion::Preparada);
        } else {
            m_resumen.reset();
            m_nombreArchivo.clear();
            fijarEstadoRestauracion(EstadoRestauracion::Inactiva);
        }
    });
}

void RespaldoViewModel::alProgresoRestauracion(respaldo::RestauracionService::Fase fase, quint64 bytes, quint64 total)
{
    if (m_estadoRestauracion != EstadoRestauracion::Preparando) {
        return;
    }
    m_textoFaseRestauracion = catalogo::textoFase(fase);
    m_fraccionRestauracion = fraccion(bytes, total);
    emit progresoRestauracionChanged();
}

void RespaldoViewModel::reiniciarProgresoRestauracion()
{
    m_textoFaseRestauracion.clear();
    m_fraccionRestauracion = -1;
    emit progresoRestauracionChanged();
}

QString RespaldoViewModel::fechaRespaldoRestauracion() const
{
    return m_resumen ? FormatoFechas::fechaHora(m_resumen->creadoEnUtc) : QString();
}

QString RespaldoViewModel::idRespaldoRestauracion() const
{
    return m_resumen ? formatoId(m_resumen->idRespaldo) : QString();
}

QString RespaldoViewModel::versionAppRestauracion() const
{
    if (!m_resumen || m_resumen->versionAppRespaldo.isEmpty()) {
        return {};
    }
    return tr("Creado con Archivo Fiscal %1").arg(m_resumen->versionAppRespaldo);
}

QString RespaldoViewModel::conteosRestauracion() const
{
    if (!m_resumen) {
        return {};
    }
    QString texto = tr("%1, %2 y %3.")
                        .arg(plural(m_resumen->perfiles, tr("%1 perfil"), tr("%1 perfiles")),
                             plural(m_resumen->solicitudes, tr("%1 descarga"), tr("%1 descargas")),
                             plural(m_resumen->paquetes, tr("%1 archivo del SAT"), tr("%1 archivos del SAT")));
    if (m_resumen->faltantes > 0) {
        texto += QLatin1Char(' ')
                 + plural(m_resumen->faltantes, tr("%1 archivo del SAT ya faltaba cuando se hizo el respaldo."),
                          tr("%1 archivos del SAT ya faltaban cuando se hizo el respaldo."));
    }
    return texto;
}

QString RespaldoViewModel::avisoMigracionRestauracion() const
{
    if (!m_resumen || !m_resumen->migrada) {
        return {};
    }
    return tr("El respaldo es de una versión anterior de Archivo Fiscal y se actualizará al restaurarlo.");
}

// ---- Datos previos y arranque ----

QString RespaldoViewModel::tituloDatosPrevios() const
{
    return m_hayDatosPrevios ? catalogo::avisoDatosPrevios().titulo : QString();
}

QString RespaldoViewModel::descripcionDatosPrevios() const
{
    return m_hayDatosPrevios ? catalogo::avisoDatosPrevios().descripcion : QString();
}

void RespaldoViewModel::consultarDatosPrevios()
{
    if (!m_datosPrevios || m_descartando) {
        return;
    }
    const quint64 gen = ++m_genDatosPrevios;
    m_datosPrevios->hayDatosPrevios().then(this, [this, gen](bool hay) {
        if (gen != m_genDatosPrevios || hay == m_hayDatosPrevios) {
            return;
        }
        m_hayDatosPrevios = hay;
        emit avisosChanged();
    });
}

void RespaldoViewModel::descartarDatosPrevios()
{
    if (!m_datosPrevios || m_descartando || !m_hayDatosPrevios) {
        return;
    }
    const quint64 gen = ++m_genDatosPrevios;
    m_descartando = true;
    m_descarte = {};
    emit avisosChanged();
    m_datosPrevios->descartarDatosPrevios().then(this, [this, gen](const ResultadoDescartePresentacion& r) {
        if (gen != m_genDatosPrevios) {
            return;
        }
        m_descartando = false;
        m_descarte = catalogo::mensajeDescarte(r);
        emit avisosChanged();
        consultarDatosPrevios();
    });
}

void RespaldoViewModel::cerrarAvisoArranque()
{
    if (avisoArranqueActivo()) {
        m_arranque = {};
        emit avisosChanged();
    }
}

} // namespace satcfdi
