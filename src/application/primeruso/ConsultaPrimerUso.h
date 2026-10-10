#pragma once

#include "domain/common/Resultado.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QFuture>

namespace satcfdi {

// Pasos del primer uso (T014.3 D1): derivados de los datos, sin bandera
// persistida. La guia se muestra solo con un resultado exitoso.
struct EstadoPrimerUso {
    bool hayPerfiles = false;     // algun perfil SAT no eliminado (activo o no)
    bool hayEFirmaLista = false;  // algun perfil con credencial Lista
    bool haySolicitudes = false;  // alguna solicitud no eliminada

    friend bool operator==(const EstadoPrimerUso&, const EstadoPrimerUso&) = default;
};

// Consulta minima de primer uso. Hilo grafico: consultar() se llama y su
// future se completa en el hilo del PersistenceDispatcher (continuar con
// then(contextoGrafico, ...)). Solo lectura.
class ConsultaPrimerUso {
public:
    using ResultadoPrimerUso = Resultado<EstadoPrimerUso, ErrorPersistencia>;

    virtual ~ConsultaPrimerUso() = default;
    virtual QFuture<ResultadoPrimerUso> consultar() = 0;
};

} // namespace satcfdi
