/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QDir>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QLibrary>
#include <QMessageBox>
#include <QProcessEnvironment>
#include <QThread>
#include <qtconcurrentrun.h>

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#if defined( __linux__ ) || defined( __APPLE__ )
#include <sys/mman.h>
#include <sys/shm.h>
#elif defined( _WIN32 )
#include <windows.h>
#endif

#include "circuitwidget.h"
#include "iopin.h"
#include "itemlibrary.h"
#include "qemudevice.h"
#include "intprop.h"
#include "qemuspi.h"
#include "qemutimer.h"
#include "qemutwi.h"
#include "qemuusart.h"

#include "esp32.h"
#include "esp32s3.h"
#include "esp32c3.h"
#include "esp8266.h"
#include "stm32.h"

#include "circuit.h"
#include "componentlist.h"
#include "simulator.h"
#include "utils.h"

#include "stringprop.h"

#define tr( str ) simulideTr( "QemuDevice", str )

QemuDevice* QemuDevice::m_pSelf = nullptr;

Component* QemuDevice::construct( QString type, QString id ) {
    //if( QemuDevice::self() )
    //{
    //    qDebug() << "\nQemuDevice::construct ERROR: only one QemuDevice allowed\n";
    //    return nullptr;
    //}
    QString device = Chip::getDevice( id );

    QemuDevice* qdev = nullptr;

    if ( device.startsWith( "STM32" ) )
        qdev = new Stm32( type, id, device );
    else if ( device.startsWith( "Esp32s3" ) )
        qdev = new Esp32s3( type, id, device );
    else if ( device.startsWith( "Esp32c3" ) )
        qdev = new Esp32c3( type, id, device );
    else if ( device.startsWith( "Esp8266" ) )
        qdev = new Esp8266( type, id, device );
    else if ( device.startsWith( "Esp32" ) )
        qdev = new Esp32( type, id, device );
    return qdev;
}

LibraryItem* QemuDevice::libraryItem() {
    return new LibraryItem( "QemuDevice", "", "ic2.png", "QemuDevice", QemuDevice::construct );
}

QemuDevice::QemuDevice( QString type, QString id ) : Chip( type, id ) {
    m_pSelf = this;
    m_rstPin = nullptr;
    m_emuFrequency = "auto";

    uint64_t pid = QCoreApplication::applicationPid();
    m_shMemKey = QString::number( pid ) + id;
    void* arena = nullptr;

    const int shMemSize = sizeof( qemuArena_t );

    // create the shared memory object
#if defined( __linux__ ) || defined( __APPLE__ )
    QByteArray memKey = m_shMemKey.toLocal8Bit();
    const char* charMemKey = memKey.constData();
    m_shMemId = shm_open( charMemKey, O_CREAT | O_RDWR, 0666 );
    if ( m_shMemId != -1 ) {
        ftruncate( m_shMemId, shMemSize );
        arena = mmap( 0, shMemSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_shMemId, 0 );
    } else {
        qDebug() << "QemuDevice::QemuDevice shm_open failed errno:" << errno << "key:" << m_shMemKey;
    }
#elif defined( _WIN32 )
    const wchar_t* charMemKey = m_shMemKey.toStdWString().c_str();
    // Create a file mapping object
    HANDLE hMapFile = CreateFileMapping( INVALID_HANDLE_VALUE, // Use paging file
                                         NULL, // Default security
                                         PAGE_READWRITE, // Read/write access
                                         0, // Maximum object size (high-order DWORD)
                                         shMemSize, // Maximum object size (low-order DWORD)
                                         charMemKey ); // Name of the mapping object

    if ( hMapFile != NULL )
        arena = MapViewOfFile( hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, shMemSize );

    m_wHandle = hMapFile;
#endif
    if ( arena ) {
        m_arena = (qemuArena_t*) arena;
        qDebug() << "QemuDevice::QemuDevice Shared Mem created" << shMemSize << "bytes";
    } else {
        m_arena = nullptr;
        m_shMemId = -1;
        qDebug() << "QemuDevice::QemuDevice Error creating arena";
    }

    m_qemuProcess.setProcessChannelMode(
        /*QProcess::MergedChannels*/ QProcess::ForwardedChannels ); // Merge stdout and stderr

    //Simulator::self()->addToUpdateList( this );

    addPropGroup( { tr( "Main" ),
                    { new StrProp<QemuDevice>( "Program", tr( "Firmware" ), "", this, &QemuDevice::firmware,
                                               &QemuDevice::setFirmware ),

                      new StrProp<QemuDevice>( "Args", tr( "Extra arguments" ), "", this, &QemuDevice::extraArgs,
                                               &QemuDevice::setExtraArgs ),

                       new StrProp<QemuDevice>(
                           "EmuFreq", tr( "Emulation frequency" ),
                           "auto,10000000,20000000,40000000,80000000,160000000,240000000;"
                           "Auto,10 MHz,20 MHz,40 MHz,80 MHz,160 MHz,240 MHz",
                           this, &QemuDevice::emuFrequency, &QemuDevice::setEmuFrequency, 0, "enum" ),

                       new IntProp<QemuDevice>( "HostForwardPort", tr( "Host Forward Port" ), "_port",
                                                this, &QemuDevice::hostForwardPort,
                                                &QemuDevice::setHostForwardPort ) },
                     0 } );
}
QemuDevice::~QemuDevice() {
    initialize();
#if defined( __linux__ ) || defined( __APPLE__ )
    if ( m_shMemId != -1 )
        shm_unlink( m_shMemKey.toLocal8Bit().data() );
#elif defined( _WIN32 )
    if ( m_wHandle != NULL ) {
        UnmapViewOfFile( (LPVOID) m_arena );
        CloseHandle( (HANDLE) m_wHandle );
    }
#endif

    for ( QemuTwi* twi : m_i2cs )
        delete twi;
    for ( QemuSpi* spi : m_spis )
        delete spi;
    for ( QemuUsart* uar : m_usarts )
        delete uar;
    for ( QemuTimer* tim : m_timers )
        delete tim;

    m_pSelf = nullptr;
}

