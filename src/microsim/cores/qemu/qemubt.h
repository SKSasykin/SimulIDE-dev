/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef QEMUBT_H
#define QEMUBT_H

#include "qemumodule.h"
#include "qemunetbackend.h"
#include <QByteArray>
#include <cstring>

class QemuBt : public QemuModule {
public:
    QemuBt( QemuDevice* mcu, QString name, int n,
            uint64_t memStart, uint64_t memEnd,
            uint8_t irqSource = 0 );
    ~QemuBt();

    void reset() override;
    void runAction() override;

    void setHostLink( quint16 port ) override;

    void injectHostFrame( const QByteArray& frame );

    bool selfTest();

    uint64_t rxCount() const { return m_rxCount; }
    uint64_t txCount() const { return m_txCount; }

private:
    bool rxFull() const {
        return ( ( m_arena->bt_rx.tail + 1 ) % QEMU_WIFI_RING_FRAMES )
               == m_arena->bt_rx.head;
    }
    bool rxPending() const {
        return m_arena->bt_rx.head != m_arena->bt_rx.tail;
    }
    void pushRxFrame( const uint8_t* data, uint32_t len ) {
        uint32_t idx = m_arena->bt_rx.tail;
        qemuWifiFrame_t* f = (qemuWifiFrame_t*)&m_arena->bt_rx.frames[idx];
        if( len > QEMU_WIFI_FRAME_MAX ) len = QEMU_WIFI_FRAME_MAX;
        f->len = len;
        memcpy( f->data, data, len );
        m_arena->bt_rx.tail = ( idx + 1 ) % QEMU_WIFI_RING_FRAMES;
    }
    bool pullTxFrame( uint8_t* buf, uint32_t* len ) {
        if( m_arena->bt_tx.head == m_arena->bt_tx.tail ) return false;
        uint32_t idx = m_arena->bt_tx.head;
        qemuWifiFrame_t* f = (qemuWifiFrame_t*)&m_arena->bt_tx.frames[idx];
        *len = f->len;
        memcpy( buf, f->data, f->len );
        m_arena->bt_tx.head = ( idx + 1 ) % QEMU_WIFI_RING_FRAMES;
        return true;
    }

    void processRx();
    void pumpTx();

    uint8_t         m_irqSource;
    QemuNetBackend* m_backend;
    uint64_t        m_rxCount;
    uint64_t        m_txCount;
};

#endif
