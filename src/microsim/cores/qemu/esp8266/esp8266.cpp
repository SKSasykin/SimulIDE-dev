/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 *   ( see copyright.txt file at root folder )                             *
 ***************************************************************************/

#include "esp8266.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>

#include "circuit.h"
#include "esp32usart.h"
#include "esp8266adc.h"
#include "esp8266gpio.h"
#include "itemlibrary.h"
#include "mainwindow.h"
#include "utils.h"

#define tr(str) simulideTr("Esp8266", str)

#define IOMEM_BASE 0x60000000
#define IOMEM_END  0x6000FFFF
#define IOMEM_SIZE IOMEM_END - IOMEM_BASE

Esp8266::Esp8266( QString type, QString id, QString device )
        : QemuDevice( type, id )
        , m_cpuFreq( 80000000 )
        , m_apbFreq( 80000000 )
        , m_adcPin( nullptr )
{
    m_area = QRect( 0, 0, 15*8, 15*8 );
    m_color = QColor( 50, 50, 70 );

    QString exe = MainWindow::self() ? MainWindow::self()->getDataFilePath("bin/qemu-system-xtensa") : "";
    m_executable = exe.isEmpty() ? "./data/bin/qemu-system-xtensa" : exe;

    m_ioMem.resize( IOMEM_SIZE );
    m_ioMemStart = IOMEM_BASE;

    m_gpio = new Esp8266Gpio( this, id+"-GPIO", 0, &m_apbFreq, 0x00000300, 0x000003FF, 17 );

    QString package = "./data/esp8266/esp8266.package";
    if( MainWindow::self() )
    {
        QString path = MainWindow::self()->getDataFilePath("esp8266/esp8266.package");
        if( !path.isEmpty() ) package = path;
    }
    setPackageFile( package );
    Chip::setName( m_device );

    m_adc = new Esp8266Adc( this, id+"-ADC", 0, &m_apbFreq, 0x00000D00, 0x00000DFF, m_adcPin );

    m_usartN = 2;
    m_usarts.resize( m_usartN );
    m_usarts[0] = new Esp32Usart( this, id+"Usart1", 0, &m_apbFreq, 0x00000000, 0x00000FFF );
    m_usarts[1] = new Esp32Usart( this, id+"Usart2", 1, &m_apbFreq, 0x00000F00, 0x00000FFF );

    m_usarts[0]->setPins( { m_gpio->m_espPad[1], m_gpio->m_espPad[3] } ); // UART0: TX=GPIO1, RX=GPIO3
    m_usarts[1]->setPins( { m_gpio->m_espPad[2], m_gpio->m_espPad[8] } ); // UART1: TX=GPIO2, RX=GPIO8

    m_dummyModule = new QemuModule( this, "UnMapped", 0, nullptr, 0, IOMEM_SIZE );
}

Esp8266::~Esp8266()
{
}

void Esp8266::stamp()
{
    m_cpuFreq = 80000000;
    m_apbFreq = 80000000;
    QemuDevice::stamp();
}

Pin* Esp8266::addPin( QString id, QString type, QString label, int n,
                      int x, int y, int angle, int length, int space )
{
    Q_UNUSED(n)
    IoPin* pin = nullptr;

    if( id == "A0" || id == "TOUT" )
    {
        pin = new IoPin( angle, QPoint(x, y), m_id+"-"+id, n-1, this, input );
        m_adcPin = pin;
    }
    else if( type.contains("rst") || id.contains("rst", Qt::CaseInsensitive)
        || id == "CHIP_EN" || id == "CH_PD" || id == "EN" )
    {
        pin = new IoPin( angle, QPoint(x, y), m_id+"-"+id, n-1, this, input );
        m_rstPin = pin;
        m_rstPin->setOutHighV( 3.3 );
        m_rstPin->setPullup( 1e5 );
        m_rstPin->setInputHighV( 0.65 );
        m_rstPin->setInputLowV( 0.65 );
    }
    else if( id.contains( "Vdd", Qt::CaseInsensitive ) || id.contains( "Vcc", Qt::CaseInsensitive )
             || id.contains( "Gnd", Qt::CaseInsensitive ) || id.contains( "Vss", Qt::CaseInsensitive ) )
    {
        pin = new IoPin( angle, QPoint(x, y), m_id+"-"+id, n-1, this, input );
    }
    else
    {
        int gpio = Esp8266Gpio::gpioFromId( id );
        if( gpio >= 0 && gpio < m_gpio->m_nPins )
            pin = m_gpio->createPin( gpio, id, this );
        else
        {
            pin = new IoPin( angle, QPoint(x, y), m_id+"-"+id, n-1, this, input );
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

bool Esp8266::createArgs()
{
    QFileInfo fi( m_firmPath );
    qint64 size = fi.size();
    if( size < 1 )
    {
        QString fallback = MainWindow::self() ? MainWindow::self()->getDataFilePath("bin/esp8266/blink.bin") : "";
        if( !fallback.isEmpty() && QFileInfo( fallback ).size() > 0 )
        {
            qDebug() << "Firmware file not found or empty, using bundled blink firmware:" << m_firmPath;
            m_firmPath = fallback;
            fi = QFileInfo( m_firmPath );
            size = fi.size();
        }
        else
        {
            QMessageBox::warning( nullptr, tr("Esp8266"),
                                 tr("File %1 not found or empty").arg(m_firmPath) );
            return false;
        }
    }
    m_arguments.clear();
    m_arguments << m_shMemKey
                << "qemu-system-xtensa"
                << "-M"    << "esp8266-simul"
                << "-bios" << m_firmPath
                << "-icount" << "shift=4,align=off,sleep=on";
    return true;
}

void Esp8266::updtFrequency()
{
    if( m_cpuFreq == m_arena->regData && m_apbFreq == m_arena->regAddr ) return;
    m_cpuFreq = m_arena->regData;
    m_apbFreq = m_arena->regAddr;
    for( QemuModule* module : m_modules ) module->freqChanged();
    qDebug() << "Esp8266::updtFrequency: cpu" << m_cpuFreq << "apb" << m_apbFreq;
}
