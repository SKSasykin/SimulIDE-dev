#include "blechatclient.h"

#include <cstring>

#include "qemubt.h"

static inline uint16_t bleRd16( const uint8_t* p )
{
    return uint16_t( p[0] ) | ( uint16_t( p[1] ) << 8 );
}

BleChatClient::BleChatClient( QObject* parent )
    : QObject( parent )
{
    std::memset( &m_arena, 0, sizeof( m_arena ) );
}

BleChatClient::~BleChatClient()
{
    stop();
}

QString BleChatClient::addrToText( const uint8_t* addr )
{
    QString text;
    const char hex[] = "0123456789ABCDEF";
    for ( int i = 5; i >= 0; --i ) {
        if ( i != 5 ) text += ':';
        text += QChar( hex[( addr[i] >> 4 ) & 0xF] );
        text += QChar( hex[addr[i] & 0xF] );
    }
    return text;
}

QString BleChatClient::parseAdvName( const QByteArray& adv )
{
    int i = 0;
    while ( i + 1 < adv.size() ) {
        int len = static_cast<uint8_t>( adv.at( i ) );
        if ( len <= 0 || i + len >= adv.size() ) break;
        uint8_t type = static_cast<uint8_t>( adv.at( i + 1 ) );
        if ( type == 0x08 || type == 0x09 ) {
            QByteArray nameBytes = adv.mid( i + 2, len - 1 );
            return QString::fromUtf8( nameBytes );
        }
        i += len + 1;
    }
    return QString();
}

QString BleChatClient::connectionText() const
{
    if ( m_connHandle == 0 ) {
        if ( m_connecting ) return "Connecting...";
        return "Disconnected";
    }
    QString text = "Connected handle=" + QString::number( m_connHandle );
    if ( m_chatReady ) text += " ready";
    else if ( m_stage != DiscoverNone ) text += " discovering...";
    return text;
}

QString BleChatClient::attributeText() const
{
    if ( m_valueHandle == 0 ) return "No characteristic";
    QString text = "handle=" + QString::number( m_valueHandle );
    if ( m_cccdHandle != 0 ) text += " cccd=" + QString::number( m_cccdHandle );
    if ( m_notifyEnabled ) text += " notify=on";
    return text;
}

void BleChatClient::resetLinkState()
{
    m_connecting = false;
    m_cancelRequested = false;
    m_services.clear();
    m_chars.clear();
    m_discSvcIdx = 0;
    m_selSvc = -1;
    m_selChar = -1;
    m_pendingTarget.active = false;
    m_connHandle = 0;
    m_peerAddress.clear();
    m_stage = DiscoverNone;
    m_pending.active = false;
    m_svcStart = 0;
    m_svcEnd = 0;
    m_svcUuid.clear();
    m_declHandle = 0;
    m_charProps = 0;
    m_valueHandle = 0;
    m_charUuid.clear();
    m_cccdHandle = 0;
    m_descEnd = 0;
    m_subscribeWanted = false;
    m_notifyEnabled = false;
    m_l2capBuffer.clear();
    m_l2capExpected = 0;
    setChatReady( false );
}

void BleChatClient::setChatReady( bool ready )
{
    if ( m_chatReady == ready ) return;
    m_chatReady = ready;
    emit discoveryChanged();
    emit connectionChanged();
}

void BleChatClient::start()
{
    if ( m_started ) return;
    std::memset( &m_arena, 0, sizeof( m_arena ) );
    m_controller = new QemuBt( m_arena, "simulide-chat", 0 );
    blockSignals( false );
    m_started = true;
    resetLinkState();
    m_devices.clear();
    emit devicesChanged();
    sendCommand( 0x0C03, QByteArray() );
    drainFrames();
    QByteArray mask;
    mask.append( char( 0x10 ) );
    mask.append( char( 0x00 ) );
    mask.append( char( 0x00 ) );
    mask.append( char( 0x00 ) );
    mask.append( char( 0x00 ) );
    mask.append( char( 0x00 ) );
    mask.append( char( 0x00 ) );
    mask.append( char( 0x20 ) );
    sendCommand( 0x0C01, mask );
    drainFrames();
    QByteArray leMask;
    leMask.append( char( 0x0B ) );
    leMask.append( char( 0x02 ) );
    leMask.append( char( 0x00 ) );
    leMask.append( char( 0x00 ) );
    leMask.append( char( 0x00 ) );
    leMask.append( char( 0x00 ) );
    leMask.append( char( 0x00 ) );
    leMask.append( char( 0x00 ) );
    sendCommand( 0x2001, leMask );
    drainFrames();
    sendCommand( 0x1009, QByteArray() );
    drainFrames();
    sendCommand( 0x2002, QByteArray() );
    drainFrames();
    emit statusMessage( "Chat controller ready" );
}

void BleChatClient::stop()
{
    if ( !m_started ) return;
    blockSignals( true );
    if ( m_scanning ) stopScan();
    if ( m_connHandle != 0 || m_connecting ) disconnect();
    drainFrames();
    delete m_controller;
    m_controller = nullptr;
    m_started = false;
    resetLinkState();
    m_devices.clear();
    emit devicesChanged();
    emit connectionChanged();
}

