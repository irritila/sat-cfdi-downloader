#pragma once

// Fixture de Qt Quick Test para RespaldoPage, sus dialogos y
// FranjaAvisosRespaldo (T025.6): RespaldoViewModel sobre los fakes de
// RespaldoFakes.h. Context property "fixtureRespaldo". Datos sinteticos.

#include "presentation/RespaldoFakes.h"
#include "RespaldoViewModel.h"

#include <QObject>
#include <QPointer>

class FixtureRespaldo : public QObject {
    Q_OBJECT
    Q_PROPERTY(satcfdi::RespaldoViewModel* respaldo READ respaldo NOTIFY respaldoChanged)
    Q_PROPERTY(int impresiones READ impresiones NOTIFY respaldoChanged)
    Q_PROPERTY(int copias READ copias NOTIFY respaldoChanged)

public:
    explicit FixtureRespaldo(QObject* parent = nullptr)
        : QObject(parent)
    {
        reiniciar(0, false, false);
    }

    satcfdi::RespaldoViewModel* respaldo() const { return m_vm; }
    int impresiones() const { return m_impresiones; }
    int copias() const { return m_copias; }

    // arranque: ResultadoArranquePresentacion::Estado (0 = Ninguno).
    // configurado: carpeta y contrasena listas. conImpresion: con "Imprimir…".
    Q_INVOKABLE void reiniciar(int arranque, bool configurado, bool conImpresion)
    {
        delete m_vm;
        m_respaldo = std::make_unique<fakes::FakeControlRespaldo>();
        m_restauracion = std::make_unique<fakes::FakeControlRestauracion>();
        m_datos = std::make_unique<fakes::FakeDatosPrevios>();
        m_impresiones = 0;
        m_copias = 0;
        m_respaldo->estadoActual.cargado = true;
        if (configurado) {
            m_respaldo->estadoActual = fakes::FakeControlRespaldo::listo();
            m_respaldo->url = QUrl::fromLocalFile(QStringLiteral("/Users/usuario/Respaldos"));
        }
        m_datos->arranque.estado = static_cast<satcfdi::ResultadoArranquePresentacion::Estado>(arranque);
        m_datos->arranque.creadoRespaldoUtc = QDateTime(QDate(2026, 10, 1), QTime(16, 30), QTimeZone::UTC);
        m_vm = new satcfdi::RespaldoViewModel(this);
        m_vm->setControlRespaldo(m_respaldo.get());
        m_vm->setControlRestauracion(m_restauracion.get());
        m_vm->setDatosPrevios(m_datos.get());
        // H1: copia secreta simulada (cuenta, no toca el portapapeles).
        m_vm->setCopiaSecreta([this](const QString&) {
            ++m_copias;
            emit respaldoChanged();
            return true;
        });
        if (conImpresion) {
            m_vm->setImpresion([this](const QString&, const QString&) {
                ++m_impresiones;
                emit respaldoChanged();
                return true;
            });
        }
        emit respaldoChanged();
    }

    // Respuesta pendiente de "hay datos previos".
    Q_INVOKABLE void responderDatosPrevios(bool hay)
    {
        if (m_datos->consultas.size() > 0) {
            m_datos->consultas.resolverUltima(hay);
        }
    }
    Q_INVOKABLE int consultasDatosPrevios() const { return m_datos->consultas.size(); }
    Q_INVOKABLE int descartesDatosPrevios() const { return m_datos->descartes.size(); }

    // Crear contrasena: devuelve una clave sintetica.
    Q_INVOKABLE void responderContrasena()
    {
        m_respaldo->estadoActual.contrasenaConfigurada = true;
        m_respaldo->contrasena.resolverUltima(fakes::FakeControlRespaldo::ResultadoClave::exito(
            std::make_shared<satcfdi::BufferSecreto>(satcfdi::BufferSecreto::desdeTexto(u"ABCD-EFGH-JKMN-PQRS"))));
    }
    Q_INVOKABLE int llamadasContrasena() const { return m_respaldo->contrasena.size(); }
    Q_INVOKABLE bool ultimaContrasenaEs(const QString& texto) const
    {
        return fakes::mismoSecreto(m_respaldo->ultimaContrasena, texto.toUtf8().constData());
    }

    Q_INVOKABLE int respaldos() const { return m_respaldo->respaldos.size(); }
    Q_INVOKABLE void progresoRespaldo(int bytes, int total)
    {
        m_respaldo->emitirProgreso(satcfdi::respaldo::RespaldoService::Fase::Escritura, quint64(bytes),
                                   quint64(total));
    }
    Q_INVOKABLE void terminarRespaldoConCopiasSinRegistro(int copias)
    {
        satcfdi::ResultadoRespaldoPresentacion r;
        r.estado = satcfdi::respaldo::ResultadoRespaldo::Estado::ExitoConAdvertencias;
        r.fechaUtc = QDateTime(QDate(2026, 10, 21), QTime(15, 0), QTimeZone::UTC);
        r.copiasSinRegistro = copias;
        m_respaldo->respaldos.resolverUltima(r);
    }

    Q_INVOKABLE int preparaciones() const { return m_restauracion->preparaciones.size(); }
    Q_INVOKABLE bool ultimoSecretoEs(const QString& texto) const
    {
        return fakes::mismoSecreto(m_restauracion->ultimoSecreto, texto.toUtf8().constData());
    }
    Q_INVOKABLE void responderPreparacion()
    {
        m_restauracion->estaPreparada = true;
        m_restauracion->preparaciones.resolverUltima(
            fakes::FakeControlRestauracion::ResultadoPreparacion::exito(fakes::FakeControlRestauracion::resumen()));
    }
    Q_INVOKABLE int cancelaciones() const { return m_restauracion->cancelaciones; }
    Q_INVOKABLE int descartesRestauracion() const { return m_restauracion->descartes; }
    Q_INVOKABLE int solicitudes() const { return m_restauracion->solicitudes.size(); }
    Q_INVOKABLE void responderSolicitud(int estado)
    {
        satcfdi::respaldo::ResultadoSolicitudRestauracion r;
        r.estado = static_cast<satcfdi::respaldo::ResultadoSolicitudRestauracion::Estado>(estado);
        m_restauracion->solicitudes.resolverUltima(r);
    }

signals:
    void respaldoChanged();

private:
    std::unique_ptr<fakes::FakeControlRespaldo> m_respaldo;
    std::unique_ptr<fakes::FakeControlRestauracion> m_restauracion;
    std::unique_ptr<fakes::FakeDatosPrevios> m_datos;
    QPointer<satcfdi::RespaldoViewModel> m_vm;
    int m_impresiones = 0;
    int m_copias = 0;
};
