/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "flipflopd.h"
#include "iopin.h"
#include "itemlibrary.h"

#define tr( str ) simulideTr( "FlipFlopD", str )

Component* FlipFlopD::construct( QString type, QString id ) {
    return new FlipFlopD( type, id );
}

LibraryItem* FlipFlopD::libraryItem() {
    return new LibraryItem( tr( "FlipFlopD" ), "Memory", "2to2.png", "FlipFlopD", FlipFlopD::construct );
}

FlipFlopD::FlipFlopD( QString type, QString id ) : FlipFlopBase( type, id ) {
    m_width = 3;
    m_height = 3;
    m_dataPins = 1;

    init( { // Inputs:
            "IL01D", "IU01S", "ID02R", "IL02>",
            // Outputs:
            "OR01Q", "OR02!Q" } );

    m_setPin = m_inpPin[1];
    m_rstPin = m_inpPin[2];
    m_clkPin = m_inpPin[3];

    //setSrInv( true );       // Inver Set & Reset pins
    //setClockInv( false );   // Don't Invert Clock pin
    m_setPin->setInverted( true ); // Set
    m_rstPin->setInverted( true ); // Reset
    setTriggerStr( "Clock" );
}
FlipFlopD::~FlipFlopD() { }

void FlipFlopD::calcOutput() {
    m_nextOutVal = m_inpPin[0]->getInpState() ? 1 : 2; // D state
}