void BleChatClient::startScan()
{
    if ( !m_started || m_scanning ) return;
    m_devices.clear();
    emit devicesChanged();
    QByteArray params;
    params.append( char( 0x01 ) );
    params.append( char( 0x10 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x10 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x00 ) );
    sendCommand( 0x200B, params );
    QByteArray enable;
    enable.append( char( 0x01 ) );
    enable.append( char( 0x00 ) );
    sendCommand( 0x200C, enable );
    drainFrames();
    m_scanning = true;
    emit devicesChanged();
    emit connectionChanged();
    emit statusMessage( "Scanning..." );
}

void BleChatClient::stopScan()
{
    if ( !m_started || !m_scanning ) return;
    QByteArray enable;
    enable.append( char( 0x00 ) );
    enable.append( char( 0x00 ) );
    sendCommand( 0x200C, enable );
    drainFrames();
    m_scanning = false;
    emit connectionChanged();
    emit statusMessage( "Scan stopped" );
}

void BleChatClient::connectToDevice( int index )
{
    if ( !m_started || m_connHandle != 0 || m_connecting ) return;
    if ( index < 0 || index >= m_devices.size() ) return;
    if ( m_scanning ) stopScan();
    const BleChatDevice& device = m_devices.at( index );
    m_targetAddress = device.address;
    m_targetAddressType = device.addressType;
    QByteArray params;
    params.append( char( 0x10 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x10 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x00 ) );
    params.append( char( device.addressType ) );
    params += device.address;
    params.append( char( 0x00 ) );
    params.append( char( 0x18 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x28 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x01 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x00 ) );
    params.append( char( 0x00 ) );
    sendCommand( 0x200D, params );
    drainFrames();
    if ( m_connHandle != 0 ) return;
    m_connecting = true;
    m_cancelRequested = false;
    m_connectTimer.start();
    emit connectionChanged();
    emit statusMessage( "Connecting to " + device.addressText +
                        " type=" + QString::number( device.addressType ) );
}

void BleChatClient::disconnect()
{
    if ( !m_started ) return;
    if ( m_connecting ) {
        m_cancelRequested = true;
        sendCommand( 0x200E, QByteArray() );
        drainFrames();
        m_connecting = false;
        emit connectionChanged();
        return;
    }
    if ( m_connHandle == 0 ) return;
    QByteArray params;
    params.append( char( m_connHandle & 0xFF ) );
    params.append( char( ( m_connHandle >> 8 ) & 0xFF ) );
    params.append( char( 0x13 ) );
    sendCommand( 0x0406, params );
    drainFrames();
}

void BleChatClient::discover()
{
    if ( m_connHandle == 0 ) return;
    startServiceDiscovery();
}

void BleChatClient::readValue()
{
    if ( m_connHandle == 0 || m_valueHandle == 0 ) {
        emit errorMessage( "Nothing to read" );
        return;
    }
    if ( m_pending.active ) {
        emit errorMessage( "Busy" );
        return;
    }
    QByteArray pdu;
    pdu.append( char( 0x0A ) );
    pdu.append( char( m_valueHandle & 0xFF ) );
    pdu.append( char( ( m_valueHandle >> 8 ) & 0xFF ) );
    m_pending.active = true;
    m_pending.opcode = 0x0A;
    m_pending.handle = m_valueHandle;
    m_pending.kind = "read";
    sendAtt( pdu );
}

void BleChatClient::writeValue( const QByteArray& data )
{
    if ( m_connHandle == 0 || m_valueHandle == 0 ) {
        emit errorMessage( "Not connected" );
        return;
    }
    if ( m_pending.active ) {
        emit errorMessage( "Busy" );
        return;
    }
    if ( data.size() > 20 ) {
        emit errorMessage( "Too long, max 20 bytes" );
        return;
    }
    QByteArray pdu;
    pdu.append( char( 0x12 ) );
    pdu.append( char( m_valueHandle & 0xFF ) );
    pdu.append( char( ( m_valueHandle >> 8 ) & 0xFF ) );
    pdu += data;
    m_pending.active = true;
    m_pending.opcode = 0x12;
    m_pending.handle = m_valueHandle;
    m_pending.kind = "write";
    sendAtt( pdu );
}

void BleChatClient::setNotifications( bool enable )
{
    if ( m_connHandle == 0 || m_cccdHandle == 0 ) {
        emit errorMessage( "No CCCD" );
        return;
    }
    if ( m_pending.active ) {
        emit errorMessage( "Busy" );
        return;
    }
    QByteArray pdu;
    pdu.append( char( 0x12 ) );
    pdu.append( char( m_cccdHandle & 0xFF ) );
    pdu.append( char( ( m_cccdHandle >> 8 ) & 0xFF ) );
    if ( enable ) {
        pdu.append( char( 0x01 ) );
        pdu.append( char( 0x00 ) );
    } else {
        pdu.append( char( 0x00 ) );
        pdu.append( char( 0x00 ) );
    }
    m_pending.active = true;
    m_pending.opcode = 0x12;
    m_pending.handle = m_cccdHandle;
    m_pending.kind = enable ? "subscribe" : "unsubscribe";
    m_subscribeWanted = enable;
    sendAtt( pdu );
}

