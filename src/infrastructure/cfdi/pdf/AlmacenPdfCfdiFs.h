#pragma once

// AlmacenPdfCfdiFs (T015.4, T015 D7): cache de PDF de CFDI en disco.
//
// - Archivos propios: "<UUID|SINUUID>-<sha256 del XML>-g<version>.pdf" (solo
//   [0-9A-Za-z-]). Solo esos nombres se consideran para buscar y expulsar:
//   nunca se borra nada ajeno de los directorios.
// - Escritura atomica con QSaveFile (temporal en el mismo directorio +
//   rename). Directorios 0700 y archivos 0600 (datos fiscales).
// - Uso reciente = fecha de modificacion: un acierto la actualiza. Tras cada
//   escritura se expulsan los menos usados hasta respetar `limiteBytes`
//   (200 MB por defecto), sin borrar el recien escrito. Aplica igual al
//   directorio temporal.
// - Si la cache no se puede escribir (no se puede crear, sin permiso, disco
//   lleno), se usa el directorio temporal de la app con las mismas reglas.
//
// Sin estado mutable: seguro desde el hilo de PrevisualizacionCfdiService.
// Logs solo con el tipo de evento y conteos (D11): sin rutas ni UUID.

#include "ports/cfdi/AlmacenPdfCfdi.h"

namespace satcfdi {

class AlmacenPdfCfdiFs final : public AlmacenPdfCfdi {
public:
    static constexpr qint64 kLimiteBytesPorDefecto = qint64{200} * 1000 * 1000; // 200 MB (D7)

    // `directorioCache` y `directorioTemporal` absolutos; se crean al escribir.
    AlmacenPdfCfdiFs(QString directorioCache, QString directorioTemporal,
                     qint64 limiteBytes = kLimiteBytesPorDefecto);

    std::optional<QString> buscar(const ClavePdfCfdi& clave) override;
    Resultado<PdfAlmacenado, ErrorAlmacenPdf> guardar(const ClavePdfCfdi& clave, const QByteArray& pdf) override;
    Resultado<bool, ErrorAlmacenPdf> copiar(const QString& rutaPdf, const QString& destinoAbsoluto) override;

    QString directorioCache() const override { return m_cache; }
    QString directorioTemporal() const override { return m_temporal; }
    qint64 limiteBytes() const noexcept { return m_limite; }

    // Nombre de archivo de la clave (para pruebas y diagnostico local).
    static QString nombreArchivo(const ClavePdfCfdi& clave);

private:
    std::optional<QString> escribirEn(const QString& directorio, const QString& nombre, const QByteArray& pdf);
    void expulsar(const QString& directorio, const QString& conservar);
    bool esPropio(const QString& rutaAbsoluta) const;

    QString m_cache;
    QString m_temporal;
    qint64 m_limite;
};

} // namespace satcfdi
