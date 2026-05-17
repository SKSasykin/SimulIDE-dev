/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "flipfloprs.h"
#include "iopin.h"
#include "itemlibrary.h"
#include "simulator.h"

#define tr( str ) simulideTr( "FlipFlopRS", str )

Component* FlipFlopRS::construct( QString type, QString id ) {
    return new FlipFlopRS( type, id );
}

LibraryItem* FlipFlopRS::libraryItem() {
    return new LibraryItem( tr( "FlipFlop RS" ), "Memory", "2to2.png", "FlipFlopRS", FlipFlopRS::construct );
}

FlipFlopRS::FlipFlopRS( QString type, QString id ) : FlipFlopBase( type, id ) {
    m_width = 3;
    m_height = 4;

    init( {
        // Inputs:
        "IL01S",
        "IL03R",
        "IL02>",
        // Outputs:
        "OR01Q",
        "OR03!Q",
    } );

    m_setPin = m_inpPin[0];
    m_rstPin = m_inpPin[1];
    m_clkPin = m_inpPin[2];

    //setSrInv( true );       // Inver Set & Reset pins
    //setClockInv( false );   // Don't Invert Clock pin
    m_setPin->setInverted( true ); // Set
    m_rstPin->setInverted( true ); // Reset
    setTriggerStr( "Clock" );

    remProperty( "UseRS" );
}
FlipFlopRS::~FlipFlopRS() { }

void FlipFlopRS::voltChanged() {
    updateClock();
    bool clkAllow = ( m_clkState == Clock_Allow ); // Get Clk to don't miss any clock changes
    if ( !clkAllow )
        return;

    bool set = sPinState();
    bool reset = rPinState();

    if ( set || reset ) {
        m_nextOutVal = ( set ? 1 : 0 ) + ( reset ? 2 : 0 );
        scheduleOutPuts( this );
    }
}
