/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "qemunetbackend.h"
#include <QNetworkDatagram>

QemuNetBackend::QemuNetBackend( QObject* parent )
    : QObject( parent )
    , m_sock( new QUdpSocket( this ) )
    , m_peerPort( 0 )
    , m_linked( false )
    , m_rxCount( 0 )
    , m_txCount( 0 )
{
    connect( m_sock, &QUdpSocket::readyRead, this, &QemuNetBackend::onReadyRead );
}

QemuNetBackend::~QemuNetBackend() { closeLink(); }

bool QemuNetBackend::setHostLink( quint16 localPort, const QString& peerHost, quint16 peerPort )
{
    m_peerHost = QHostAddress( peerHost );
    m_peerPort = peerPort ? peerPort : localPort;
    bool ok = m_sock->bind( QHostAddress::AnyIPv4, localPort,
                            QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint );
    m_linked = ok;
    return ok;
}

void QemuNetBackend::closeLink()
{
    if( m_sock ) m_sock->close();
    m_linked = false;
    m_in.clear();
}

void QemuNetBackend::sendFrame( const QByteArray& frame )
{
    if( !m_linked || !m_sock ) return;
    qint64 w = m_sock->writeDatagram( frame, m_peerHost, m_peerPort );
    if( w > 0 ) m_txCount++;
}

bool QemuNetBackend::hasPending() const
{
    return !m_in.isEmpty();
}

QByteArray QemuNetBackend::takePending()
{
    if( m_in.isEmpty() ) return QByteArray();
    m_rxCount++;
    return m_in.dequeue();
}

void QemuNetBackend::onReadyRead()
{
    if( !m_sock ) return;
    while( m_sock->hasPendingDatagrams() )
    {
        QNetworkDatagram d = m_sock->receiveDatagram();
        m_in.enqueue( d.data() );
    }
}
