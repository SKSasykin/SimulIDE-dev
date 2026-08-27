/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "qemubt.h"
#include <QCoreApplication>
#include <cstring>

QemuBt::QemuBt( QemuDevice* mcu, QString name, int n,
                uint64_t memStart, uint64_t memEnd, uint8_t irqSource )
    : QemuModule( mcu, name, n, nullptr, memStart, memEnd )
    , m_irqSource( irqSource )
    , m_backend( new QemuNetBackend() )
    ,     m_rxCount( 0 )
    , m_txCount( 0 )
{
    m_type = "bt";
    m_arena->bt_rx.head = m_arena->bt_rx.tail = 0;
    m_arena->bt_tx.head = m_arena->bt_tx.tail = 0;
}

QemuBt::~QemuBt() { delete m_backend; }

void QemuBt::reset() {
    QemuModule::reset();
    m_arena->bt_rx.head = m_arena->bt_rx.tail = 0;
    m_arena->bt_tx.head = m_arena->bt_tx.tail = 0;
    m_rxCount = m_txCount = 0;
}

void QemuBt::setHostLink( quint16 port ) {
    if( port ) m_backend->setHostLink( port );
    else       m_backend->closeLink();
}

void QemuBt::injectHostFrame( const QByteArray& frame ) {
    if( rxFull() ) return;
    pushRxFrame( (const uint8_t*)frame.constData(), (uint32_t)frame.size() );
    m_rxCount++;
    setInterrupt( m_irqSource, 1 );
}

bool QemuBt::selfTest() {
    if( !m_backend->setHostLink( 14568 ) ) return false;
    QByteArray probe( "SIMULIDE_BT_SELFTEST", 19 );
    m_backend->sendFrame( probe );
    QCoreApplication::processEvents();
    processRx();
    bool ok = ( m_rxCount > 0 ) && rxPending();
    m_backend->closeLink();
    return ok;
}

void QemuBt::processRx() {
    while( m_backend->hasPending() ) {
        QByteArray f = m_backend->takePending();
        if( rxFull() ) break;
        pushRxFrame( (const uint8_t*)f.constData(), (uint32_t)f.size() );
        m_rxCount++;
    }
    if( rxPending() ) setInterrupt( m_irqSource, 1 );
    else              setInterrupt( m_irqSource, 0 );
}

void QemuBt::pumpTx() {
    uint8_t buf[QEMU_WIFI_FRAME_MAX];
    uint32_t len = 0;
    while( pullTxFrame( buf, &len ) ) {
        m_backend->sendFrame( QByteArray( (const char*)buf, (int)len ) );
        m_txCount++;
    }
}

void QemuBt::runAction() {
    QemuModule::runAction();
    processRx();
    pumpTx();
}
