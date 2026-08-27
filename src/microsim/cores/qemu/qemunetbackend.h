/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QQueue>
#include <QByteArray>
#include <QHostAddress>

class QemuNetBackend : public QObject {
    Q_OBJECT
public:
    explicit QemuNetBackend( QObject* parent = nullptr );
    ~QemuNetBackend();

    bool setHostLink( quint16 localPort, const QString& peerHost = "127.0.0.1", quint16 peerPort = 0 );
    void closeLink();

    void sendFrame( const QByteArray& frame );

    bool hasPending() const;
    QByteArray takePending();

    quint64 rxCount() const { return m_rxCount; }
    quint64 txCount() const { return m_txCount; }
    bool linked() const { return m_linked; }

private slots:
    void onReadyRead();

private:
    QUdpSocket*   m_sock;
    QQueue<QByteArray> m_in;
    QHostAddress  m_peerHost;
    quint16       m_peerPort;
    bool          m_linked;
    quint64       m_rxCount;
    quint64       m_txCount;
};