void BleChatClient::poll()
{
    if ( !m_started || !m_controller ) return;
    m_controller->runTick();
    drainFrames();
    if ( m_connecting && m_connectTimer.hasExpired( 8000 ) ) {
        m_connecting = false;
        m_cancelRequested = true;
        sendCommand( 0x200E, QByteArray() );
        drainFrames();
        emit connectionChanged();
        emit errorMessage( "Connect timeout: peripheral is busy or unreachable, press Scan and try again" );
    }
}

bool BleChatClient::sendCommand( uint16_t opcode, const QByteArray& params )
{
    if ( !m_started || !m_controller ) return false;
    QByteArray frame;
    frame.append( char( 0x01 ) );
    frame.append( char( opcode & 0xFF ) );
    frame.append( char( ( opcode >> 8 ) & 0xFF ) );
    frame.append( char( params.size() & 0xFF ) );
    frame += params;
    volatile qemuWifiRing_t& ring = m_arena.bt_tx;
    uint32_t tail = ring.tail;
    uint32_t next = ( tail + 1 ) % QEMU_WIFI_RING_FRAMES;
    if ( next == ring.head ) {
        emit errorMessage( "Controller busy" );
        return false;
    }
    if ( uint32_t( frame.size() ) > QEMU_WIFI_FRAME_MAX ) return false;
    qemuWifiFrame_t& slot = const_cast<qemuWifiFrame_t&>( ring.frames[tail] );
    slot.len = uint32_t( frame.size() );
    std::memcpy( slot.data, frame.constData(), size_t( frame.size() ) );
    ring.tail = next;
    m_controller->runAction();
    return true;
}

bool BleChatClient::drainFrames()
{
    if ( !m_controller ) return false;
    bool any = false;
    while ( true ) {
        volatile qemuWifiRing_t& ring = m_arena.bt_rx;
        if ( ring.head == ring.tail ) break;
        uint32_t head = ring.head;
        const qemuWifiFrame_t& slot = const_cast<const qemuWifiFrame_t&>( ring.frames[head] );
        uint32_t len = slot.len;
        if ( len == 0 || len > QEMU_WIFI_FRAME_MAX ) {
            ring.head = ( head + 1 ) % QEMU_WIFI_RING_FRAMES;
            m_controller->runAction();
            continue;
        }
        QByteArray frame( reinterpret_cast<const char*>( const_cast<uint8_t*>( slot.data ) ), int( len ) );
        ring.head = ( head + 1 ) % QEMU_WIFI_RING_FRAMES;
        m_controller->runAction();
        any = true;
        if ( frame.size() < 1 ) continue;
        if ( static_cast<uint8_t>( frame.at( 0 ) ) == 0x04 ) handleEventFrame( frame );
        else if ( static_cast<uint8_t>( frame.at( 0 ) ) == 0x02 ) handleAclFrame( frame );
    }
    return any;
}

void BleChatClient::handleEventFrame( const QByteArray& frame )
{
    if ( frame.size() < 3 ) return;
    const uint8_t* data = reinterpret_cast<const uint8_t*>( frame.constData() );
    uint8_t code = data[1];
    uint8_t plen = data[2];
    if ( int( plen ) + 3 != frame.size() ) return;
    const uint8_t* params = data + 3;
    if ( code == 0x0E ) {
        if ( plen < 4 ) return;
        uint16_t opcode = uint16_t( params[1] ) | ( uint16_t( params[2] ) << 8 );
        uint8_t status = params[3];
        if ( status != 0 ) {
            emit errorMessage( "Command 0x" + QString::number( opcode, 16 ) + " failed " + QString::number( status ) );
            if ( m_connecting && opcode == 0x200D ) {
                m_connecting = false;
                emit connectionChanged();
            }
            if ( m_pending.active ) {
                m_pending.active = false;
                if ( m_pending.kind == "write" ) emit writeDone( false, "Controller error" );
                if ( m_stage != DiscoverNone ) finishDiscovery( false, "Controller error" );
            }
        }
        return;
    }
    if ( code == 0x0F ) {
        if ( plen < 4 ) return;
        uint8_t status = params[0];
        uint16_t opcode = uint16_t( params[2] ) | ( uint16_t( params[3] ) << 8 );
        if ( status != 0 ) {
            emit errorMessage( "Command 0x" + QString::number( opcode, 16 ) + " rejected " + QString::number( status ) );
            if ( m_connecting && opcode == 0x200D ) {
                m_connecting = false;
                emit connectionChanged();
            }
        } else if ( opcode == 0x200D && m_connecting ) {
            emit statusMessage( "Connection request accepted, waiting for complete" );
        }
        return;
    }
    if ( code == 0x05 ) {
        if ( plen < 4 ) return;
        uint16_t handle = bleRd16( params + 1 );
        uint8_t reason = params[3];
        if ( handle == m_connHandle ) {
            resetLinkState();
            emit connectionChanged();
            emit discoveryChanged();
            emit statusMessage( "Disconnected reason=" + QString::number( reason ) );
        }
        return;
    }
    if ( code == 0x13 ) return;
    if ( code != 0x3E ) return;
    if ( plen < 1 ) return;
    uint8_t sub = params[0];
    const uint8_t* subData = params + 1;
    int subLen = plen - 1;
    if ( sub == 0x02 ) {
        parseAdvertisingReport( subData, subLen );
    } else if ( sub == 0x01 || sub == 0x0A ) {
        parseConnectionComplete( subData, subLen, sub == 0x0A );
    }
}

