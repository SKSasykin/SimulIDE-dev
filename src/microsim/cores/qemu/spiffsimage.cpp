#include "spiffsimage.h"

#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QObject>
#include <QSaveFile>
#include <algorithm>

namespace {

const int SPIFFS_PAGE_SIZE = 256;
const int SPIFFS_PAGE_HEADER_SIZE = 5;
const int SPIFFS_DATA_SIZE = SPIFFS_PAGE_SIZE - SPIFFS_PAGE_HEADER_SIZE;
const quint16 SPIFFS_OBJ_ID_IX_FLAG = 0x8000;
const quint8 SPIFFS_TYPE_FILE = 1;

struct FlashRange {
    int offset;
    int size;
};

struct FileHeader {
    quint32 size = 0;
    quint8 type = 0;
    QString name;
    int offset = 0;
};

struct DataSpan {
    int offset = 0;
    QByteArray data;
};

quint16 read16( const QByteArray& data, int offset ) {
    return quint16( quint8( data.at( offset ) ) ) | ( quint16( quint8( data.at( offset + 1 ) ) ) << 8 );
}

quint32 read32( const QByteArray& data, int offset ) {
    return quint32( quint8( data.at( offset ) ) ) | ( quint32( quint8( data.at( offset + 1 ) ) ) << 8 )
           | ( quint32( quint8( data.at( offset + 2 ) ) ) << 16 )
           | ( quint32( quint8( data.at( offset + 3 ) ) ) << 24 );
}

bool validPage( quint8 flags ) {
    return ( flags & 0x01 ) == 0 && ( flags & 0x80 ) != 0 && ( flags & 0x02 ) == 0;
}

bool validName( const QByteArray& flash, int offset, QString* name ) {
    int length = 0;
    while ( length < 32 && flash.at( offset + length ) != 0 ) {
        uchar c = uchar( flash.at( offset + length ) );
        if ( c < 0x20 || c == 0x7f )
            return false;
        ++length;
    }
    if ( length == 0 || length == 32 )
        return false;
    *name = QString::fromUtf8( flash.constData() + offset, length );
    return true;
}

void parseRange( const QByteArray& flash, const FlashRange& range, QMap<quint16, FileHeader>& headers,
                 QMap<quint16, QMap<quint16, DataSpan>>& dataPages ) {
    int firstPage = range.offset / SPIFFS_PAGE_SIZE;
    int lastPage = ( range.offset + range.size ) / SPIFFS_PAGE_SIZE;

    for ( int page = firstPage; page < lastPage; ++page ) {
        int offset = page * SPIFFS_PAGE_SIZE;
        quint16 objId = read16( flash, offset );
        quint16 span = read16( flash, offset + 2 );
        quint8 flags = quint8( flash.at( offset + 4 ) );

        if ( objId == 0 || objId == 0xFFFF || !validPage( flags ) )
            continue;
        if ( ( flags & 0x04 ) != 0 || ( objId & SPIFFS_OBJ_ID_IX_FLAG ) == 0 )
            continue;
        if ( ( flags & 0x40 ) == 0 )
            continue;
        if ( span != 0 )
            continue;

        quint32 size = read32( flash, offset + 8 );
        quint8 type = quint8( flash.at( offset + 12 ) );
        if ( size > quint32( range.size ) || type != SPIFFS_TYPE_FILE )
            continue;
        if ( offset + 45 > flash.size() )
            continue;

        QString name;
        if ( !validName( flash, offset + 13, &name ) )
            continue;
        if ( !name.startsWith( '/' ) )
            name.prepend( '/' );

        quint16 baseId = objId & ~SPIFFS_OBJ_ID_IX_FLAG;
        auto it = headers.find( baseId );
        if ( it == headers.end() || offset > it->offset ) {
            FileHeader header;
            header.size = size;
            header.type = type;
            header.name = name;
            header.offset = offset;
            headers.insert( baseId, header );
        }
    }

    for ( int page = firstPage; page < lastPage; ++page ) {
        int offset = page * SPIFFS_PAGE_SIZE;
        quint16 objId = read16( flash, offset );
        quint16 span = read16( flash, offset + 2 );
        quint8 flags = quint8( flash.at( offset + 4 ) );

        if ( objId == 0 || objId == 0xFFFF || !validPage( flags ) )
            continue;
        if ( ( flags & 0x04 ) == 0 || ( objId & SPIFFS_OBJ_ID_IX_FLAG ) != 0 )
            continue;

        quint16 baseId = objId;
        auto hit = headers.find( baseId );
        if ( hit == headers.end() )
            continue;
        if ( quint64( span ) * SPIFFS_DATA_SIZE >= quint64( hit->size ) + SPIFFS_DATA_SIZE )
            continue;

        qint64 remaining = qint64( hit->size ) - qint64( span ) * SPIFFS_DATA_SIZE;
        if ( remaining <= 0 )
            continue;
        int length = int( std::min<qint64>( SPIFFS_DATA_SIZE, remaining ) );

        DataSpan spanData;
        spanData.offset = offset;
        spanData.data = flash.mid( offset + SPIFFS_PAGE_HEADER_SIZE, length );

        auto& spans = dataPages[baseId];
        auto spanIt = spans.find( span );
        if ( spanIt == spans.end() || offset > spanIt->offset )
            spans.insert( span, spanData );
    }
}

QVector<FlashRange> spiffsRanges( const QByteArray& flash, bool* hasTable ) {
    QVector<FlashRange> ranges;
    *hasTable = false;

    const int tableOffset = 0x8000;
    if ( flash.size() < tableOffset + 32 )
        return ranges;

    bool sawEntry = false;
    for ( int i = 0; i < 96; ++i ) {
        int pos = tableOffset + i * 32;
        if ( pos + 32 > flash.size() )
            break;
        quint16 magic = read16( flash, pos );
        if ( magic != 0x50AA ) {
            if ( !sawEntry )
                continue;
            break;
        }
        sawEntry = true;
        quint8 type = quint8( flash.at( pos + 2 ) );
        quint8 subtype = quint8( flash.at( pos + 3 ) );
        quint32 offset = read32( flash, pos + 4 );
        quint32 size = read32( flash, pos + 8 );

        if ( type == 0x01 && subtype == 0x82 && size > 0 && offset < quint32( flash.size() )
             && quint64( offset ) + size <= quint64( flash.size() ) ) {
            FlashRange range;
            range.offset = int( offset );
            range.size = int( size );
            ranges.append( range );
        }
    }
    *hasTable = sawEntry;
    return ranges;
}

bool write16( QByteArray* data, int offset, quint16 value ) {
    if ( offset < 0 || offset + 2 > data->size() )
        return false;
    (*data)[offset] = char( value & 0xff );
    (*data)[offset + 1] = char( value >> 8 );
    return true;
}

bool write32( QByteArray* data, int offset, quint32 value ) {
    if ( offset < 0 || offset + 4 > data->size() )
        return false;
    for ( int i = 0; i < 4; ++i )
        (*data)[offset + i] = char( value >> ( i * 8 ) );
    return true;
}

bool rebuildRange( QByteArray* flash, const FlashRange& range, const QVector<SpiffsEntry>& entries,
                   int metaSize, QString* error ) {
    if ( range.offset % 4096 || range.size < 4096 || range.offset + range.size > flash->size() ) {
        if ( error ) *error = QObject::tr( "Invalid SPIFFS partition geometry" );
        return false;
    }

    const int pagesPerBlock = 4096 / SPIFFS_PAGE_SIZE;
    const int blocks = range.size / 4096;
    QByteArray rebuilt( range.size, char( 0xff ) );
    quint16 objectId = 1;
    int block = 0;
    int pageInBlock = 1;

    for ( const SpiffsEntry& entry : entries ) {
        QByteArray name = entry.path.toUtf8();
        if ( name.startsWith( '/' ) ) name.remove( 0, 1 );
        if ( name.isEmpty() || name.size() >= 32 || entry.data.size() > 0xffffffffu ) {
            if ( error ) *error = QObject::tr( "Invalid SPIFFS file name or size: %1" ).arg( entry.path );
            return false;
        }
        int dataPages = ( entry.data.size() + SPIFFS_DATA_SIZE - 1 ) / SPIFFS_DATA_SIZE;
        int needed = 1 + dataPages;
        if ( needed > pagesPerBlock - 1 ) {
            if ( error ) *error = QObject::tr( "File is too large for the SPIFFS writer: %1" ).arg( entry.path );
            return false;
        }
        if ( pageInBlock + needed > pagesPerBlock ) {
            ++block;
            pageInBlock = 1;
        }
        if ( block >= blocks ) {
            if ( error ) *error = QObject::tr( "Not enough free SPIFFS space" );
            return false;
        }

        int indexPage = block * pagesPerBlock + pageInBlock++;
        int indexOffset = indexPage * SPIFFS_PAGE_SIZE;
        if ( !write16( &rebuilt, indexOffset, quint16( 0x8000 | objectId ) )
             || !write16( &rebuilt, indexOffset + 2, 0 ) ) return false;
        rebuilt[indexOffset + 4] = char( 0xf8 );
        write32( &rebuilt, indexOffset + 8, quint32( entry.data.size() ) );
        rebuilt[indexOffset + 12] = char( SPIFFS_TYPE_FILE );
        rebuilt.replace( indexOffset + 13, name.size(), name );
        rebuilt[indexOffset + 13 + name.size()] = 0;

        int dataOffset = entry.data.size() ? pageInBlock : -1;
        if ( dataOffset >= 0 )
            write16( &rebuilt, indexOffset + 45 + metaSize, quint16( block * pagesPerBlock + dataOffset ) );
        for ( int span = 0; span < dataPages; ++span ) {
            int page = block * pagesPerBlock + pageInBlock++;
            int offset = page * SPIFFS_PAGE_SIZE;
            write16( &rebuilt, offset, objectId );
            write16( &rebuilt, offset + 2, quint16( span ) );
            rebuilt[offset + 4] = char( 0xfc );
            int start = span * SPIFFS_DATA_SIZE;
            rebuilt.replace( offset + SPIFFS_PAGE_HEADER_SIZE, entry.data.mid( start, SPIFFS_DATA_SIZE ).size(),
                            entry.data.mid( start, SPIFFS_DATA_SIZE ) );
        }
        ++objectId;
    }

    for ( int b = 0; b < blocks; ++b ) {
        int lookup = b * 4096;
        for ( int p = 1; p < pagesPerBlock; ++p ) {
            int offset = ( b * pagesPerBlock + p ) * SPIFFS_PAGE_SIZE;
            quint16 id = read16( rebuilt, offset );
            if ( id == 0xffff ) continue;
            write16( &rebuilt, lookup + ( p - 1 ) * 2, id );
        }
        int metadata = range.offset + b * 4096 + SPIFFS_PAGE_SIZE - 4;
        rebuilt[lookup + SPIFFS_PAGE_SIZE - 4] = flash->at( metadata );
        rebuilt[lookup + SPIFFS_PAGE_SIZE - 3] = flash->at( metadata + 1 );
        rebuilt[lookup + SPIFFS_PAGE_SIZE - 2] = flash->at( metadata + 2 );
        rebuilt[lookup + SPIFFS_PAGE_SIZE - 1] = flash->at( metadata + 3 );
    }
    flash->replace( range.offset, range.size, rebuilt );
    return true;
}

bool updateImage( const QString& path, const QString& filePath, const QByteArray* replacement,
                  QString* error ) {
    QFile input( path );
    if ( !input.open( QIODevice::ReadOnly ) ) {
        if ( error ) *error = QObject::tr( "Cannot open flash image: %1" ).arg( path );
        return false;
    }
    QByteArray flash = input.readAll();
    input.close();
    QString parseError;
    QVector<SpiffsEntry> entries = SpiffsImage::parse( flash, &parseError );
    QString normalized = filePath.startsWith( '/' ) ? filePath : "/" + filePath;
    bool found = false;
    QVector<SpiffsEntry> updated;
    for ( const SpiffsEntry& entry : entries ) {
        if ( entry.path == normalized ) {
            found = true;
            if ( replacement ) {
                SpiffsEntry copy = entry;
                copy.data = *replacement;
                copy.size = quint32( copy.data.size() );
                updated.append( copy );
            }
        } else updated.append( entry );
    }
    if ( replacement && !found ) {
        SpiffsEntry entry;
        entry.path = normalized;
        entry.data = *replacement;
        entry.size = quint32( entry.data.size() );
        updated.append( entry );
    }
    if ( !replacement && !found ) {
        if ( error ) *error = QObject::tr( "SPIFFS file not found: %1" ).arg( normalized );
        return false;
    }
    bool hasTable = false;
    QVector<FlashRange> ranges = spiffsRanges( flash, &hasTable );
    if ( !hasTable || ranges.isEmpty() ) {
        if ( error ) *error = QObject::tr( "SPIFFS partition table not found" );
        return false;
    }
    if ( ranges.size() != 1 ) {
        if ( error ) *error = QObject::tr( "Multiple SPIFFS partitions are not supported" );
        return false;
    }
    int metaSize = 0;
    for ( int offset = ranges.first().offset; offset + SPIFFS_PAGE_SIZE <= flash.size(); offset += SPIFFS_PAGE_SIZE ) {
        if ( read16( flash, offset ) & SPIFFS_OBJ_ID_IX_FLAG && read16( flash, offset + 2 ) == 0
             && quint8( flash.at( offset + 4 ) ) == 0xf8 ) {
            quint16 at45 = read16( flash, offset + 45 );
            quint16 at49 = read16( flash, offset + 49 );
            int maxPage = flash.size() / SPIFFS_PAGE_SIZE;
            if ( at49 < maxPage && at49 != 0xffff && ( at45 >= maxPage || at45 == 0xffff ) )
                metaSize = 4;
            break;
        }
    }
    if ( !rebuildRange( &flash, ranges.first(), updated, metaSize, error ) ) return false;

    QSaveFile output( path );
    if ( !output.open( QIODevice::WriteOnly ) || output.write( flash ) != flash.size() || !output.commit() ) {
        if ( error ) *error = QObject::tr( "Cannot save flash image: %1" ).arg( path );
        return false;
    }
    return true;
}

} // namespace

