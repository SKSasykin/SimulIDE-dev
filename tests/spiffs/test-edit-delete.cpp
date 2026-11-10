#include "spiffsimage.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>

static QByteArray readImage( const QString& path ) {
    QFile file( path );
    if ( !file.open( QIODevice::ReadOnly ) ) return {};
    return file.readAll();
}

static bool writeImage( const QString& path, const QByteArray& data ) {
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly ) ) return false;
    return file.write( data ) == data.size();
}

static bool copyImage( const QString& source, const QString& destination ) {
    QFile::remove( destination );
    return QFile::copy( source, destination );
}

int main( int argc, char** argv ) {
    QCoreApplication app( argc, argv );
    if ( argc != 2 ) return 2;

    const QString image = QString::fromLocal8Bit( argv[1] );
    QString error;
    QFile source( image );
    if ( !source.open( QIODevice::ReadOnly ) ) return 3;
    QByteArray rawData = source.readAll();
    source.close();
    QByteArray flash( 0x20000, char( 0xff ) );
    flash.replace( 0x10000, rawData.size(), rawData );
    flash[0x8000] = char( 0xaa );
    flash[0x8001] = char( 0x50 );
    flash[0x8002] = char( 0x01 );
    flash[0x8003] = char( 0x82 );
    for ( int i = 0; i < 4; ++i ) flash[0x8004 + i] = char( 0x10000 >> ( i * 8 ) );
    for ( int i = 0; i < 4; ++i ) flash[0x8008 + i] = char( 0x10000 >> ( i * 8 ) );
    const QString partitionedImage = image + ".partitioned";
    const QByteArray bootArea = flash.left( 0x10000 );
    QFile partitioned( partitionedImage );
    if ( !partitioned.open( QIODevice::WriteOnly ) || partitioned.write( flash ) != flash.size() ) return 3;
    partitioned.close();

    const QString rawImage = image + ".raw";
    const QByteArray rawBefore = readImage( image );
    if ( !copyImage( image, rawImage ) ) return 3;
    if ( SpiffsImage::writeFile( rawImage, "/edit.txt", QByteArray( "unsafe\n" ), &error ) ) return 3;
    if ( readImage( rawImage ) != rawBefore ) return 3;
    QFile::remove( rawImage );

    const QByteArray partitionedBefore = readImage( partitionedImage );
    SpiffsStats initialStats = SpiffsImage::stats( partitionedImage, &error );
    if ( !initialStats.hasPartition || initialStats.totalBytes != 0x10000 || initialStats.fileCount != 2
         || initialStats.usedBytes != 12 ) return 4;

    if ( !SpiffsImage::writeFile( partitionedImage, "/edit.txt", QByteArray( "new text\n" ), &error ) ) {
        qWarning() << error;
        return 4;
    }

    QVector<SpiffsEntry> entries = SpiffsImage::read( partitionedImage, &error );
    QFile verify( partitionedImage );
    if ( !verify.open( QIODevice::ReadOnly ) || !verify.read( bootArea.size() ).startsWith( bootArea ) ) return 5;
    verify.close();
    bool edited = false;
    for ( const SpiffsEntry& entry : entries )
        edited = edited || ( entry.path == "/edit.txt" && entry.data == QByteArray( "new text\n" ) );
    if ( entries.size() != 2 || !edited ) return 5;

    const QByteArray afterWrite = readImage( partitionedImage );
    if ( !SpiffsImage::removeFile( partitionedImage, "/missing.txt", &error ) ) {
        if ( !error.contains( "not found" ) ) return 6;
    } else return 6;
    if ( readImage( partitionedImage ) != afterWrite ) return 6;

    if ( !SpiffsImage::writeFile( partitionedImage, "/created.hex", QByteArray(), &error ) ) return 7;
    QByteArray imported;
    imported.append( char( 0x00 ) );
    imported.append( char( 0x7f ) );
    imported.append( char( 0xff ) );
    if ( !SpiffsImage::writeFile( partitionedImage, "/import.bin", imported, &error ) ) return 7;

    if ( !SpiffsImage::removeFile( partitionedImage, "/data.bin", &error ) ) {
        qWarning() << error;
        return 6;
    }
    entries = SpiffsImage::read( partitionedImage, &error );
    if ( entries.size() != 3 ) return 8;
    bool created = false;
    bool importedFile = false;
    for ( const SpiffsEntry& entry : entries ) {
        created = created || ( entry.path == "/created.hex" && entry.data.isEmpty() );
        importedFile = importedFile || ( entry.path == "/import.bin" && entry.data == imported );
    }
    if ( !created || !importedFile ) return 8;

    if ( !readImage( partitionedImage ).startsWith( bootArea ) ) return 9;
    SpiffsStats finalStats = SpiffsImage::stats( partitionedImage, &error );
    if ( finalStats.fileCount != 3 || finalStats.usedBytes != 12 || finalStats.freeBytes != 0x10000 - 12 ) return 9;

    const QString wrongTypeImage = image + ".wrong-type";
    QByteArray wrongType = partitionedBefore;
    wrongType[0x8002] = 0;
    if ( !writeImage( wrongTypeImage, wrongType ) ) return 10;
    if ( SpiffsImage::writeFile( wrongTypeImage, "/edit.txt", QByteArray( "blocked\n" ), &error ) ) return 10;
    if ( readImage( wrongTypeImage ) != wrongType ) return 10;
    QFile::remove( wrongTypeImage );

    const QString multipleImage = image + ".multiple";
    QByteArray multiple = partitionedBefore;
    multiple[0x8020] = char( 0xaa );
    multiple[0x8021] = char( 0x50 );
    multiple[0x8022] = char( 0x01 );
    multiple[0x8023] = char( 0x82 );
    for ( int i = 0; i < 4; ++i ) multiple[0x8024 + i] = char( 0x18000 >> ( i * 8 ) );
    for ( int i = 0; i < 4; ++i ) multiple[0x8028 + i] = char( 0x8000 >> ( i * 8 ) );
    if ( !writeImage( multipleImage, multiple ) ) return 11;
    if ( SpiffsImage::writeFile( multipleImage, "/edit.txt", QByteArray( "blocked\n" ), &error ) ) return 11;
    if ( readImage( multipleImage ) != multiple ) return 11;
    QFile::remove( multipleImage );

    return 0;
}
