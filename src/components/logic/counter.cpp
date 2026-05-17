/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "counter.h"
#include "iopin.h"
#include "itemlibrary.h"

#include "boolprop.h"
#include "intprop.h"

#define tr( str ) simulideTr( "Counter", str )

Component* Counter::construct( QString type, QString id ) {
    return new Counter( type, id );
}

LibraryItem* Counter::libraryItem() {
    return new LibraryItem( tr( "Simple Counter" ), "Arithmetic", "2to1.png", "Counter", Counter::construct );
}

Counter::Counter( QString type, QString id ) : LogicComponent( type, id ) {
    m_TopValue = 1;
    m_width = 3;
    m_height = 3;

    init( { // Inputs:
            "IL01>", "IL02R", "IU01S",
            // Outputs:
            "OR01Q" } );

    m_clkPin = m_inpPin[0]; // eClockedDevice
    m_rstPin = m_inpPin[1];
    m_setPin = m_inpPin[2];

    //setSrInv( true );            // Invert Reset Pin
    m_rstPin->setInverted( true );
    m_setPin->setInverted( true );
    useSetPin( false ); // Don't use Set Pin

    addPropGroup( { tr( "Main" ),
                    {
                        new BoolProp<Counter>( "Pin_SET", tr( "Use Set Pin" ), "", this, &Counter::pinSet,
                                               &Counter::useSetPin, propNoCopy ),

                        //new BoolProp<Counter>("Clock_Inverted", tr("Clock Inverted"),""
                        //                        , this, &Counter::clockInv, &Counter::setClockInv ),

                        //new BoolProp<Counter>("Reset_Inverted", tr("Set/Reset Inverted"),""
                        //                        , this, &Counter::srInv, &Counter::setSrInv ),

                        new IntProp<Counter>( "Max_Value", tr( "Count to" ), "", this, &Counter::maxVal,
                                              &Counter::setMaxVal, 0, "uint" ),
                    },
                    groupNoCopy } );

    appendPropGroup( tr( "Main" ), IoComponent::familyProps() );

    addPropGroup( { tr( "Inputs" ), IoComponent::inputProps(), 0 } );
    addPropGroup( { tr( "Outputs" ), IoComponent::outputProps(), 0 } );
    addPropGroup( { tr( "Timing" ), IoComponent::edgeProps(), 0 } );
}
Counter::~Counter() { }

bool Counter::propNotFound( QString prop, QString val ) {
    if ( prop == "Clock_Inverted" ) // Old circuits
    {
        m_clkPin->setInverted( val == "true" );
    } else if ( prop == "Reset_Inverted" ) // Old circuits
    {
        bool invert = ( val == "true" );

        m_setPin->setInverted( invert );
        m_rstPin->setInverted( invert );
    } else
        return false;

    return true;
}

void Counter::stamp() {
    m_Counter = 0;
    m_rstPin->changeCallBack( this );
    m_setPin->changeCallBack( this );
    LogicComponent::stamp();
}

void Counter::voltChanged() {
    updateClock();
    bool clkRising = ( m_clkState == Clock_Rising );

    if ( m_rstPin->getInpState() ) // Reset
    {
        m_Counter = 0;
        m_nextOutVal = 0;
    } else if ( m_pinSet && m_setPin->getInpState() ) // Set
    {
        m_Counter = m_TopValue;
        m_nextOutVal = 1;
    } else if ( clkRising ) {
        m_Counter++;

        if ( m_Counter == m_TopValue )
            m_nextOutVal = 1;
        else if ( m_Counter > m_TopValue ) {
            m_Counter = 0;
            m_nextOutVal = 0;
        }
    }
    IoComponent::scheduleOutPuts( this );
}

/*void Counter::setSrInv( bool inv )
{
    m_resetInv = inv;
    m_rstPin->setInverted( inv );
    m_setPin->setInverted( inv );
}*/

void Counter::useSetPin( bool set ) {
    m_pinSet = set;
    if ( !set )
        m_setPin->removeConnector();
    m_setPin->setVisible( set );
}
