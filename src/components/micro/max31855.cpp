/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <math.h>

#include <QGraphicsProxyWidget>
#include <QPainter>

#include "circuit.h"
#include "itemlibrary.h"
#include "max31855.h"
#include "simulator.h"
#include "updobutton.h"
#include "utils.h"

#include "boolprop.h"
#include "doubleprop.h"

#define tr( str ) simulideTr( "Max31855", str )

Component* Max31855::construct( QString type, QString id ) {
    return new Max31855( type, id );
}

LibraryItem* Max31855::libraryItem() {
    return new LibraryItem( "MAX31855", "Sensors", "ic2_comp.png", "MAX31855", Max31855::construct );
}

Max31855::Max31855( QString type, QString id )
    : Component( type, id ), SpiModule( id ),
      m_pinCS( 270, QPoint( -16, 36 ), id + "-PinCS", 8, this, input ),
      m_pinDI( 270, QPoint( -8, 36 ), id + "-PinDI", 8, this, input ),
      m_pinCK( 270, QPoint( 0, 36 ), id + "-PinCK", 8, this, input ),
      m_pinDO( 270, QPoint( 8, 36 ), id + "-PinDO", 8, this, output ),
      m_gnd( 270, QPoint( 16, 36 ), id + "-PinGnd", 8, this ) {
    m_graphical = true;
    m_area = QRect( -18, -24, 36, 52 );

    m_pinCS.setLabelText( "CS" );
    m_pinCK.setLabelText( "CLK" );
    m_pinDO.setLabelText( "DO" );

    m_pinCS.setInputHighV( 2.31 );
    m_pinCS.setInputLowV( 0.99 );
    m_pinCK.setInputHighV( 2.31 );
    m_pinCK.setInputLowV( 0.99 );
    m_pinDO.setOutHighV( 3.3 );

    // DI (m_pinDI) is kept only as an internal MOSI input for SpiModule; it is
    // not part of the connectable model and reads low when left open.
    m_pinDI.setEnabled( false );
    m_pinDI.setVisible( false );

    m_pin.resize( 3 );
    m_pin = { &m_pinCS, &m_pinCK, &m_pinDO };
    for ( int i = 0; i < 3; i++ )
        m_pin[i]->setLabelColor( QColor( 250, 250, 200 ) );

    m_gnd.setLabelColor( QColor( 250, 250, 200 ) );
    m_gnd.setLabelText( "Gnd" );

    // SpiModule:
    m_MOSI = &m_pinDI;
    m_MISO = &m_pinDO;
    m_SS = &m_pinCS;
    m_clkPin = &m_pinCK;

    m_useSS = true;

    m_temp = 22;
    m_tempInc = 0.5;
    m_internalTemp = 25;
    m_grounded = false;
    m_oc = false;
    m_scg = false;
    m_scv = false;

    m_byteIndex = 0;

    m_font.setFamily( "Ubuntu Mono" );
    m_font.setPixelSize( 9 );
    m_font.setBold( true );
    m_font.setLetterSpacing( QFont::PercentageSpacing, 100 );
    setLabelPos( -24, -38 );

    UpDoButton* u_button = new UpDoButton( true );
    QGraphicsProxyWidget* proxy = Circuit::self()->addWidget( u_button );
    proxy->setParentItem( this );
    proxy->setPos( QPoint( -23, -26 ) );

    UpDoButton* d_button = new UpDoButton( false );
    proxy = Circuit::self()->addWidget( d_button );
    proxy->setParentItem( this );
    proxy->setPos( QPoint( -23, -21 ) );

    QObject::connect( u_button, &UpDoButton::pressed, [=]() { upbuttonclicked(); } );
    QObject::connect( d_button, &UpDoButton::pressed, [=]() { downbuttonclicked(); } );

    Simulator::self()->addToUpdateList( this );

    buildData();

    addPropGroup(
        { tr( "Main" ),
          {
              new DoubProp<Max31855>( "Temp", tr( "Thermocouple Temp." ), "°C", this, &Max31855::temp,
                                      &Max31855::setTemp ),
              new DoubProp<Max31855>( "TempInc", tr( "Temp. increment" ), "°C", this, &Max31855::tempInc,
                                      &Max31855::setTempInc ),
              new DoubProp<Max31855>( "IntTemp", tr( "Internal Temp." ), "°C", this, &Max31855::internalTemp,
                                      &Max31855::setInternalTemp ),
              new BoolProp<Max31855>( "Grounded", tr( "Grounded" ), "", this, &Max31855::grounded,
                                      &Max31855::setGrounded, propNoCopy ),
                  },
                  0 } );

    addPropGroup( { tr( "Faults" ),
                    {
                        new BoolProp<Max31855>( "OC", tr( "Open Circuit" ), "", this, &Max31855::oc,
                                                &Max31855::setOc ),
                        new BoolProp<Max31855>( "SCG", tr( "Short to GND" ), "", this, &Max31855::scg,
                                                &Max31855::setScg ),
                        new BoolProp<Max31855>( "SCV", tr( "Short to VCC" ), "", this, &Max31855::scv,
                                                &Max31855::setScv ),
                    },
                    0 } );
}
Max31855::~Max31855() { }

