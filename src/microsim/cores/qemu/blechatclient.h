#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

#include "qemuarena.h"

class QemuBt;

struct BleChatDevice {
    QByteArray address;
    uint8_t addressType = 0;
    QString addressText;
    QString name;
    QByteArray advData;
    int rssi = 0;
};

class BleChatClient : public QObject {
    Q_OBJECT

public:
    explicit BleChatClient( QObject* parent = nullptr );
    ~BleChatClient();

    void start();
    void stop();
    bool started() const { return m_started; }

    void startScan();
    void stopScan();
    bool scanning() const { return m_scanning; }

    QList<BleChatDevice> devices() const { return m_devices; }

    void connectToDevice( int index );
    void disconnect();
    bool connected() const { return m_connHandle != 0; }
    bool connecting() const { return m_connecting; }
    bool notificationsOn() const { return m_notifyEnabled; }
    bool ready() const { return m_chatReady; }
    QString connectionText() const;
    QString attributeText() const;

    void discover();
    void readValue();
    void writeValue( const QByteArray& data );
    void setNotifications( bool enable );
    int serviceCount() const { return m_services.size(); }
    QString serviceText( int svc ) const;
    int charCount( int svc ) const;
    QString charText( int svc, int pos ) const;
    int selectedService() const { return m_selSvc; }
    int selectedChar() const { return m_selChar; }
    bool targetWritable() const { return m_valueHandle != 0 && ( m_charProps & 0x08 ); }
    void selectTarget( int svc, int pos );

    void poll();

signals:
    void devicesChanged();
    void connectionChanged();
    void discoveryChanged();
    void valueRead( QByteArray data );
    void writeDone( bool ok, QString info );
    void notification( QByteArray data );
    void statusMessage( QString text );
    void errorMessage( QString text );

private:
    enum DiscoverStage {
        DiscoverNone,
        DiscoverServices,
        DiscoverChars,
        DiscoverDescs,
        DiscoverSubscribe,
        DiscoverDone
    };

    struct PendingAtt {
        bool active = false;
        uint8_t opcode = 0;
        uint16_t handle = 0;
        QByteArray data;
        QString kind;
    };

    struct KnownService {
        uint16_t start = 0;
        uint16_t end = 0;
        QByteArray uuid;
    };

    struct KnownChar {
        int svcIndex = -1;
        uint16_t decl = 0;
        uint8_t props = 0;
        uint16_t valueHandle = 0;
        QByteArray uuid;
    };

    struct PendingTarget {
        bool active = false;
        int svc = -1;
        int pos = -1;
    };

    bool sendCommand( uint16_t opcode, const QByteArray& params );
    bool drainFrames();
    void handleEventFrame( const QByteArray& frame );
    void handleAclFrame( const QByteArray& frame );
    void handleL2capPacket( const QByteArray& packet );
    void handleAttPdu( const QByteArray& pdu );
    void sendAtt( const QByteArray& pdu );
    bool sendAclFragments( const QByteArray& l2cap );
    void parseAdvertisingReport( const uint8_t* data, int len );
    void parseConnectionComplete( const uint8_t* data, int len, bool enhanced );
    void startServiceDiscovery();
    void startCharDiscovery();
    void continueCharWalk();
    void startDescDiscovery();
    void selectDefaultTarget();
    void applyTargetFields( int svc, int pos );
    void applyTargetAndDiscover();
    int bestCharPos( int svc ) const;
    const KnownChar* charAt( int svc, int pos ) const;
    static QString uuidText( const QByteArray& uuid );
    static QString propsText( uint8_t props );
    void finishDiscovery( bool ok, const QString& info );
    void resetLinkState();
    void setChatReady( bool ready );
    static QString addrToText( const uint8_t* addr );
    static QString parseAdvName( const QByteArray& adv );

    qemuArena_t m_arena;
    QemuBt* m_controller = nullptr;
    bool m_started = false;
    bool m_scanning = false;
    QList<BleChatDevice> m_devices;
    QByteArray m_targetAddress;
    uint8_t m_targetAddressType = 0;
    bool m_connecting = false;
    bool m_cancelRequested = false;
    QElapsedTimer m_connectTimer;
    uint16_t m_connHandle = 0;
    QByteArray m_peerAddress;

    DiscoverStage m_stage = DiscoverNone;
    PendingAtt m_pending;
    PendingTarget m_pendingTarget;
    QList<KnownService> m_services;
    QList<KnownChar> m_chars;
    int m_discSvcIdx = 0;
    int m_selSvc = -1;
    int m_selChar = -1;
    uint16_t m_svcStart = 0;
    uint16_t m_svcEnd = 0;
    QByteArray m_svcUuid;
    uint16_t m_declHandle = 0;
    uint8_t m_charProps = 0;
    uint16_t m_valueHandle = 0;
    QByteArray m_charUuid;
    uint16_t m_cccdHandle = 0;
    uint16_t m_descEnd = 0;
    bool m_subscribeWanted = false;
    bool m_chatReady = false;
    bool m_notifyEnabled = false;
    QByteArray m_l2capBuffer;
    int m_l2capExpected = 0;
    uint16_t m_l2capHandle = 0;
};