void QemuDevice::initialize() {
    if ( m_shMemId == -1 )
        return;

    m_arena->running = 0;

    m_qemuProcess.waitForFinished( 500 );
    if ( m_qemuProcess.state() != QProcess::NotRunning ) {
        m_qemuProcess.kill();
        qDebug() << "QemuDevice: Qemu proccess killed";
    } else
        qDebug() << "QemuDevice: Qemu proccess finished";
    //updateStep();

    for ( QemuModule* module : m_modules )
        module->reset();
}

void QemuDevice::setEmuFrequency( QString f ) {
    m_emuFrequency = f;

    bool ok = false;
    double freq = f.toDouble( &ok );
    if ( ok && freq > 0 && m_arena )
        m_arena->ps_per_inst = 1e12 / freq;
}

void QemuDevice::setWifiLinkPort( int p ) {
    m_wifiLinkPort = p;
    for( QemuModule* m : m_modules )
        if( m->getType() == "wifi" )
            m->setHostLink( (quint16)p );
}

void QemuDevice::setBtLinkPort( int p ) {
    m_btLinkPort = p;
    for( QemuModule* m : m_modules )
        if( m->getType() == "bt" )
            m->setHostLink( (quint16)p );
}

void QemuDevice::stamp() {
    if ( m_shMemId == -1 )
        return;

    m_eventModule = nullptr;

    //m_lastTime = 0;
    m_arena->simuTime = 0;
    m_arena->qemuTime = 0;
    m_arena->regData = 0;
    m_arena->regAddr = 0;
    m_arena->irqNumber = 0;
    m_arena->irqLevel = 0;
    m_arena->simuAction = 0;
    m_arena->qemuAction = 0;
    m_arena->loop_timeout_ns = 0;
    m_arena->running = 0;
    m_arena->ps_per_inst = 0;

    if ( m_rstPin )
        m_rstPin->changeCallBack( this );

    if ( createArgs() ) {
        QString executable = m_executable;
#ifdef _WIN32
        executable += ".exe";
#endif
        if ( !QFileInfo::exists( executable ) ) {
            qDebug() << "Error: QemuDevice::stamp executable does not exist:" << Qt::endl << executable;
            return;
        }
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.remove( "SIMULIDE_QEMU_FREQ_HZ" );
        if ( m_emuFrequency != "auto" )
            env.insert( "SIMULIDE_QEMU_FREQ_HZ", m_emuFrequency );
        m_qemuProcess.setProcessEnvironment( env );

        if ( m_qemuProcess.state() != QProcess::NotRunning ) { // Make sure previous process is gone
            m_qemuProcess.waitForFinished( 500 );
            if ( m_qemuProcess.state() != QProcess::NotRunning ) {
                m_qemuProcess.kill();
                m_qemuProcess.waitForFinished( 1000 );
            }
        }
        m_qemuProcess.start( executable, m_arguments );

        QElapsedTimer stampTimer;
        stampTimer.start();
        while ( !m_arena->running ) // Wait for Qemu running
        {
            QThread::msleep( 10 );
            if ( stampTimer.elapsed() > 5000 ) // Don't wait forever
            {
                qDebug() << "Error: QemuDevice::stamp timeout";

                m_qemuProcess.waitForFinished( 500 );

                QString output = m_qemuProcess.readAllStandardError();
                if ( !output.isEmpty() ) {
                    QStringList lines = output.split( "\n" );
                    for ( QString line : lines )
                        qDebug() << line.remove( "\"" );
                }
                if ( m_qemuProcess.state() == QProcess::NotRunning ) {
                    qDebug() << m_qemuProcess.exitStatus();
                    qDebug() << m_qemuProcess.error();
                    qDebug() << m_qemuProcess.exitCode();
                }
                qDebug() << m_qemuProcess.state();
                //                    m_qemuProcess.kill();
                return;
            }
        }
        qDebug() << "\nQemuDevice::stamp started";

        Simulator::self()->addEvent( 1, this );
    }
}

