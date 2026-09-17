/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QProcess>

#include "chip.h"
#include "qemuarena.h"

class BleChatDialog;
class IoPin;
class QemuModule;
class QemuUsart;
class QemuTimer;
class QemuTwi;
class QemuSpi;
class LibraryItem;

class QemuDevice : public Chip {
public:
    QemuDevice( QString type, QString id );
    ~QemuDevice();

    void initialize() override;
    void stamp() override;
    //void updateStep() override;
    void voltChanged() override;
    void runEvent() override;

    QString firmware() { return m_firmware; }
    void setFirmware( QString file );

    QString extraArgs() { return m_extraArgs; }
    void setExtraArgs( QString a ) { m_extraArgs = a; }

    QString emuFrequency() { return m_emuFrequency; }
    void setEmuFrequency( QString f );

    void setPackageFile( QString package );

    std::vector<uint32_t>* getIoMem() { return &m_ioMem; }
    volatile qemuArena_t* getArena() { return m_arena; }

    void slotLoad();
    void slotReload();
    void slotOpenTerm( int num );
    void slotOpenBleChat();

    void addModule( QemuModule* m ) { m_modules.append( m ); }

    // Host-link UDP ports for the WiFi / BT network backends.
    int  wifiLinkPort() { return m_wifiLinkPort; }
    void setWifiLinkPort( int p );
    int  btLinkPort() { return m_btLinkPort; }
    void setBtLinkPort( int p );
    int  hostForwardPort() { return m_hostForwardPort; }
    void setHostForwardPort( int p ) { m_hostForwardPort = p; }

    static QemuDevice* self() { return m_pSelf; }
    static Component* construct( QString type, QString id );
    static LibraryItem* libraryItem();

protected:
    static QemuDevice* m_pSelf;

    virtual bool createArgs() { return false; }

    virtual void doAction();
    virtual void updtFrequency() { ; }

    void runModuleEvent();

    void contextMenu( QGraphicsSceneContextMenuEvent* e, QMenu* m ) override;

    QString m_lastFirmDir; // Last firmware folder used
    QString m_firmware;
    QString m_firmPath;
    QString m_executable;
    QString m_packageFile;

    QString m_extraArgs;
    QString m_emuFrequency;

    volatile qemuArena_t* m_arena;

    QemuModule* m_dummyModule;
    QemuModule* m_eventModule;
    uint64_t m_nextEvent;
    uint64_t m_lastEvent;

    uint32_t m_ioMemStart;

    int m_wifiLinkPort = 0;
    int m_btLinkPort = 0;
    int m_hostForwardPort = 0;

    int m_gpioSize;
    std::vector<IoPin*> m_ioPin;
    IoPin* m_rstPin;
    bool m_resetRequested = false;

    QString m_shMemKey;
    int64_t m_shMemId;

    void* m_wHandle;

    QProcess m_qemuProcess;
    QStringList m_arguments;

    uint8_t m_portN;
    uint8_t m_usartN;
    uint8_t m_timerN;
    uint8_t m_i2cN;
    uint8_t m_spiN;

    std::vector<QemuTwi*> m_i2cs;
    std::vector<QemuSpi*> m_spis;
    std::vector<QemuUsart*> m_usarts;
    std::vector<QemuTimer*> m_timers;
    BleChatDialog* m_bleChat = nullptr;
    std::vector<uint32_t> m_ioMem;

    QList<QemuModule*> m_modules;
};
