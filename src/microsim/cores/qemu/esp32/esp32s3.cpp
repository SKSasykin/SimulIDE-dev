/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>

#include "circuit.h"
#include "esp32adc.h"
#include "esp32gpio.h"
#include "esp32pin.h"
#include "esp32s3.h"
#include "esp32usart.h"
#include "itemlibrary.h"
#include "mainwindow.h"
#include "utils.h"

#define tr( str ) simulideTr( "Esp32s3", str )

#define IOMEM_BASE 0x60000000
#define IOMEM_END 0x6007FFFF
#define IOMEM_SIZE IOMEM_END - IOMEM_BASE

Esp32s3::Esp32s3( QString type, QString id, QString device ) : QemuDevice( type, id ) {
    m_area = QRect( 0, 0, 15 * 8, 15 * 8 );
    m_color = QColor( 50, 50, 70 );

    QString exe = MainWindow::self() ? MainWindow::self()->getDataFilePath( "bin/qemu-system-xtensa" ) : "";
    m_executable = exe.isEmpty() ? "./data/bin/qemu-system-xtensa" : exe;
    m_firmware = "";

    m_ioMem.resize( IOMEM_SIZE, 0 );
    m_ioMemStart = IOMEM_BASE;

    m_gpio = new Esp32Gpio( this, id + "-GPIO", 0, &m_apbFreq, 0x00004000, 0x00004FFF, 49, 32 );

    QString package = "./data/esp32/esp32s3.package";
    if ( MainWindow::self() ) {
        QString path = MainWindow::self()->getDataFilePath( "esp32/esp32s3.package" );
        if ( !path.isEmpty() )
            package = path;
    }
    setPackageFile( package );
    Chip::setName( m_device );

    Esp32Pin* dummyP = m_gpio->m_dummyPin;

    m_usartN = 3;
    m_usarts.resize( m_usartN );
    m_usarts[0] = new Esp32Usart( this, id + "Usart1", 0, &m_apbFreq, 0x00000000, 0x00000FFF );
    m_usarts[1] = new Esp32Usart( this, id + "Usart2", 1, &m_apbFreq, 0x00010000, 0x00010FFF );
    m_usarts[2] = new Esp32Usart( this, id + "Usart3", 2, &m_apbFreq, 0x0002E000, 0x0002EFFF );
    for ( int i = 0; i < m_usartN; ++i )
        m_usarts[i]->setPins( { dummyP, dummyP } );

    m_adc = new Esp32Adc( this, id + "-ADC", 0, &m_apbFreq, 0x00008800, 0x00008FFF, m_gpio, Esp32AdcS3 );

    m_dummyModule = new QemuModule( this, "UnMapped", 0, nullptr, 0, IOMEM_SIZE );

    createMatrix();
    m_gpio->createIoMux();
}
Esp32s3::~Esp32s3() { }

bool Esp32s3::createArgs() {
    QFileInfo fi = QFileInfo( m_firmPath );
    qint64 size = fi.size();

    if ( size == 0 ) {
        QString fallback = MainWindow::self() ? MainWindow::self()->getDataFilePath( "bin/esp32s3/blink.ino.merged.bin" ) : "";
        if ( !fallback.isEmpty() && QFileInfo( fallback ).size() == 4194304 ) {
            qDebug() << "Firmware file not found or empty, using bundled blink firmware:" << m_firmPath;
            m_firmPath = fallback;
            fi = QFileInfo( m_firmPath );
            size = fi.size();
        } else {
            qDebug() << "Error: firmware file not found or empty:" << m_firmPath;
            return false;
        }
    }
    if ( size > 4194304 ) {
        qDebug() << "Error firmware file size:" << size << "must be 4194304";
        qDebug() << m_firmPath;
        return false;
    }

    int index = m_firmPath.lastIndexOf( "." );
    QString firmware = m_firmPath.left( index );
    QString efuses = firmware + ".efuse";

    if ( size < 4194304 ) {
        QString base = QFileInfo( m_firmPath ).baseName();
        QString padPath = QDir::tempPath() + "/simulide-esp32s3-" + base + "-flash.bin";
        QFile pad( padPath );
        if ( pad.exists() )
            pad.remove();
        if ( !pad.open( QIODevice::WriteOnly ) ) {
            qDebug() << "Error: cannot create padded firmware file:" << padPath;
            return false;
        }
        QFile fw( m_firmPath );
        if ( !fw.open( QIODevice::ReadOnly ) ) {
            qDebug() << "Error: cannot open firmware file:" << m_firmPath;
            pad.close();
            return false;
        }
        pad.write( fw.readAll() );
        pad.write( QByteArray( int( 4194304 - size ), char( 0xFF ) ) );
        fw.close();
        pad.close();
        qDebug() << "Padded firmware" << m_firmPath << "to 4194304 bytes ->" << padPath;
        firmware = padPath.left( padPath.lastIndexOf( "." ) );
    }

    if ( !QFileInfo::exists( efuses ) ) {
        QString path = MainWindow::self() ? MainWindow::self()->getDataFilePath( "bin/esp32s3/esp32s3.efuse" ) : "";
        efuses = path.isEmpty() ? "./data/bin/esp32s3/esp32s3.efuse" : path;
    }

    m_arguments.clear();

    m_arguments << m_shMemKey; // Shared Memory key

    m_arguments << "qemu-system-xtensa";

    m_arguments << "-M";
    m_arguments << "esp32s3";

    QString romBin = MainWindow::self() ? MainWindow::self()->getDataFilePath( "bin/esp/rom/bin" ) : "";
    m_arguments << "-L"; /// TODO: embed files in Simulide
    m_arguments << ( romBin.isEmpty() ? "./data/bin/esp/rom/bin" : romBin );

    m_arguments << "-drive";
    m_arguments << "file=" + firmware + ".bin,if=mtd,format=raw";

    m_arguments << "-drive";
    m_arguments << "file=" + efuses + ",if=none,format=raw,id=efuse";

    m_arguments << "-global";
    m_arguments << "driver=nvram.esp32s3.efuse,property=drive,value=efuse";

    m_arguments << "-global";
    m_arguments << "driver=timer.esp32c3.timg,property=wdt_disable,value=true";

    m_arguments << "-icount";
    m_arguments << "shift=4,align=off,sleep=off";

    return true;
}