//void QemuDevice::updateStep()
//{
//    return;
//
//    QString output = m_qemuProcess.readAllStandardOutput();
//    if( !output.isEmpty() )
//    {
//        QStringList lines = output.split("\n");
//        for( QString line : lines ) qDebug() << line.remove("\"");
//    }
//}

void QemuDevice::voltChanged() {
    if ( m_shMemId == -1 )
        return;

    bool reset = !m_rstPin->getInpState();

    if ( reset ) {
        m_resetRequested = true;
        m_arena->running = 0;

        QMetaObject::invokeMethod( qApp, [this]() {
            if ( m_qemuProcess.state() > QProcess::NotRunning ) {
                Simulator::self()->cancelEvents( this );
                m_qemuProcess.kill();
            }
        }, Qt::QueuedConnection );
    } else if ( m_resetRequested ) {
        QMetaObject::invokeMethod( qApp, [this]() {
            if ( m_qemuProcess.state() != QProcess::NotRunning ) {
                m_qemuProcess.waitForFinished( 500 );
                if ( m_qemuProcess.state() != QProcess::NotRunning ) {
                    m_qemuProcess.kill();
                    m_qemuProcess.waitForFinished( 1000 );
                }
            }
            Simulator::self()->cancelEvents( this );
            stamp();
            m_resetRequested = false;
        }, Qt::QueuedConnection );
    }
}

void QemuDevice::runModuleEvent() {
    if ( m_eventModule ) {
        m_eventModule->runAction();
        m_eventModule->m_eventAction = 0;
        m_eventModule = nullptr;
    }
}

void QemuDevice::runEvent() {
    uint64_t now = Simulator::self()->circTime();
    //qDebug() << "   QemuDevice::runEvent"<< now;

    runModuleEvent();

    uint64_t nextTime = 0;
    uint64_t noProgress = 0;
    QElapsedTimer waitTimer;
    waitTimer.start();
    while ( true ) {
        m_arena->simuTime = 0;
        while ( !m_arena->simuTime ) // Wait for next event from Qemu
        {
            if ( Simulator::self()->simState() < SIM_RUNNING ) //
            {
                Simulator::self()->addEvent( 1, this );
                return;
            }
            if ( m_resetRequested ) // Reset in progress: stamp() will reschedule
                return;
            if ( waitTimer.elapsed() > 1000 ) { // No post from Qemu: yield to frame loop
                qDebug() << "QemuDevice::runEvent wait timeout";
                Simulator::self()->addEvent( 1, this );
                return;
            }
        }
        waitTimer.restart();

        nextTime = m_arena->simuTime;

        if ( nextTime < now )
            nextTime = now;

        if ( m_arena->simuAction ) {
            if ( m_arena->simuAction == SIM_FREQ )
                updtFrequency();
            else if ( m_arena->simuAction != SIM_EVENT )
                doAction();
            m_arena->simuAction = 0;

            if ( nextTime == now ) {
                runModuleEvent();
                if ( ++noProgress >= 100 ) { // No forward progress: yield to frame loop
                    nextTime = now + 1000000000ull;
                    break;
                }
            } else {
                noProgress = 0;
                break;
            }
        } else { // Time posted without action: reschedule
            noProgress = 0;
            break;
        }
    }
    //qDebug() << "QemuDevice::runEvent Next"<< nextTime;

    Simulator::self()->addEventAt( nextTime, this );
}

