#pragma once

namespace satcfdi {

// Puerto de almacenamiento local de paquetes ZIP descargados (escritura
// temporal, renombrado final y eliminacion). No expone rutas en T002.
//
// T002: frontera compilable sin metodos. Los metodos se agregan en la tarea
// que fija el contrato (ver docs/tasks/); no adelantar firmas aqui.
class PackageStorage {
public:
    virtual ~PackageStorage() = default;
};

} // namespace satcfdi
