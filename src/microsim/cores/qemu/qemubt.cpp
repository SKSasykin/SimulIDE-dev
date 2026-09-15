/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "qemubt.h"
#include <atomic>
#include <cstring>

namespace {
bool completeControllerFrame( const uint8_t* frame, uint32_t len ) {
    if( !frame || !len || len > QEMU_WIFI_FRAME_MAX ) return false;

    uint32_t packetLen = 0;
    switch( frame[0] ) {
    case 0x02: // ACL data
        if( len < 5 ) return false;
        packetLen = 5u + frame[3] + ( uint32_t( frame[4] ) << 8 );
        break;
    case 0x03: // Synchronous data
        if( len < 4 ) return false;
        packetLen = 4u + frame[3];
        break;
    case 0x04: // HCI event
        if( len < 3 ) return false;
        packetLen = 3u + frame[2];
        break;
    case 0x05: // ISO data
        if( len < 5 ) return false;
        packetLen = 5u + ( ( frame[3] | ( uint32_t( frame[4] ) << 8 ) ) & 0x3FFF );
        break;
    default:
        return false;
    }
    return len == packetLen;
}
}

QemuBt::QemuBt( QemuDevice* mcu, QString name, int n,
                 uint64_t memStart, uint64_t memEnd )
    : QemuModule( mcu, name, n, nullptr, memStart, memEnd )
    , m_rxCount( 0 )
    , m_txCount( 0 )
{
    m_type = "bt";
    m_arena->bt_rx.head = m_arena->bt_rx.tail = 0;
    m_arena->bt_tx.head = m_arena->bt_tx.tail = 0;
}

void QemuBt::reset() {
    QemuModule::reset();
    m_arena->bt_rx.head = m_arena->bt_rx.tail = 0;
    m_arena->bt_tx.head = m_arena->bt_tx.tail = 0;
    m_rxCount = m_txCount = 0;
}

void QemuBt::injectHostFrame( const QByteArray& frame ) {
    if( frame.size() <= 0 || frame.size() > QEMU_WIFI_FRAME_MAX ) return;
    const uint8_t* data = reinterpret_cast<const uint8_t*>( frame.constData() );
    uint32_t len = static_cast<uint32_t>( frame.size() );
    if( completeControllerFrame( data, len ) && pushRxFrame( data, len ) )
        m_rxCount++;
}

bool QemuBt::pushRxFrame( const uint8_t* data, uint32_t len ) {
    if( !data || !len || len > QEMU_WIFI_FRAME_MAX ) return false;

    volatile qemuWifiRing_t* ring = &m_arena->bt_rx;
    uint32_t tail = ring->tail;
    uint32_t head = ring->head;
    std::atomic_thread_fence( std::memory_order_acquire );
    if( tail >= QEMU_WIFI_RING_FRAMES || head >= QEMU_WIFI_RING_FRAMES ) return false;

    uint32_t next = ( tail + 1 ) % QEMU_WIFI_RING_FRAMES;
    if( next == head ) return false;

    qemuWifiFrame_t* output = const_cast<qemuWifiFrame_t*>( &ring->frames[tail] );
    output->len = len;
    memcpy( output->data, data, len );
    ring->seq++;
    std::atomic_thread_fence( std::memory_order_release );
    ring->tail = next;
    return true;
}

void QemuBt::pumpTx() {
    volatile qemuWifiRing_t* ring = &m_arena->bt_tx;
    uint8_t frame[QEMU_WIFI_FRAME_MAX];

    while( true ) {
        uint32_t head = ring->head;
        uint32_t tail = ring->tail;
        if( head >= QEMU_WIFI_RING_FRAMES || tail >= QEMU_WIFI_RING_FRAMES || head == tail ) return;

        std::atomic_thread_fence( std::memory_order_acquire );
        const qemuWifiFrame_t* input = const_cast<const qemuWifiFrame_t*>( &ring->frames[head] );
        uint32_t len = input->len;
        bool bounded = len <= QEMU_WIFI_FRAME_MAX;
        if( bounded ) memcpy( frame, input->data, len );

        uint8_t response[7];
        uint32_t responseLen = 0;
        if( bounded && len >= 4 && frame[0] == 0x01 ) {
            uint32_t packetLen = 4u + frame[3];
            if( len == packetLen ) {
                uint16_t opcode = uint16_t( frame[1] ) | ( uint16_t( frame[2] ) << 8 );
                uint8_t status = 0x01; // Unknown HCI Command
                if( opcode == 0x0C03 ) status = frame[3] == 0 ? 0x00 : 0x12;

                response[0] = 0x04;
                response[1] = 0x0E;
                response[2] = 0x04;
                response[3] = 0x01;
                response[4] = frame[1];
                response[5] = frame[2];
                response[6] = status;
                responseLen = sizeof( response );
            }
        }

        // Preserve a valid command until QEMU has room for its completion event.
        if( responseLen && !pushRxFrame( response, responseLen ) ) return;
        if( responseLen ) m_rxCount++;

        std::atomic_thread_fence( std::memory_order_release );
        ring->head = ( head + 1 ) % QEMU_WIFI_RING_FRAMES;
        m_txCount++;
    }
}

void QemuBt::runAction() {
    pumpTx();
}
