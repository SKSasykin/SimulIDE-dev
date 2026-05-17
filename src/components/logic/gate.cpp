/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>

#include "circuit.h"
#include "connector.h"
#include "gate.h"
#include "iopin.h"

#include "boolprop.h"

#define tr( str ) simulideTr( "Gate", str )

Gate::Gate( QString type, QString id, int inputs ) : LogicComponent( type, id ) {
    m_width = 2;
    m_initState = false;
    m_minInputs = inputs;

    setNumOuts( 1, "", -1 );
    setNumInputs( inputs ); // Create Input Pins
}
Gate::~Gate() { }

bool Gate::propNotFound( QString prop, QString val ) {
    if ( prop == "Inverted" ) // Old circuits
    {
        m_outPin[0]->setInverted( val == "true" );
        return true;
    }
    return false;
}

QList<ComProperty*> Gate::outputProps() {
    QList<ComProperty*> outProps = IoComponent::outputProps();
    outProps.prepend(
        new BoolProp<Gate>( "initHigh", tr( "Initial High State" ), "", this, &Gate::initHigh, &Gate::setInitHigh ) );

    return outProps;
}

void Gate::stamp() {
    LogicComponent::stamp();
    for ( uint i = 0; i < m_inpPin.size(); ++i )
        m_inpPin[i]->changeCallBack( this );

    m_outPin[0]->setOutState( m_initState );

    m_nextOutVal = m_outValue = m_initState;
}

void Gate::voltChanged() {
    if ( m_tristate )
        LogicComponent::updateOutEnabled();

    int inputs = 0;

    for ( uint i = 0; i < m_inpPin.size(); ++i ) {
        bool state = m_inpPin[i]->getInpState();
        if ( state )
            inputs++;
    }
    bool out = calcOutput( inputs ); // In each gate type

    m_nextOutVal = out ? 1 : 0;

    scheduleOutPuts( this );
}

bool Gate::calcOutput( int inputs ) {
    return ( (uint) inputs == m_inpPin.size() ); // Default for: Buffer, Inverter, And, Nand
}

void Gate::setNumInputs( int inputs ) {
    if ( inputs < m_minInputs )
        return;
    IoComponent::setNumInps( inputs, "" );
    m_outPin[0]->setY( 0 );
    m_area = QRect( -11, -4 * m_inpPin.size(), 19, 4 * 2 * m_inpPin.size() );

    updatePath();
}

QPainterPath Gate::shape() const {
    return m_path;
}

void Gate::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) {
    Component::paint( p, o, w );

    p->drawPath( m_path );

    Component::paintSelected( p );
}