void BleChatClient::parseAdvertisingReport( const uint8_t* data, int len )
{
    if ( !data || len < 1 ) return;
    int count = data[0];
    int pos = 1;
    bool changed = false;
    for ( int i = 0; i < count; ++i ) {
        if ( pos + 9 > len ) break;
        uint8_t eventType = data[pos];
        uint8_t addrType = data[pos + 1];
        const uint8_t* addr = data + pos + 2;
        uint8_t dataLen = data[pos + 8];
        if ( pos + 9 + dataLen + 1 > len ) break;
        QByteArray adv;
        if ( dataLen ) adv = QByteArray( reinterpret_cast<const char*>( data + pos + 9 ), dataLen );
        int rssi = int8_t( data[pos + 9 + dataLen] );
        QByteArray rawAddr( reinterpret_cast<const char*>( addr ), 6 );
        int found = -1;
        for ( int k = 0; k < m_devices.size(); ++k ) {
            if ( m_devices.at( k ).address == rawAddr ) {
                found = k;
                break;
            }
        }
        QString name = parseAdvName( adv );
        if ( found < 0 ) {
            BleChatDevice device;
            device.address = rawAddr;
            device.addressType = addrType;
            device.addressText = addrToText( addr );
            device.name = name;
            device.advData = adv;
            device.rssi = rssi;
            m_devices.append( device );
            changed = true;
        } else {
            BleChatDevice& device = m_devices[found];
            device.advData = adv;
            device.rssi = rssi;
            if ( !name.isEmpty() ) device.name = name;
            (void)eventType;
            changed = true;
        }
        pos += 10 + dataLen;
    }
    if ( changed ) emit devicesChanged();
}

void BleChatClient::parseConnectionComplete( const uint8_t* data, int len, bool enhanced )
{
    int need = enhanced ? 26 : 16;
    if ( !data || len < need ) return;
    uint8_t status = data[0];
    uint16_t handle = bleRd16( data + 1 );
    if ( status != 0 ) {
        if ( m_connecting ) {
            m_connecting = false;
            emit connectionChanged();
            if ( m_cancelRequested ) emit statusMessage( "Cancelled" );
            else emit errorMessage( "Connect failed " + QString::number( status ) );
        }
        m_cancelRequested = false;
        return;
    }
    const uint8_t* addr = data + 5;
    m_connHandle = handle;
    m_peerAddress = QByteArray( reinterpret_cast<const char*>( addr ), 6 );
    m_connecting = false;
    m_scanning = false;
    emit connectionChanged();
    emit statusMessage( "Connected handle=" + QString::number( handle ) );
    startServiceDiscovery();
}

void BleChatClient::startServiceDiscovery()
{
    m_stage = DiscoverServices;
    m_services.clear();
    m_chars.clear();
    m_discSvcIdx = 0;
    m_selSvc = -1;
    m_selChar = -1;
    m_pendingTarget.active = false;
    m_svcStart = 0;
    m_svcEnd = 0;
    m_svcUuid.clear();
    m_valueHandle = 0;
    m_cccdHandle = 0;
    setChatReady( false );
    QByteArray pdu;
    pdu.append( char( 0x10 ) );
    pdu.append( char( 0x01 ) );
    pdu.append( char( 0x00 ) );
    pdu.append( char( 0xFF ) );
    pdu.append( char( 0xFF ) );
    pdu.append( char( 0x00 ) );
    pdu.append( char( 0x28 ) );
    m_pending.active = true;
    m_pending.opcode = 0x10;
    m_pending.kind = "services";
    sendAtt( pdu );
    emit discoveryChanged();
    emit statusMessage( "Discovering services..." );
}

void BleChatClient::startCharDiscovery()
{
    if ( m_discSvcIdx < 0 || m_discSvcIdx >= m_services.size() ) {
        finishDiscovery( false, "No service" );
        return;
    }
    const KnownService& service = m_services.at( m_discSvcIdx );
    m_stage = DiscoverChars;
    QByteArray pdu;
    pdu.append( char( 0x08 ) );
    pdu.append( char( service.start & 0xFF ) );
    pdu.append( char( ( service.start >> 8 ) & 0xFF ) );
    pdu.append( char( service.end & 0xFF ) );
    pdu.append( char( ( service.end >> 8 ) & 0xFF ) );
    pdu.append( char( 0x03 ) );
    pdu.append( char( 0x28 ) );
    m_pending.active = true;
    m_pending.opcode = 0x08;
    m_pending.kind = "chars";
    sendAtt( pdu );
    emit discoveryChanged();
}

void BleChatClient::continueCharWalk()
{
    ++m_discSvcIdx;
    if ( m_discSvcIdx < m_services.size() ) {
        startCharDiscovery();
        return;
    }
    selectDefaultTarget();
}

void BleChatClient::startDescDiscovery()
{
    if ( m_valueHandle == 0 ) {
        finishDiscovery( false, "No characteristic" );
        return;
    }
    m_stage = DiscoverDescs;
    uint16_t start = m_valueHandle + 1;
    m_descEnd = m_svcEnd;
    if ( start > m_descEnd ) {
        finishDiscovery( true, "No descriptors" );
        return;
    }
    QByteArray pdu;
    pdu.append( char( 0x04 ) );
    pdu.append( char( start & 0xFF ) );
    pdu.append( char( ( start >> 8 ) & 0xFF ) );
    pdu.append( char( m_descEnd & 0xFF ) );
    pdu.append( char( ( m_descEnd >> 8 ) & 0xFF ) );
    m_pending.active = true;
    m_pending.opcode = 0x04;
    m_pending.kind = "descs";
    sendAtt( pdu );
    emit discoveryChanged();
}

