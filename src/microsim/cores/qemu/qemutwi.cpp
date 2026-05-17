/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QDebug>

#include "iopin.h"
#include "qemudevice.h"
#include "qemutwi.h"
#include "simulator.h"

QemuTwi::QemuTwi( QemuDevice* mcu, QString name, int n, uint32_t* clk, uint64_t memStart, uint64_t memEnd )
    : QemuModule( mcu, name, n, clk, memStart, memEnd ), TwiModule( name ) { }
QemuTwi::~QemuTwi() { }

void QemuTwi::setMode( twiMode_t mode ) {
    //qDebug() << "QemuTwi::setMode" << mode << (m_scl && m_sda);
    if ( m_scl && m_sda )
        TwiModule::setMode( mode );
}