void Max31855::stamp() {
    m_byteIndex = 0;
    buildData();
    m_gnd.setUnused( m_grounded );
    m_gnd.setEnabled( !m_grounded );
    m_gnd.setVisible( !m_grounded );
    if ( m_grounded )
        m_gnd.removeConnector();
    SpiModule::setMode( SPI_SLAVE );
}

void Max31855::voltChanged() {
    SpiModule::voltChanged();
}

void Max31855::ssChanged( bool enable ) {
    if ( enable ) { // CS active Low: start a new 32-bit transfer
        m_byteIndex = 0;
        buildData();
        m_srReg = m_sendData[0];
        // Push the fresh MSB to MISO immediately: the base class drove the
        // stale bit at CS fall, and a mode-0 master samples it on the first
        // rising edge. Advance the register so the first falling edge drives D30.
        driveData( ( m_srReg & m_outBit ) > 0 );
        m_srReg <<= 1;
    }
}

void Max31855::endTransaction() {
    SpiModule::endTransaction(); // slave: resetSR()

    m_byteIndex = ( m_byteIndex + 1 ) & 0x3; // 4 bytes per transfer
    m_srReg = m_sendData[m_byteIndex];
}

void Max31855::buildData() {
    uint32_t word = 0;

    int16_t tc = (int16_t) lround( m_temp * 4 ); // 0.25°C resolution, 14-bit signed
    word |= ( (uint32_t) tc & 0x3FFF ) << 18;

    int16_t it = (int16_t) lround( m_internalTemp * 16 ); // 0.0625°C resolution, 12-bit signed
    word |= ( (uint32_t) it & 0x0FFF ) << 4;

    if ( m_oc || m_scg || m_scv )
        word |= 1 << 16; // FAULT

    if ( m_scv )
        word |= 1 << 2;
    if ( m_scg )
        word |= 1 << 1;
    if ( m_oc )
        word |= 1 << 0;

    m_sendData[0] = word >> 24;
    m_sendData[1] = word >> 16;
    m_sendData[2] = word >> 8;
    m_sendData[3] = word;
}

void Max31855::setTemp( double t ) {
    m_temp = t;
    if ( m_temp > 1000 )
        m_temp = 1000;
    if ( m_temp < -200 )
        m_temp = -200;
    buildData();
    update();
}

void Max31855::setGrounded( bool g ) {
    if ( g == m_grounded )
        return;
    m_grounded = g;
    m_gnd.setUnused( g );
    m_gnd.setEnabled( !g );
    m_gnd.setVisible( !g );
    if ( g )
        m_gnd.removeConnector();
    update();
}

void Max31855::setInternalTemp( double t ) {
    m_internalTemp = t;
    if ( m_internalTemp > 125 )
        m_internalTemp = 125;
    if ( m_internalTemp < -40 )
        m_internalTemp = -40;
    buildData();
    update();
}

void Max31855::setOc( bool b ) {
    m_oc = b;
    buildData();
    update();
}

void Max31855::setScg( bool b ) {
    m_scg = b;
    buildData();
    update();
}

void Max31855::setScv( bool b ) {
    m_scv = b;
    buildData();
    update();
}

void Max31855::upbuttonclicked() {
    m_temp += m_tempInc;
    if ( Simulator::self()->isRunning() )
        Simulator::self()->addToUpdateList( this );
    else
        updateStep();
}

void Max31855::downbuttonclicked() {
    m_temp -= m_tempInc;
    if ( Simulator::self()->isRunning() )
        Simulator::self()->addToUpdateList( this );
    else
        updateStep();
}

void Max31855::updateStep() {
    setTemp( m_temp );
    Simulator::self()->remFromUpdateList( this );
}

void Max31855::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) {
    Component::paint( p, o, w );

    p->setBrush( QColor( 20, 30, 60 ) );
    p->drawRoundedRect( QRect( -18, -14, 36, 42 ), 1, 1 );

    p->setOpacity( .6 );
    p->fillRect( QRectF( -18, -24, 36, 8 ), QColor( Qt::white ) );
    p->setOpacity( 1 );

    p->setPen( QColor( Qt::black ) );
    p->setFont( m_font );
    p->drawText( QRectF( -18, -24, 36, 8 ), Qt::AlignCenter, QString::number( m_temp, 'f', 1 ) + "°C" );

    QFont chipFont = m_font;
    chipFont.setPixelSize( 7 );
    p->setPen( QColor( Qt::white ) );
    p->setFont( chipFont );
    p->drawText( QRectF( -17, -12, 34, 16 ), Qt::AlignCenter, "MAX31855" );

    p->setPen( QColor( 80, 95, 125 ) );
    p->drawLine( QPointF( -16, 6 ), QPointF( 16, 6 ) );

    Component::paintSelected( p );
}
