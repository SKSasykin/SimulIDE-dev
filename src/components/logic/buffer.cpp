/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "buffer.h"
#include "circuit.h"
#include "iopin.h"
#include "itemlibrary.h"

#include "boolprop.h"

#define tr( str ) simulideTr( "Buffer", str )

Component* Buffer::construct( QString type, QString id ) {
    return new Buffer( type, id );
}

LibraryItem* Buffer::libraryItem() {
    return new LibraryItem( tr( "Buffer" ), "Gates", "buffer.png", "Buffer", Buffer::construct );
}

Buffer::Buffer( QString type, QString id ) : Gate( type, id, 1 ) {
    m_area = QRect( -8, -8, 16, 16 );
    m_small = false;
    Buffer::updatePath();

    setOePin( new IoPin( 90, QPoint( 0, -12 ), m_id + "-Pin_outEnable", 0, this, input ) );
    Buffer::setTristate( false );

    addPropGroup( { tr( "Main" ),
                    {
                        new BoolProp<Buffer>( "Small", tr( "Small size" ), "", this, &Buffer::isSmall,
                                              &Buffer::setSmall, propNoCopy ),
                    },
                    0 } );

    appendPropGroup( tr( "Main" ), IoComponent::familyProps() );

    addPropGroup(
        { tr( "Inputs" ),
          IoComponent::inputProps()
              + QList<ComProperty*>( { new ComProperty( "", tr( "Invert Input by Right-Click on Pin" ), "", "", 0 ) } ),
          0 } );

    addPropGroup( { tr( "Outputs" ),
                    Gate::outputProps() + IoComponent::outputType()
                        + QList<ComProperty*>(
                            { new BoolProp<Buffer>( "Tristate", tr( "Tristate" ), "", this, &Buffer::tristate,
                                                    &Buffer::setTristate, propNoCopy ),
                              new ComProperty( "", tr( "Invert Output by Right-Click on Pin" ), "", "", 0 ) } ),
                    0 } );

    addPropGroup( { tr( "Timing" ), IoComponent::edgeProps(), 0 } );

    remProperty( "Inverted" );
}
Buffer::~Buffer() { }

void Buffer::setSmall( bool s ) {
    m_small = s;

    if ( s ) {
        m_outPin[0]->setLength( 7 );
        m_outPin[0]->setX( 8 );
        m_oePin->setLength( 9 );
        m_oePin->setX( -4 );
        m_area = QRect( -9, -5, 10, 10 );
    } else {
        m_outPin[0]->setLength( 8 );
        m_outPin[0]->setX( 16 );
        m_oePin->setLength( 8 );
        m_oePin->setX( 0 );
        m_area = QRect( -8, -8, 16, 16 );
    }
    updatePath();
    Circuit::self()->update();
}

void Buffer::setTristate( bool t ) // Activate or deactivate OE Pin
{
    LogicComponent::setTristate( t );
    m_oePin->setLabelText( "" );
}

void Buffer::updatePath() {
    if ( m_small ) {
        m_path = QPainterPath();
        m_path.moveTo( -9, -5.5 );
        m_path.lineTo( -9, 5.5 );
        m_path.lineTo( 1.5, 1 );
        m_path.lineTo( 1.5, -1 );
        m_path.lineTo( -9, -5.5 );
    } else {
        m_path = QPainterPath();
        m_path.moveTo( -8, -8 );
        m_path.lineTo( -8, 8 );
        m_path.lineTo( 8, 1 );
        m_path.lineTo( 8, -1 );
        m_path.lineTo( -8, -8 );
    }
}