void BleChatClient::selectDefaultTarget()
{
    int bestSvc = -1;
    int bestPos = -1;
    int best = -1;
    for ( int svc = 0; svc < m_services.size(); ++svc ) {
        const int svcScore = m_services.at( svc ).uuid.size() == 16 ? 2 : 0;
        for ( int pos = 0; pos < charCount( svc ); ++pos ) {
            const KnownChar* chr = charAt( svc, pos );
            if ( !chr ) continue;
            int score = svcScore * 10;
            if ( ( chr->props & 0x18 ) == 0x18 ) score += 2;
            else if ( chr->props & 0x08 ) score += 1;
            if ( score > best ) {
                best = score;
                bestSvc = svc;
                bestPos = pos;
            }
        }
    }
    if ( bestSvc < 0 ) {
        finishDiscovery( false, "Characteristic not found" );
        return;
    }
    m_pendingTarget.active = false;
    applyTargetFields( bestSvc, bestPos );
    emit statusMessage( "Service found handles " + QString::number( m_svcStart ) +
                        "-" + QString::number( m_svcEnd ) );
    emit statusMessage( "Characteristic found value handle " +
                        QString::number( m_valueHandle ) );
    startDescDiscovery();
}

void BleChatClient::applyTargetFields( int svc, int pos )
{
    const KnownService& service = m_services.at( svc );
    const KnownChar* chr = charAt( svc, pos );
    if ( !chr ) return;
    m_selSvc = svc;
    m_selChar = pos;
    m_svcStart = service.start;
    m_svcEnd = service.end;
    m_svcUuid = service.uuid;
    m_declHandle = chr->decl;
    m_charProps = chr->props;
    m_valueHandle = chr->valueHandle;
    m_charUuid = chr->uuid;
    m_cccdHandle = 0;
    m_notifyEnabled = false;
}

void BleChatClient::applyTargetAndDiscover()
{
    m_pendingTarget.active = false;
    applyTargetFields( m_pendingTarget.svc, m_pendingTarget.pos );
    emit discoveryChanged();
    startDescDiscovery();
}

int BleChatClient::bestCharPos( int svc ) const
{
    int bestPos = -1;
    int best = -1;
    for ( int pos = 0; pos < charCount( svc ); ++pos ) {
        const KnownChar* chr = charAt( svc, pos );
        if ( !chr ) continue;
        int score = 0;
        if ( ( chr->props & 0x18 ) == 0x18 ) score = 2;
        else if ( chr->props & 0x08 ) score = 1;
        if ( score > best ) {
            best = score;
            bestPos = pos;
        }
    }
    return bestPos;
}

const BleChatClient::KnownChar* BleChatClient::charAt( int svc, int pos ) const
{
    int seen = -1;
    for ( int i = 0; i < m_chars.size(); ++i ) {
        if ( m_chars.at( i ).svcIndex != svc ) continue;
        ++seen;
        if ( seen == pos ) return &m_chars.at( i );
    }
    return nullptr;
}

void BleChatClient::selectTarget( int svc, int pos )
{
    if ( m_connHandle == 0 ) {
        emit errorMessage( "Not connected" );
        return;
    }
    if ( svc < 0 || svc >= m_services.size() ) {
        emit errorMessage( "No such service" );
        return;
    }
    if ( pos < 0 ) pos = bestCharPos( svc );
    if ( !charAt( svc, pos ) ) {
        emit errorMessage( "No characteristic" );
        return;
    }
    if ( m_pending.active ) {
        emit errorMessage( "Busy" );
        return;
    }
    const KnownChar* chr = charAt( svc, pos );
    if ( svc == m_selSvc && chr->valueHandle == m_valueHandle ) return;
    m_pendingTarget.active = true;
    m_pendingTarget.svc = svc;
    m_pendingTarget.pos = pos;
    if ( m_notifyEnabled && m_cccdHandle != 0 ) {
        QByteArray pdu;
        pdu.append( char( 0x12 ) );
        pdu.append( char( m_cccdHandle & 0xFF ) );
        pdu.append( char( ( m_cccdHandle >> 8 ) & 0xFF ) );
        pdu.append( char( 0x00 ) );
        pdu.append( char( 0x00 ) );
        m_pending.active = true;
        m_pending.opcode = 0x12;
        m_pending.handle = m_cccdHandle;
        m_pending.kind = "unswitch";
        m_subscribeWanted = false;
        sendAtt( pdu );
        return;
    }
    applyTargetAndDiscover();
}

