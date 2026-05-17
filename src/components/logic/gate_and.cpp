/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "gate_and.h"
#include "itemlibrary.h"

#include "boolprop.h"
#include "intprop.h"

#define tr( str ) simulideTr( "AndGate", str )

Component* AndGate::construct( QString type, QString id ) {
    return new AndGate( type, id );
}

LibraryItem* AndGate::libraryItem() {
    return new LibraryItem( tr( "And Gate" ), "Gates", "andgate.png", "And Gate", AndGate::construct );
}

AndGate::AndGate( QString type, QString id ) : Gate( type, id, 2 ) {
    AndGate::updatePath();

    addPropGroup( { tr( "Main" ), IoComponent::familyProps(), 0 } );

    addPropGroup( { tr( "Inputs" ),
                    IoComponent::inputProps()
                        + QList<ComProperty*>(
                            { new IntProp<AndGate>( "Num_Inputs", tr( "Input Size" ), "", this, &AndGate::numInps,
                                                    &AndGate::setNumInputs, propNoCopy, "uint" ),

                              new BoolProp<AndGate>( "Invert_Inputs", tr( "Invert Inputs" ), "", this,
                                                     &AndGate::invertInps, &AndGate::setInvertInps, propNoCopy ) } ),
                    0 } );

    addPropGroup( { tr( "Outputs" ),
                    Gate::outputProps() + IoComponent::outputType()
                        + QList<ComProperty*>(
                            { new ComProperty( "", tr( "Invert Output by Right-Click on Pin" ), "", "", 0 ) } ),
                    0 } );

    addPropGroup( { tr( "Timing" ), IoComponent::edgeProps(), 0 } );

    remProperty( "Inverted" );
}
AndGate::~AndGate() { }

void AndGate::updatePath() {
    int endY = m_area.height() / 2;
    int endX = m_area.width() / 2;

    m_path = QPainterPath();
    m_path.moveTo( -9, -endY );
    m_path.lineTo( -4, -endY );
    m_path.quadTo( QPoint( endX, -endY ), QPoint( endX, 0 ) );
    m_path.quadTo( QPoint( endX, endY ), QPoint( -4, endY ) );
    m_path.lineTo( -9, endY );
    m_path.lineTo( -9, -endY );
}
