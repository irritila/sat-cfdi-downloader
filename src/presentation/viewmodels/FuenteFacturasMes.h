#pragma once

#include "application/cfdi/DescargaGuiadaService.h"
#include "domain/cfdi/CoberturaMensual.h"
#include "domain/cfdi/MesCalendario.h"
#include "domain/common/Resultado.h"
#include "domain/perfiles/PerfilId.h"
#include "ports/persistence/ErrorPersistencia.h"
#include "ports/repositories/CatalogoMensualRepository.h"
#include "ports/secrets/SecretStoreTypes.h"

#include <QDateTime>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <optional>

namespace satcfdi {

// Perfil para el combo de Facturas (T023.4): todos los perfiles visibles y
// activos, con el estado de su e.firma.
struct PerfilFacturas {
    PerfilId id;
    QString rfc;
    QString nombre;
    std::optional<EstadoCredencial> credencial; // nullopt: no se pudo leer
};

// Lo que la pantalla Facturas necesita de la aplicacion (T023.4). El
// composition root la implementa sobre CatalogoMensualCfdiService,
// DescargaGuiadaService y la preparacion de perfiles; las pruebas, con fakes.
// Los futures se continuan en el hilo grafico; un future cancelado se trata
// como error de lectura. QML no ve SQL, archivos ni SAT.
//
// Senales (hilo grafico): datosCambiados(perfil, mes) cuando cambia algo que
// afecta el mes (indice de CFDI, estado de una descarga, nuevas descargas):
// recargar catalogo y disponibilidad sin vaciar la lista; perfil nulo = puede
// afectar a cualquier perfil. perfilesCambiados()
// cuando cambia la lista de perfiles o una credencial.
class FuenteFacturasMes : public QObject {
    Q_OBJECT

public:
    using ResultadoPerfiles = Resultado<QList<PerfilFacturas>, ErrorPersistencia>;
    using ResultadoCatalogo = Resultado<CatalogoMensual, ErrorPersistencia>;
    using ResultadoDisponibilidad = Resultado<EvaluacionCoberturaMensual, ErrorPersistencia>;
    using ResultadoPreplan = DescargaGuiadaService::ResultadoPreplan;
    using ResultadoEjecucion = DescargaGuiadaService::ResultadoEjecucion;

    using QObject::QObject;
    ~FuenteFacturasMes() override = default;

    virtual QFuture<ResultadoPerfiles> perfiles() = 0;
    virtual QFuture<ResultadoCatalogo> consultar(const ConsultaCatalogoMensual& consulta) = 0;
    // `revalidar`: pedir al indexador revisar los ZIP del mes (al abrir el mes).
    virtual QFuture<ResultadoDisponibilidad> disponibilidad(const PerfilId& perfil, MesCalendario mes,
                                                            bool revalidar) = 0;
    virtual QFuture<ResultadoPreplan> planificar(const PerfilId& perfil, MesCalendario mes, AccionGuiada accion) = 0;
    virtual QFuture<ResultadoEjecucion> ejecutar(const PreplanGuiado& preplan, const DecisionesGuiadas& decisiones) = 0;
    // Reloj inyectado (UTC): mes en curso y mes inicial por omision.
    virtual QDateTime ahoraUtc() const = 0;

signals:
    void datosCambiados(const satcfdi::PerfilId& perfil);
    void perfilesCambiados();
};

// Preferencia local del ultimo perfil usado en Facturas (T023.4). El root la
// implementa con QSettings; las pruebas, en memoria. Solo el id del perfil.
class PreferenciasFacturas {
public:
    virtual ~PreferenciasFacturas() = default;
    virtual std::optional<QString> ultimoPerfil() const = 0;
    virtual void setUltimoPerfil(const QString& perfilId) = 0;
};

} // namespace satcfdi
