/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef QEMUBT_H
#define QEMUBT_H

#include "qemumodule.h"
#include <QByteArray>

class QemuBt : public QemuModule {
public:
    QemuBt( QemuDevice* mcu, QString name, int n,
            uint64_t memStart, uint64_t memEnd );

    void reset() override;
    void runAction() override;

    void injectHostFrame( const QByteArray& frame );

    uint64_t rxCount() const { return m_rxCount; }
    uint64_t txCount() const { return m_txCount; }

private:
    bool pushRxFrame( const uint8_t* data, uint32_t len );
    void pumpTx();

    uint64_t m_rxCount;
    uint64_t m_txCount;
};

#endif
