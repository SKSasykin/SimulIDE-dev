#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

struct SpiffsEntry {
    QString path;
    QByteArray data;
    quint32 size = 0;
};

struct SpiffsStats {
    quint64 totalBytes = 0;
    quint64 usedBytes = 0;
    quint64 freeBytes = 0;
    int fileCount = 0;
    bool hasPartition = false;
};

class SpiffsImage {
public:
    static QVector<SpiffsEntry> read( const QString& path, QString* error = nullptr );
    static QVector<SpiffsEntry> parse( const QByteArray& flash, QString* error = nullptr );
    static SpiffsStats stats( const QString& path, QString* error = nullptr );
    static bool writeFile( const QString& path, const QString& filePath, const QByteArray& data,
                           QString* error = nullptr );
    static bool removeFile( const QString& path, const QString& filePath, QString* error = nullptr );
};