QString BleChatClient::uuidText( const QByteArray& uuid )
{
    const char hex[] = "0123456789ABCDEF";
    if ( uuid.size() == 2 ) {
        const uint16_t value = bleRd16( reinterpret_cast<const uint8_t*>( uuid.constData() ) );
        QString text = "0x";
        text += QChar( hex[( value >> 12 ) & 0xF] );
        text += QChar( hex[( value >> 8 ) & 0xF] );
        text += QChar( hex[( value >> 4 ) & 0xF] );
        text += QChar( hex[value & 0xF] );
        return text;
    }
    QString text;
    for ( int i = 0; i < uuid.size(); ++i ) {
        if ( i > 0 ) text += ' ';
        const uint8_t byte = static_cast<uint8_t>( uuid.at( i ) );
        text += QChar( hex[( byte >> 4 ) & 0xF] );
        text += QChar( hex[byte & 0xF] );
    }
    return text;
}

QString BleChatClient::propsText( uint8_t props )
{
    QString text;
    if ( props & 0x02 ) text += 'R';
    if ( props & 0x08 ) text += 'W';
    if ( props & 0x10 ) text += 'N';
    const char hex[] = "0123456789ABCDEF";
    text += " (0x";
    text += QChar( hex[( props >> 4 ) & 0xF] );
    text += QChar( hex[props & 0xF] );
    text += ')';
    return text;
}

QString BleChatClient::serviceText( int svc ) const
{
    if ( svc < 0 || svc >= m_services.size() ) return QString();
    const KnownService& service = m_services.at( svc );
    return "handles " + QString::number( service.start ) + "-" +
           QString::number( service.end ) + " " + uuidText( service.uuid );
}

int BleChatClient::charCount( int svc ) const
{
    int count = 0;
    for ( int i = 0; i < m_chars.size(); ++i )
        if ( m_chars.at( i ).svcIndex == svc ) ++count;
    return count;
}

QString BleChatClient::charText( int svc, int pos ) const
{
    const KnownChar* chr = charAt( svc, pos );
    if ( !chr ) return QString();
    return "value " + QString::number( chr->valueHandle ) + " " +
           propsText( chr->props ) + " " + uuidText( chr->uuid );
}

void BleChatClient::finishDiscovery( bool ok, const QString& info )
{
    m_pending.active = false;
    if ( !ok ) {
        m_stage = DiscoverNone;
        emit discoveryChanged();
        emit errorMessage( info );
        return;
    }
    if ( info == "continue" ) return;
    m_stage = DiscoverDone;
    setChatReady( true );
    emit discoveryChanged();
    emit statusMessage( "Chat ready" );
}

void BleChatClient::sendAtt( const QByteArray& pdu )
{
    QByteArray l2cap;
    uint16_t len = uint16_t( pdu.size() );
    l2cap.append( char( len & 0xFF ) );
    l2cap.append( char( ( len >> 8 ) & 0xFF ) );
    l2cap.append( char( 0x04 ) );
    l2cap.append( char( 0x00 ) );
    l2cap += pdu;
    sendAclFragments( l2cap );
}

bool BleChatClient::sendAclFragments( const QByteArray& l2cap )
{
    if ( !m_started || !m_controller || m_connHandle == 0 ) return false;
    int offset = 0;
    bool first = true;
    while ( offset < l2cap.size() ) {
        int chunk = qMin( 27, l2cap.size() - offset );
        QByteArray frag = l2cap.mid( offset, chunk );
        uint16_t flags = m_connHandle & 0x0FFF;
        if ( first ) flags |= ( 0 << 12 );
        else flags |= ( 1 << 12 );
        QByteArray frame;
        frame.append( char( 0x02 ) );
        frame.append( char( flags & 0xFF ) );
        frame.append( char( ( flags >> 8 ) & 0xFF ) );
        frame.append( char( chunk & 0xFF ) );
        frame.append( char( ( chunk >> 8 ) & 0xFF ) );
        frame += frag;
        volatile qemuWifiRing_t& ring = m_arena.bt_tx;
        uint32_t tail = ring.tail;
        uint32_t next = ( tail + 1 ) % QEMU_WIFI_RING_FRAMES;
        if ( next == ring.head ) {
            emit errorMessage( "Controller busy" );
            return false;
        }
        qemuWifiFrame_t& slot = const_cast<qemuWifiFrame_t&>( ring.frames[tail] );
        slot.len = uint32_t( frame.size() );
        std::memcpy( slot.data, frame.constData(), size_t( frame.size() ) );
        ring.tail = next;
        m_controller->runAction();
        offset += chunk;
        first = false;
    }
    return true;
}

void BleChatClient::handleAclFrame( const QByteArray& frame )
{
    if ( frame.size() < 5 ) return;
    const uint8_t* data = reinterpret_cast<const uint8_t*>( frame.constData() );
    uint16_t flags = bleRd16( data + 1 );
    uint16_t payloadLen = bleRd16( data + 3 );
    uint16_t handle = flags & 0x0FFF;
    uint8_t pb = ( flags >> 12 ) & 0x03;
    if ( int( payloadLen ) + 5 != frame.size() ) return;
    if ( handle != m_connHandle ) return;
    QByteArray payload = frame.mid( 5 );
    if ( pb == 2 || pb == 0 ) {
        if ( payload.size() < 4 ) return;
        uint16_t l2len = bleRd16( reinterpret_cast<const uint8_t*>( payload.constData() ) );
        uint16_t cid = bleRd16( reinterpret_cast<const uint8_t*>( payload.constData() ) + 2 );
        if ( cid != 0x0004 ) return;
        m_l2capExpected = 4 + l2len;
        m_l2capHandle = handle;
        m_l2capBuffer = payload;
        if ( m_l2capBuffer.size() >= m_l2capExpected ) {
            QByteArray packet = m_l2capBuffer.left( m_l2capExpected );
            m_l2capBuffer.clear();
            m_l2capExpected = 0;
            handleL2capPacket( packet );
        }
    } else if ( pb == 1 ) {
        if ( m_l2capExpected <= 0 || handle != m_l2capHandle ) return;
        m_l2capBuffer += payload;
        if ( m_l2capBuffer.size() >= m_l2capExpected ) {
            QByteArray packet = m_l2capBuffer.left( m_l2capExpected );
            m_l2capBuffer.clear();
            m_l2capExpected = 0;
            handleL2capPacket( packet );
        }
    }
}

