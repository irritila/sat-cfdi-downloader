#include "HistorialViewModel.h"

#include "FormatoFechas.h"

#include "domain/cfdi/MesCalendario.h"

#include <array>
#include <utility>

namespace satcfdi {

namespace {

const std::array<const char*, 12> kMeses = {"Enero", "Febrero", "Marzo", "Abril", "Mayo", "Junio",
                                            "Julio", "Agosto", "Septiembre", "Octubre", "Noviembre", "Diciembre"};

QString tituloMes(MesCalendario m)
{
    return QStringLiteral("%1 %2").arg(QString::fromUtf8(kMeses[std::size_t(m.mes() - 1)])).arg(m.anio());
}

// "4 oct 2026, 09:12" en hora Centro (D19), independiente de la zona del equipo.
QString fechaCentro(const QDateTime& instante)
{
    if (!instante.isValid()) {
        return {};
    }
    const QDateTime pared = centro::aParedCentro(instante);
    return FormatoFechas::fecha(pared.date()) + QStringLiteral(", ") + pared.time().toString(u"HH:mm");
}

struct Aspecto {
    const char* texto;
    const char* tono;
    const char* icono;
};

Aspecto aspecto(ResultadoHistorial r)
{
    switch (r) {
    case ResultadoHistorial::Completa: return {"Completa", "exito", "check-circle"};
    case ResultadoHistorial::EnPreparacion: return {"En preparación", "progreso", "clock"};
    case ResultadoHistorial::RequiereAtencion: return {"Requiere atención", "error", "exclamation-triangle"};
    case ResultadoHistorial::SinFacturas: return {"Sin facturas en el SAT", "neutro", "tray"};
    case ResultadoHistorial::NoCompletada: return {"Cubierta por otra descarga", "neutro", "circle-dashed"};
    case ResultadoHistorial::Eliminada: return {"Eliminada", "neutro", "trash"};
    case ResultadoHistorial::Anterior: return {"Descarga anterior", "neutro", "clock-xmark"};
    }
    return {"Completa", "exito", "check-circle"};
}

QString conteos(const EntradaHistorial& e)
{
    if (e.resultado == ResultadoHistorial::SinFacturas) {
        return QStringLiteral("El SAT no tenía facturas de este mes");
    }
    if (e.resultado == ResultadoHistorial::Eliminada) {
        return {};
    }
    if (e.tipo == EntradaHistorial::Tipo::Anterior || e.tieneEmitidas != e.tieneRecibidas) {
        return e.tieneEmitidas ? QStringLiteral("Solo emitidas: %1").arg(e.emitidas)
                               : QStringLiteral("Solo recibidas: %1").arg(e.recibidas);
    }
    return QStringLiteral("%1 emitidas, %2 recibidas").arg(e.emitidas).arg(e.recibidas);
}

QString detalle(const EntradaHistorial& e)
{
    if (e.tipo == EntradaHistorial::Tipo::Anterior) {
        return e.conFiltros ? QStringLiteral("Hecha con una versión anterior, con filtros")
                            : QStringLiteral("Hecha con una versión anterior");
    }
    if (e.resultado == ResultadoHistorial::Eliminada) {
        return QStringLiteral("Esta descarga se eliminó de este equipo");
    }
    if (e.eliminacionParcial) {
        return QStringLiteral("Parte de esta descarga se eliminó");
    }
    if (e.resultado == ResultadoHistorial::NoCompletada) {
        return QStringLiteral("Una descarga posterior obtuvo estas facturas");
    }
    return {};
}

} // namespace

HistorialViewModel::HistorialViewModel(QObject* parent)
    : QObject(parent)
    , m_modelo(new HistorialListModel(this))
{
}

HistorialViewModel::~HistorialViewModel() = default;

void HistorialViewModel::setFuente(FuenteHistorial* fuente)
{
    if (m_fuente) {
        disconnect(m_fuente, nullptr, this, nullptr);
    }
    m_fuente = fuente;
    if (fuente) {
        connect(fuente, &FuenteHistorial::datosCambiados, this, [this](const PerfilId& perfil) {
            if (m_activo && !m_perfil.esNulo() && (perfil.esNulo() || perfil == m_perfil)) {
                consultar(true);
            }
        });
    }
}

QString HistorialViewModel::subtitulo() const
{
    if (m_perfil.esNulo()) {
        return {};
    }
    return m_perfilNombre.isEmpty() ? m_perfilRfc : m_perfilRfc + QStringLiteral(" — ") + m_perfilNombre;
}

void HistorialViewModel::mostrarPerfil(const QString& perfilId, const QString& rfc, const QString& nombre)
{
    const PerfilId nuevo = PerfilId::desdeTexto(perfilId).value_or(PerfilId());
    const bool cambio = nuevo != m_perfil;
    m_perfil = nuevo;
    m_perfilRfc = m_perfil.esNulo() ? QString() : rfc;
    m_perfilNombre = m_perfil.esNulo() ? QString() : nombre;
    emit perfilChanged();
    if (cambio) {
        ++m_generacion;
        m_acciones.clear();
        m_modelo->reemplazar({});
        if (m_perfil.esNulo()) {
            setVista(QStringLiteral("sinPerfil"), QStringLiteral("Agrega un perfil para ver sus descargas"), false);
        }
    }
    if (m_activo) {
        consultar(!cambio);
    }
}

void HistorialViewModel::activar()
{
    m_activo = true;
    consultar(m_modelo->count() > 0);
}

void HistorialViewModel::desactivar()
{
    m_activo = false;
}

void HistorialViewModel::recargar()
{
    consultar(m_modelo->count() > 0);
}

void HistorialViewModel::consultar(bool conservar)
{
    if (m_perfil.esNulo()) {
        setVista(QStringLiteral("sinPerfil"), QStringLiteral("Agrega un perfil para ver sus descargas"), false);
        return;
    }
    if (!m_fuente) {
        setVista(QStringLiteral("vacio"), QStringLiteral("Aún no hay descargas de este perfil"), false);
        return;
    }
    const quint64 gen = ++m_generacion;
    if (conservar && m_vista == QStringLiteral("lista")) {
        setVista(m_vista, m_textoVista, true);
    } else {
        setVista(QStringLiteral("cargando"), QString(), false);
    }
    m_fuente->historial(m_perfil).then(this, [this, gen](const FuenteHistorial::ResultadoEntradas& r) {
        if (gen == m_generacion) {
            aplicar(r);
        }
    });
}

void HistorialViewModel::aplicar(const FuenteHistorial::ResultadoEntradas& resultado)
{
    if (!resultado) {
        if (m_modelo->count() > 0) {
            setVista(QStringLiteral("lista"), QString(), false); // conserva la lista anterior
        } else {
            setVista(QStringLiteral("error"), QStringLiteral("No se pudo leer el historial"), false);
        }
        return;
    }
    QList<FilaHistorial> filas;
    QHash<QString, Accion> acciones;
    for (const EntradaHistorial& e : resultado.valor()) {
        FilaHistorial f;
        const bool anterior = e.tipo == EntradaHistorial::Tipo::Anterior;
        const auto id = anterior ? (e.diagnosticoEmitidas ? e.diagnosticoEmitidas : e.diagnosticoRecibidas) : std::nullopt;
        f.clave = anterior ? QStringLiteral("a:") + (id ? id->texto() : QString()) : QStringLiteral("l:") + e.loteId;
        if (e.mes) {
            f.anio = e.mes->anio();
            f.mes = e.mes->mes();
            f.titulo = tituloMes(*e.mes);
        } else {
            f.titulo = FormatoFechas::fecha(e.desde) + QStringLiteral(" – ") + FormatoFechas::fecha(e.hasta);
        }
        f.fecha = fechaCentro(e.creadoEn);
        f.textoFecha = QStringLiteral("Descarga del %1").arg(f.fecha);
        f.resultado = QString(claveEstable(e.resultado));
        const Aspecto a = aspecto(e.resultado);
        f.resultadoTexto = QString::fromUtf8(a.texto);
        f.tono = QString::fromUtf8(a.tono);
        f.icono = QString::fromUtf8(a.icono);
        f.textoConteos = conteos(e);
        f.detalle = detalle(e);
        f.eliminacionParcial = e.eliminacionParcial;
        f.anterior = anterior;
        f.puedeVerFacturas = !anterior && e.resultado != ResultadoHistorial::Eliminada && e.mes.has_value();
        f.puedeReintentar = !anterior && e.puedeReintentar && e.mes.has_value();
        // Eliminada: "Ver detalles tecnicos" abre la lista de Diagnostico.
        f.puedeVerDetalles = e.diagnosticoEmitidas || e.diagnosticoRecibidas
                             || e.resultado == ResultadoHistorial::Eliminada;
        f.eligeDireccion = e.diagnosticoEmitidas.has_value() && e.diagnosticoRecibidas.has_value();
        QStringList partes{f.titulo, f.resultadoTexto, QStringLiteral("descarga del %1").arg(f.fecha)};
        for (const QString& t : {f.textoConteos, f.detalle}) {
            if (!t.isEmpty()) {
                partes.append(t);
            }
        }
        f.nombreAccesible = partes.join(QStringLiteral(", "));
        acciones.insert(f.clave, Accion{e.mes, e.diagnosticoEmitidas, e.diagnosticoRecibidas});
        filas.append(std::move(f));
    }
    m_acciones = std::move(acciones);
    const int total = int(filas.size());
    m_modelo->reemplazar(std::move(filas));
    if (total == 0) {
        setVista(QStringLiteral("vacio"), QStringLiteral("Aún no hay descargas de este perfil"), false);
    } else {
        setVista(QStringLiteral("lista"), QString(), false);
    }
    const QString anuncio = total == 1 ? QStringLiteral("1 descarga") : QStringLiteral("%1 descargas").arg(total);
    if (anuncio != m_anuncio) {
        m_anuncio = anuncio;
        emit anuncioChanged();
    }
}

bool HistorialViewModel::verFacturas(int fila)
{
    const FilaHistorial* f = m_modelo->filaEn(fila);
    if (!f || !f->puedeVerFacturas) {
        return false;
    }
    emit facturasMesSolicitadas(m_perfil.texto(), f->anio, f->mes);
    return true;
}

bool HistorialViewModel::reintentar(int fila)
{
    const FilaHistorial* f = m_modelo->filaEn(fila);
    if (!f || !f->puedeReintentar) {
        return false;
    }
    emit reintentoSolicitado(m_perfil.texto(), f->anio, f->mes);
    return true;
}

bool HistorialViewModel::verDetallesTecnicos(int fila, Direccion direccion)
{
    const FilaHistorial* f = m_modelo->filaEn(fila);
    if (!f || !f->puedeVerDetalles) {
        return false;
    }
    const Accion a = m_acciones.value(f->clave);
    std::optional<SolicitudId> s;
    switch (direccion) {
    case Direccion::Emitidas: s = a.emitidas; break;
    case Direccion::Recibidas: s = a.recibidas; break;
    case Direccion::Ninguna:
        if (a.emitidas && a.recibidas) {
            return false; // hay que elegir
        }
        s = a.emitidas ? a.emitidas : a.recibidas;
        break;
    }
    if (!s && (a.emitidas || a.recibidas)) {
        return false; // direccion sin diagnostico
    }
    emit diagnosticoSolicitado(s ? s->texto() : QString()); // "" -> lista de Diagnostico
    return true;
}

void HistorialViewModel::setVista(const QString& vista, const QString& texto, bool actualizando)
{
    if (m_vista == vista && m_textoVista == texto && m_actualizando == actualizando) {
        return;
    }
    m_vista = vista;
    m_textoVista = texto;
    m_actualizando = actualizando;
    emit vistaChanged();
}

} // namespace satcfdi