void Esp32s3::stamp() {
    m_cpuFreq = 40000000; // 40 MHz
    m_apbFreq = 40000000;
    QemuDevice::stamp();
}

Pin* Esp32s3::addPin( QString id, QString type, QString label, int n, int x, int y, int angle, int length, int space ) {
    IoPin* pin = nullptr;

    if ( type.contains( "rst" ) ) {
        pin = new IoPin( angle, QPoint( x, y ), m_id + "-" + id, n - 1, this, input );
        m_rstPin = pin;
        m_rstPin->setOutHighV( 3.3 );
        m_rstPin->setPullup( 1e5 );
        m_rstPin->setInputHighV( 0.65 );
        m_rstPin->setInputLowV( 0.65 );
    } else {
        int gpio = Esp32Gpio::gpioFromId( id );
        if ( gpio >= 0 && gpio < m_gpio->m_nPins )
            pin = m_gpio->createPin( gpio, id, this );
        else {
            pin = new IoPin( angle, QPoint( x, y ), m_id + "-" + id, n - 1, this, input );
            pin->setUnused( true );
        }
    }

    pin->setPos( x, y );
    pin->setPinAngle( angle );
    pin->setLength( length );
    pin->setSpace( space );
    pin->setLabelText( label );
    pin->setFlag( QGraphicsItem::ItemStacksBehindParent, true );
    return pin;
}

void Esp32s3::updtFrequency() {
    if ( m_cpuFreq == m_arena->regData && m_apbFreq == m_arena->regAddr )
        return;

    m_cpuFreq = m_arena->regData;
    m_apbFreq = m_arena->regAddr;

    for ( QemuModule* module : m_modules )
        module->freqChanged();

    qDebug() << "Esp32s3::updtFrequency CPU:" << m_cpuFreq / 1000000 << "MHz,  APB:" << m_apbFreq / 1000000 << "MHz";
}

void Esp32s3::createMatrix() {
    for ( int i = 0; i < 256; ++i ) {
        m_gpio->m_matrixIn[i] = { nullptr, nullptr, "---" };
        m_gpio->m_matrixOut[i] = { nullptr, nullptr, "---" };
    }

    // UART0
    m_gpio->m_matrixIn[14] = { m_usarts[0], m_usarts[0]->getRxPinPtr(), "Rx0" }; // U0RXD
    m_gpio->m_matrixOut[14] = { m_usarts[0], m_usarts[0]->getTxPinPtr(), "Tx0" }; // U0TXD
    // UART1
    m_gpio->m_matrixIn[17] = { m_usarts[1], m_usarts[1]->getRxPinPtr(), "Rx1" }; // U1RXD
    m_gpio->m_matrixOut[17] = { m_usarts[1], m_usarts[1]->getTxPinPtr(), "Tx1" }; // U1TXD
    // UART2
    m_gpio->m_matrixIn[198] = { m_usarts[2], m_usarts[2]->getRxPinPtr(), "Rx2" }; // U2RXD
    m_gpio->m_matrixOut[198] = { m_usarts[2], m_usarts[2]->getTxPinPtr(), "Tx2" }; // U2TXD
}
