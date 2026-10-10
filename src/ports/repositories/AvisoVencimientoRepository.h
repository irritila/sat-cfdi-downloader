#pragma once

#include "domain/common/Resultado.h"
#include "domain/perfiles/PerfilId.h"
#include "ports/persistence/ErrorPersistencia.h"

#include <QDateTime>

namespace satcfdi {

// Dedupe persistido de avisos de vencimiento de e.firma (T014.3 D3, migracion
// 005, tabla aviso_vencimiento_efirma). Clave (perfil, vigente_hasta,
// umbral_dias): reemplazar la e.firma cambia vigente_hasta y con ello la clave,
// lo que reinicia el dedupe sin borrar filas. Sincrono.
//
// Hilo: solo desde tareas de PersistenceDispatcher.
class AvisoVencimientoRepository {
public:
    virtual ~AvisoVencimientoRepository() = default;

    // INSERT si la clave no existe. true si se inserto (aviso nuevo); false si
    // ya estaba (ya se aviso). `umbralDias` es 30 o 7. Tx: requerida (error
    // Transaccion sin UnitOfWork activo). Errores: Integridad (umbral o perfil
    // invalidos), Ocupado, Almacenamiento.
    virtual Resultado<bool, ErrorPersistencia> registrarSiNuevo(const PerfilId& perfil, const QDateTime& vigenteHasta,
                                                                int umbralDias, const QDateTime& avisadoEn) = 0;
};

} // namespace satcfdi
