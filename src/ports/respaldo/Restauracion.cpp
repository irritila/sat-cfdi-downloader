#include "ports/respaldo/Restauracion.h"

#include <QDir>

namespace satcfdi::respaldo {

QString claveEstable(ErrorRestauracion::Categoria c)
{
    using C = ErrorRestauracion::Categoria;
    switch (c) {
    case C::EnCurso: return QStringLiteral("en_curso");
    case C::Detenido: return QStringLiteral("detenido");
    case C::NoPreparada: return QStringLiteral("no_preparada");
    case C::RestauracionPendiente: return QStringLiteral("restauracion_pendiente");
    case C::Origen: return QStringLiteral("origen");
    case C::OrigenCambio: return QStringLiteral("origen_cambio");
    case C::StagingCambio: return QStringLiteral("staging_cambio");
    case C::CredencialIncorrecta: return QStringLiteral("credencial_incorrecta");
    case C::ClaveRecuperacionMalFormada: return QStringLiteral("clave_recuperacion_mal_formada");
    case C::Contenedor: return QStringLiteral("contenedor");
    case C::MetadatosInvalidos: return QStringLiteral("metadatos_invalidos");
    case C::EsquemaMasNuevo: return QStringLiteral("esquema_mas_nuevo");
    case C::EsquemaInvalido: return QStringLiteral("esquema_invalido");
    case C::Integridad: return QStringLiteral("integridad");
    case C::RutaInesperada: return QStringLiteral("ruta_inesperada");
    case C::Base: return QStringLiteral("base");
    case C::Staging: return QStringLiteral("staging");
    case C::Destino: return QStringLiteral("destino");
    case C::EspacioInsuficiente: return QStringLiteral("espacio_insuficiente");
    case C::Marcador: return QStringLiteral("marcador");
    case C::Cancelado: return QStringLiteral("cancelado");
    case C::Interno: return QStringLiteral("interno");
    }
    return QStringLiteral("interno");
}

QString StagingRestauracion::rutaBase() const
{
    return QDir(datos).filePath(QStringLiteral("satcfdi.sqlite3"));
}

} // namespace satcfdi::respaldo
