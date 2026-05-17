/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "e-reactive.h"
#include "analogclock.h"
#include "e-node.h"
#include "e-pin.h"
#include "simulator.h"

eReactive::eReactive( QString id ) : eResistor( id ) {
    m_value = 0;
    m_InitCurr = 0;
    m_InitVolt = 0;

    AnalogClock::self()->addClkElement( this );
}
eReactive::~eReactive() {
    AnalogClock::self()->remClkElement( this );
}

void eReactive::stamp() {
    eResistor::stamp();

    if ( m_ePin[0]->isConnected() && m_ePin[1]->isConnected() ) {
        m_ePin[0]->createCurrent();
        m_ePin[1]->createCurrent();

        updtReactStep();

        m_volt = m_InitVolt;
        m_curSource = m_InitCurr;
        m_curSource = updtCurr();

        if ( m_curSource ) {
            m_ePin[0]->stampCurrent( m_curSource );
            m_ePin[1]->stampCurrent( -m_curSource );
        }
        //m_ePin[0]->changeCallBack( this );
        //m_ePin[1]->changeCallBack( this );
    }
    //m_voltChanged = false;

    updtReactStep();
}

/*void eReactive::voltChanged()
{
    uint64_t time = AnalogClock::self()->eventTime-Simulator::self()->circTime(); // Time to next event;
    time = m_timeStep - time; // Time from last event

    if( time && !m_voltChanged )
    {
        if( m_lastTime ) time -= m_lastTime;
        uint64_t tStep = m_tStep;
        m_tStep = (double)time/1e12;         // Time in seconds
        eResistor::setResistance( updtRes() );
        runEvent();
        m_tStep = tStep;
        m_voltChanged = true;
        m_lastTime += time;
    }
}*/

void eReactive::runEvent() {
    double volt = m_ePin[0]->getVoltage() - m_ePin[1]->getVoltage();

    if ( m_volt != volt ) {
        /*if( m_voltChanged )
        {
            updtReactStep();
            m_voltChanged = false;
            m_lastTime = 0;
        }*/
        m_volt = volt;
        m_curSource = updtCurr();

        m_ePin[0]->stampCurrent( m_curSource );
        m_ePin[1]->stampCurrent( -m_curSource );
    }
}

void eReactive::updtReactStep() {
    m_timeStep = AnalogClock::self()->getStep(); // Time in ps
    m_tStep = (double) m_timeStep / 1e12; // Time in seconds
    eResistor::setResistance( updtRes() );
}
