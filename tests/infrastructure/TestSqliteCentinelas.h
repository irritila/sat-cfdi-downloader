#pragma once

#include <QObject>

// Centinelas de secretos: tras sanear con RegexLogSanitizer y persistir con
// los repositorios SQLite, ningun secreto aparece en `.sqlite3`, `-wal` ni
// `-shm`, ni antes ni despues del cierre. Un marcador de control no sensible
// demuestra que la inspeccion de archivos ve los datos escritos.
class TestSqliteCentinelas : public QObject {
    Q_OBJECT

private slots:
    void secretosNoLleganAArchivosSqlite();
};