void QemuDevice::doAction() {
    uint32_t address = m_arena->regAddr;

    for ( QemuModule* module : m_modules ) {
        if ( address < module->m_memStart || address > module->m_memEnd )
            continue;

        //qDebug() << "   QemuDevice::doAction"<< module->m_name;
        m_eventModule = module;
        module->m_eventAddress = address;
        module->m_eventValue = m_arena->regData;
        module->m_eventAction = m_arena->simuAction;
        break;
    }
}

void QemuDevice::slotOpenTerm( int num ) {
    m_usarts.at( num - 1 )->openMonitor( findIdLabel(), num );
    //m_serialMon = num;
}

void QemuDevice::slotLoad() {
    QDir dir( m_lastFirmDir );
    if ( !dir.exists() )
        m_lastFirmDir = Circuit::self()->getFilePath();

    QString fileName = QFileDialog::getOpenFileName( nullptr, tr( "Load Firmware" ), m_lastFirmDir,
                                                     tr( "All files (*.*);;Hex Files (*.hex)" ) );

    if ( fileName.isEmpty() )
        return; // User cancels loading

    setFirmware( fileName );
}

void QemuDevice::slotReload() {
    if ( !m_firmware.isEmpty() )
        setFirmware( m_firmware );
    else
        QMessageBox::warning( 0, "QemuDevice::slotReload", tr( "No File to reload " ) );
}

void QemuDevice::setFirmware( QString file ) {
    if ( Simulator::self()->isRunning() )
        CircuitWidget::self()->powerCircOff();

    QFileInfo fi( file );
    if ( fi.isAbsolute() ) {
        m_firmware = fi.absoluteFilePath();
        m_firmPath = fi.absoluteFilePath();
    } else {
        QDir circuitDir = QFileInfo( Circuit::self()->getFilePath() ).absoluteDir();
        m_firmware = file;
        m_firmPath = circuitDir.absoluteFilePath( file );
    }
}

void QemuDevice::setPackageFile( QString package ) {
    if ( !QFile::exists( package ) ) {
        qDebug() << "File does not exist:" << package;
        return;
    }
    QString pkgText = fileToString( package, "QemuDevice::setPackageFile" );
    QString pkgStr = convertPackage( pkgText );
    m_isLS = package.endsWith( "_LS.package" );
    initPackage( pkgStr );
    setLogicSymbol( m_isLS );

    m_label.setPlainText( m_name );

    Circuit::self()->update();
}

void QemuDevice::contextMenu( QGraphicsSceneContextMenuEvent* event, QMenu* menu ) {
    //if( m_eMcu.flashSize() )
    {
        QAction* loadAction = menu->addAction( QIcon( ":/load.svg" ), tr( "Load firmware" ) );
        QObject::connect( loadAction, &QAction::triggered, [=]() { slotLoad(); } );

        QAction* reloadAction = menu->addAction( QIcon( ":/reload.svg" ), tr( "Reload firmware" ) );
        QObject::connect( reloadAction, &QAction::triggered, [=]() { slotReload(); } );

        menu->addSeparator();
    }

    //QAction* openRamTab = menu->addAction( QIcon(":/terminal.svg"),tr("Open Mcu Monitor.") );
    //QObject::connect( openRamTab, &QAction::triggered, [=](){ slotOpenMcuMonitor(); } );

    if ( m_usarts.size() ) {
        QMenu* serMonMenu = menu->addMenu( QIcon( ":/serialterm.png" ), tr( "Open Serial Monitor." ) );

        for ( uint i = 0; i < m_usarts.size(); ++i ) {
            const int portNumber = i + 1;

            QAction* act = serMonMenu->addAction( "USART" + QString::number( portNumber ) );
            QObject::connect( act, &QAction::triggered, [=]() { slotOpenTerm( portNumber ); } );
        }
    }
    menu->addSeparator();
    Component::contextMenu( event, menu );
}
