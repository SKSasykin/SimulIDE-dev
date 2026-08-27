/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QProcess>

#include "chip.h"

// -------------------------------------------------
// -- WiFi / BT packet rings (SimulIDE host <=> QEMU guest)
//    Producer/consumer single-writer rings. The guest (QEMU) and the
//    SimulIDE host backend each own one side of every ring:
//      wifi_tx : guest -> host   (frames to transmit on the air / network)
//      wifi_rx : host  -> guest  (frames received, delivered to the driver)
//      bt_tx   : guest -> host   (HCI/ACL/iso out)
//      bt_rx   : host  -> guest  (HCI/ACL/iso in)
//    head is written by the producer, tail by the consumer.

#define QEMU_WIFI_RING_FRAMES 64
#define QEMU_WIFI_FRAME_MAX   1536

typedef struct qemuWifiFrame {
    uint32_t len;
    uint8_t  data[QEMU_WIFI_FRAME_MAX];
} qemuWifiFrame_t;

typedef struct qemuWifiRing {
    volatile uint32_t     head;
    volatile uint32_t     tail;
    volatile uint64_t     seq;
    qemuWifiFrame_t       frames[QEMU_WIFI_RING_FRAMES];
} qemuWifiRing_t;

typedef struct qemuArena {
    uint64_t simuTime; // in ps
    uint64_t qemuTime; // in ps
    uint64_t regData;
    uint64_t regAddr;
    uint64_t irqNumber;
    uint64_t irqLevel;
    uint64_t simuAction;
    uint64_t qemuAction;
    uint64_t running;
    int64_t loop_timeout_ns;
    double ps_per_inst;

    qemuWifiRing_t wifi_rx;
    qemuWifiRing_t wifi_tx;
    qemuWifiRing_t bt_rx;
    qemuWifiRing_t bt_tx;

} qemuArena_t;

enum simuAction {
    SIM_NONE = 0,
    SIM_READ,
    SIM_WRITE,
    SIM_FREQ,
    SIM_INTERRUPT,
    SIM_I2C = 10,
    SIM_SPI,
    SIM_USART,
    SIM_TIMER,
    SIM_GPIO_IN,
    SIM_WIFI = 12,
    SIM_BT = 13,
    SIM_EVENT = 1 << 7
};

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
    std::vector<uint32_t> m_ioMem;

    QList<QemuModule*> m_modules;
};
