/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "dectobcd.h"
#include "circuit.h"
#include "iopin.h"
#include "itemlibrary.h"

#include "boolprop.h"

#define tr( str ) simulideTr( "DecToBcd", str )

Component* DecToBcd::construct( QString type, QString id ) {
    return new DecToBcd( type, id );
}

LibraryItem* DecToBcd::libraryItem() {
    return new LibraryItem( tr( "Encoder(10/16 to 4)" ), "Converters", "3to2g.png", "DecToBcd", DecToBcd::construct );
}

DecToBcd::DecToBcd( QString type, QString id ) : LogicComponent( type, id ) {
    m_width = 4;
    m_height = 10;

    m_tristate = true;
    m_16Bits = false;
    m_bits = 10;

    QStringList pinList;

    pinList // Outputs:
        << "OR03 A" << "OR04 B" << "OR05 C" << "OR06 D";
    init( pinList );

    setNumInps( 10, "D", 1 );

    createOePin( "IU03OE ", id + "-in15" ); // Output Enable

    addPropGroup( { tr( "Main" ),
                    {
                        new BoolProp<DecToBcd>( "_16_Bits", tr( "16 Bits" ), "", this, &DecToBcd::is16Bits,
                                                &DecToBcd::set16bits, propNoCopy ),
                    },
                    groupNoCopy } );

    appendPropGroup( tr( "Main" ), IoComponent::familyProps() );

    addPropGroup( { tr( "Inputs" ),
                    IoComponent::inputProps()
                        + QList<ComProperty*>(
                            { new BoolProp<DecToBcd>( "Invert_Inputs", tr( "Invert Inputs" ), "", this,
                                                      &DecToBcd::invertInps, &DecToBcd::setInvertInps, propNoCopy ) } ),
                    0 } );

    addPropGroup( { tr( "Outputs" ),
                    IoComponent::outputProps() + IoComponent::outputType()
                        + QList<ComProperty*>( {
                            new BoolProp<DecToBcd>( "Tristate", tr( "Tristate" ), "", this, &DecToBcd::tristate,
                                                    &DecToBcd::setTristate ),
                        } ),
                    0 } );

    addPropGroup( { tr( "Timing" ), IoComponent::edgeProps(), 0 } );
}
DecToBcd::~DecToBcd() { }

void DecToBcd::stamp() {
    for ( uint i = 0; i < m_inpPin.size(); ++i )
        m_inpPin[i]->changeCallBack( this );
    LogicComponent::stamp();
}

void DecToBcd::voltChanged() {
    if ( m_tristate )
        LogicComponent::updateOutEnabled();

    int i;
    for ( i = m_bits - 2; i >= 0; --i ) {
        if ( m_inpPin[i]->getInpState() )
            break;
    }
    m_nextOutVal = i + 1;
    scheduleOutPuts( this );
}

void DecToBcd::set16bits( bool set ) {
    m_bits = set ? 16 : 10;
    m_16Bits = set;
    setNumInps( m_16Bits ? 16 : 10, "D", 1 );
    m_oePin->setY( m_area.y() - 8 );
}