void BleChatClient::handleL2capPacket( const QByteArray& packet )
{
    if ( packet.size() < 4 ) return;
    uint16_t cid = bleRd16( reinterpret_cast<const uint8_t*>( packet.constData() ) + 2 );
    if ( cid != 0x0004 ) return;
    handleAttPdu( packet.mid( 4 ) );
}

void BleChatClient::handleAttPdu( const QByteArray& pdu )
{
    if ( pdu.isEmpty() ) return;
    uint8_t opcode = static_cast<uint8_t>( pdu.at( 0 ) );
    if ( opcode == 0x1B || opcode == 0x1D ) {
        if ( pdu.size() < 3 ) return;
        uint16_t handle = bleRd16( reinterpret_cast<const uint8_t*>( pdu.constData() ) + 1 );
        QByteArray value = pdu.mid( 3 );
        if ( handle == m_valueHandle ) {
            emit notification( value );
            if ( opcode == 0x1D ) {
                QByteArray confirm;
                confirm.append( char( 0x1E ) );
                sendAtt( confirm );
            }
        }
        return;
    }
    if ( !m_pending.active ) return;
    if ( opcode == 0x01 ) {
        if ( pdu.size() < 5 ) {
            m_pending.active = false;
            return;
        }
        uint8_t req = static_cast<uint8_t>( pdu.at( 1 ) );
        uint16_t handle = bleRd16( reinterpret_cast<const uint8_t*>( pdu.constData() ) + 2 );
        uint8_t err = static_cast<uint8_t>( pdu.at( 4 ) );
        (void)handle;
        (void)req;
        if ( err == 0x0A ) {
            if ( m_pending.kind == "services" ) {
                if ( !m_services.isEmpty() ) {
                    m_discSvcIdx = 0;
                    startCharDiscovery();
                } else finishDiscovery( false, "Service not found" );
            } else if ( m_pending.kind == "chars" ) {
                continueCharWalk();
            } else if ( m_pending.kind == "descs" ) {
                if ( m_valueHandle != 0 ) {
                    m_stage = DiscoverDone;
                    setChatReady( true );
                    emit discoveryChanged();
                    emit statusMessage( "Chat ready" );
                } else {
                    finishDiscovery( false, "Descriptor not found" );
                }
            } else if ( m_pending.kind == "read" || m_pending.kind == "write" || m_pending.kind == "subscribe" || m_pending.kind == "unsubscribe" || m_pending.kind == "unswitch" ) {
                m_pending.active = false;
                if ( m_pending.kind == "write" ) emit writeDone( false, "Attribute error" );
                else emit errorMessage( "Attribute error" );
            } else {
                m_pending.active = false;
            }
        } else {
            m_pending.active = false;
            if ( m_pending.kind == "write" ) emit writeDone( false, "Error " + QString::number( err ) );
            else if ( m_stage != DiscoverNone ) finishDiscovery( false, "ATT error " + QString::number( err ) );
            else emit errorMessage( "ATT error " + QString::number( err ) );
        }
        return;
    }
    if ( opcode == 0x11 && m_pending.kind == "services" ) {
        if ( pdu.size() < 2 ) {
            m_pending.active = false;
            return;
        }
        uint8_t entryLen = static_cast<uint8_t>( pdu.at( 1 ) );
        if ( entryLen < 6 || ( pdu.size() - 2 ) % entryLen != 0 ) {
            m_pending.active = false;
            return;
        }
        int count = ( pdu.size() - 2 ) / entryLen;
        uint16_t lastEnd = 0;
        for ( int i = 0; i < count; ++i ) {
            const uint8_t* entry = reinterpret_cast<const uint8_t*>( pdu.constData() ) + 2 + i * entryLen;
            KnownService service;
            service.start = bleRd16( entry );
            service.end = bleRd16( entry + 2 );
            service.uuid = QByteArray( reinterpret_cast<const char*>( entry + 4 ), entryLen - 4 );
            m_services.append( service );
            lastEnd = service.end;
        }
        if ( lastEnd < 0xFFFF && count > 0 ) {
            QByteArray next;
            next.append( char( 0x10 ) );
            next.append( char( ( lastEnd + 1 ) & 0xFF ) );
            next.append( char( ( ( lastEnd + 1 ) >> 8 ) & 0xFF ) );
            next.append( char( 0xFF ) );
            next.append( char( 0xFF ) );
            next.append( char( 0x00 ) );
            next.append( char( 0x28 ) );
            sendAtt( next );
        } else {
            m_discSvcIdx = 0;
            startCharDiscovery();
        }
        return;
    }
    if ( opcode == 0x09 && m_pending.kind == "chars" ) {
        if ( pdu.size() < 2 ) {
            m_pending.active = false;
            return;
        }
        uint8_t entryLen = static_cast<uint8_t>( pdu.at( 1 ) );
        if ( entryLen < 7 || ( pdu.size() - 2 ) % entryLen != 0 ) {
            m_pending.active = false;
            return;
        }
        int count = ( pdu.size() - 2 ) / entryLen;
        uint16_t lastDecl = 0;
        for ( int i = 0; i < count; ++i ) {
            const uint8_t* entry = reinterpret_cast<const uint8_t*>( pdu.constData() ) + 2 + i * entryLen;
            KnownChar chr;
            chr.svcIndex = m_discSvcIdx;
            chr.decl = bleRd16( entry );
            chr.props = entry[2];
            chr.valueHandle = bleRd16( entry + 3 );
            chr.uuid = QByteArray( reinterpret_cast<const char*>( entry + 5 ), entryLen - 5 );
            m_chars.append( chr );
            lastDecl = chr.decl;
        }
        uint16_t svcEnd = 0xFFFF;
        if ( m_discSvcIdx >= 0 && m_discSvcIdx < m_services.size() )
            svcEnd = m_services.at( m_discSvcIdx ).end;
        if ( lastDecl < svcEnd && count > 0 ) {
            QByteArray next;
            next.append( char( 0x08 ) );
            next.append( char( ( lastDecl + 1 ) & 0xFF ) );
            next.append( char( ( ( lastDecl + 1 ) >> 8 ) & 0xFF ) );
            next.append( char( svcEnd & 0xFF ) );
            next.append( char( ( svcEnd >> 8 ) & 0xFF ) );
            next.append( char( 0x03 ) );
            next.append( char( 0x28 ) );
            sendAtt( next );
        } else {
            continueCharWalk();
        }
        return;
    }
    if ( opcode == 0x05 && m_pending.kind == "descs" ) {
        if ( pdu.size() < 2 ) {
            m_pending.active = false;
            return;
        }
        uint8_t format = static_cast<uint8_t>( pdu.at( 1 ) );
        uint16_t lastHandle = 0;
        if ( format == 1 ) {
            if ( ( pdu.size() - 2 ) % 4 != 0 ) {
                m_pending.active = false;
                return;
            }
            int count = ( pdu.size() - 2 ) / 4;
            for ( int i = 0; i < count; ++i ) {
                const uint8_t* entry = reinterpret_cast<const uint8_t*>( pdu.constData() ) + 2 + i * 4;
                uint16_t handle = bleRd16( entry );
                uint16_t uuid = bleRd16( entry + 2 );
                if ( uuid == 0x2902 ) m_cccdHandle = handle;
                lastHandle = handle;
            }
        } else if ( format == 2 ) {
            if ( ( pdu.size() - 2 ) % 18 != 0 ) {
                m_pending.active = false;
                return;
            }
            int count = ( pdu.size() - 2 ) / 18;
            for ( int i = 0; i < count; ++i ) {
                const uint8_t* entry = reinterpret_cast<const uint8_t*>( pdu.constData() ) + 2 + i * 18;
                lastHandle = bleRd16( entry );
            }
        } else {
            m_pending.active = false;
            return;
        }
        if ( m_cccdHandle != 0 ) {
            QByteArray req;
            req.append( char( 0x12 ) );
            req.append( char( m_cccdHandle & 0xFF ) );
            req.append( char( ( m_cccdHandle >> 8 ) & 0xFF ) );
            req.append( char( 0x01 ) );
            req.append( char( 0x00 ) );
            m_pending.opcode = 0x12;
            m_pending.kind = "subscribe";
            m_stage = DiscoverSubscribe;
            sendAtt( req );
        } else if ( lastHandle != 0 && lastHandle < m_descEnd ) {
            uint16_t start = lastHandle + 1;
            QByteArray next;
            next.append( char( 0x04 ) );
            next.append( char( start & 0xFF ) );
            next.append( char( ( start >> 8 ) & 0xFF ) );
            next.append( char( m_descEnd & 0xFF ) );
            next.append( char( ( m_descEnd >> 8 ) & 0xFF ) );
            sendAtt( next );
        } else {
            m_pending.active = false;
            m_stage = DiscoverDone;
            setChatReady( true );
            emit discoveryChanged();
            emit statusMessage( "Chat ready" );
        }
        return;
    }
    if ( opcode == 0x0B && m_pending.kind == "read" ) {
        QByteArray value = pdu.mid( 1 );
        m_pending.active = false;
        emit valueRead( value );
        return;
    }
    if ( opcode == 0x13 ) {
        QString kind = m_pending.kind;
        m_pending.active = false;
        if ( kind == "subscribe" ) {
            m_notifyEnabled = true;
            m_stage = DiscoverDone;
            setChatReady( true );
            emit discoveryChanged();
            emit statusMessage( "Subscribed" );
        } else if ( kind == "unsubscribe" ) {
            m_notifyEnabled = false;
            emit discoveryChanged();
            emit statusMessage( "Unsubscribed" );
        } else if ( kind == "unswitch" ) {
            m_notifyEnabled = false;
            applyTargetAndDiscover();
        } else if ( kind == "write" ) {
            emit writeDone( true, "OK" );
        }
        return;
    }
}