QVector<SpiffsEntry> SpiffsImage::read( const QString& path, QString* error ) {
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        if ( error )
            *error = QObject::tr( "Cannot open flash image: %1" ).arg( path );
        return {};
    }
    return parse( file.readAll(), error );
}

SpiffsStats SpiffsImage::stats( const QString& path, QString* error ) {
    SpiffsStats result;
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) {
        if ( error ) *error = QObject::tr( "Cannot open flash image: %1" ).arg( path );
        return result;
    }

    QByteArray flash = file.readAll();
    bool hasTable = false;
    QVector<FlashRange> ranges = spiffsRanges( flash, &hasTable );
    result.hasPartition = hasTable && !ranges.isEmpty();
    if ( ranges.isEmpty() ) {
        FlashRange range;
        range.offset = 0;
        range.size = flash.size();
        ranges.append( range );
    }
    for ( const FlashRange& range : ranges )
        result.totalBytes += quint64( range.size );

    QVector<SpiffsEntry> entries = parse( flash, error );
    result.fileCount = entries.size();
    for ( const SpiffsEntry& entry : entries )
        result.usedBytes += entry.size;
    result.freeBytes = result.usedBytes < result.totalBytes ? result.totalBytes - result.usedBytes : 0;
    return result;
}

QVector<SpiffsEntry> SpiffsImage::parse( const QByteArray& flash, QString* error ) {
    if ( flash.isEmpty() ) {
        if ( error )
            *error = QObject::tr( "Empty flash image" );
        return {};
    }

    bool hasTable = false;
    QVector<FlashRange> ranges = spiffsRanges( flash, &hasTable );

    if ( ranges.isEmpty() ) {
        FlashRange range;
        range.offset = 0;
        range.size = flash.size();
        ranges.append( range );
    }

    QMap<quint16, FileHeader> headers;
    QMap<quint16, QMap<quint16, DataSpan>> dataPages;

    for ( const FlashRange& range : ranges )
        parseRange( flash, range, headers, dataPages );

    QVector<SpiffsEntry> entries;
    for ( auto it = headers.constBegin(); it != headers.constEnd(); ++it ) {
        const FileHeader& header = it.value();
        if ( header.type != SPIFFS_TYPE_FILE )
            continue;

        QByteArray content;
        auto dataIt = dataPages.constFind( it.key() );
        if ( dataIt != dataPages.constEnd() ) {
            content.reserve( int( header.size ) );
            for ( const DataSpan& span : dataIt.value() )
                content.append( span.data );
        }
        if ( quint32( content.size() ) > header.size )
            content.truncate( int( header.size ) );

        SpiffsEntry entry;
        entry.path = header.name;
        entry.data = content;
        entry.size = header.size;
        entries.append( entry );
    }

    std::sort( entries.begin(), entries.end(),
               []( const SpiffsEntry& a, const SpiffsEntry& b ) { return a.path < b.path; } );

    if ( entries.isEmpty() ) {
        if ( error ) {
            if ( hasTable )
                *error = QObject::tr( "No SPIFFS partition or files found" );
            else
                *error = QObject::tr( "No SPIFFS files found" );
        }
    }
    return entries;
}

bool SpiffsImage::writeFile( const QString& path, const QString& filePath, const QByteArray& data, QString* error ) {
    return updateImage( path, filePath, &data, error );
}

bool SpiffsImage::removeFile( const QString& path, const QString& filePath, QString* error ) {
    return updateImage( path, filePath, nullptr, error );
}
