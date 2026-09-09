/***************************************************************************
 *   Copyright (C) 2026 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <math.h>

#include <QGraphicsProxyWidget>
#include <QPainter>

#include "circuit.h"
#include "circuitwidget.h"
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
      m_tcPlus( 90, QPoint( -8, -32 ), id + "-PinTCPlus", 8, this, input ),
      m_tcMinus( 90, QPoint( 8, -32 ), id + "-PinTCMinus", 8, this, input ),
      m_gnd( 270, QPoint( 16, 36 ), id + "-PinGnd", 8, this ) {
    m_graphical = true;
    m_area = QRect( -18, -24, 36, 52 );

    m_pinCS.setLabelText( "CS" );
    m_pinCK.setLabelText( "CLK" );
    m_pinDO.setLabelText( "DO" );
    m_tcPlus.setLabelText( "+" );
    m_tcPlus.setLabelOffset( QPointF( -4, -8 ) );
    m_tcMinus.setLabelText( "-" );
    m_tcMinus.setLabelOffset( QPointF( -4, -8 ) );

    m_pinCS.setInputHighV( 2.31 );
    m_pinCS.setInputLowV( 0.99 );
    m_pinCK.setInputHighV( 2.31 );
    m_pinCK.setInputLowV( 0.99 );
    m_pinDO.setOutHighV( 3.3 );

    m_tcPlus.setUnused( true );
    m_tcPlus.setEnabled( false );
    m_tcPlus.setVisible( false );
    m_tcMinus.setUnused( true );
    m_tcMinus.setEnabled( false );
    m_tcMinus.setVisible( false );

    // DI (m_pinDI) is kept only as an internal MOSI input for SpiModule; it is
    // not part of the connectable model and reads low when left open.
    m_pinDI.setEnabled( false );
    m_pinDI.setVisible( false );

    m_pin.resize( 5 );
    m_pin = { &m_pinCS, &m_pinCK, &m_pinDO, &m_tcPlus, &m_tcMinus };
    for ( int i = 0; i < 5; i++ )
        m_pin[i]->setLabelColor( QColor( 250, 250, 200 ) );
    m_tcPlus.setLabelColor( Qt::black );
    m_tcMinus.setLabelColor( Qt::black );

    m_gnd.setLabelColor( QColor( 250, 250, 200 ) );
    m_gnd.setLabelText( "Gnd" );

    // SpiModule:
    m_MOSI = &m_pinDI;
    m_MISO = &m_pinDO;
    m_SS = &m_pinCS;
    m_clkPin = &m_pinCK;

    m_useSS = true;

    m_temp = 22;
    m_externalTemp = 22;
    m_tempInc = 0.5;
    m_internalTemp = 25;
    m_grounded = false;
    m_externalThermocouple = false;
    m_guiDirty = true;
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
    m_upButtonProxy = Circuit::self()->addWidget( u_button );
    m_upButtonProxy->setParentItem( this );
    m_upButtonProxy->setPos( QPoint( -23, -26 ) );

    UpDoButton* d_button = new UpDoButton( false );
    m_downButtonProxy = Circuit::self()->addWidget( d_button );
    m_downButtonProxy->setParentItem( this );
    m_downButtonProxy->setPos( QPoint( -23, -21 ) );

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
              new BoolProp<Max31855>( "ExternalTC", tr( "External thermocouple" ), "", this,
                                      &Max31855::externalThermocouple, &Max31855::setExternalThermocouple,
                                      propNoCopy ),
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
    m_tcPlus.changeCallBack( this, m_externalThermocouple );
    m_tcMinus.changeCallBack( this, m_externalThermocouple );
    if ( m_externalThermocouple )
        m_externalTemp = readThermocoupleTemp();
    SpiModule::setMode( SPI_SLAVE );
}

void Max31855::voltChanged() {
    SpiModule::voltChanged();
    updateExternalTemp();
}

void Max31855::ssChanged( bool enable ) {
    if ( enable ) { // CS active Low: start a new 32-bit transfer
        m_byteIndex = 0;
        if ( m_externalThermocouple )
            m_externalTemp = readThermocoupleTemp();
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

    const double temperature = m_externalThermocouple ? m_externalTemp : m_temp;
    int16_t tc = (int16_t) lround( temperature * 4 ); // 0.25°C resolution, 14-bit signed
    word |= ( (uint32_t) tc & 0x3FFF ) << 18;

    int16_t it = (int16_t) lround( m_internalTemp * 16 ); // 0.0625°C resolution, 12-bit signed
    word |= ( (uint32_t) it & 0x0FFF ) << 4;

    const bool openCircuit = m_oc || ( m_externalThermocouple
                                       && ( !m_tcPlus.isConnected() || !m_tcMinus.isConnected() ) );
    if ( openCircuit || m_scg || m_scv )
        word |= 1 << 16; // FAULT

    if ( m_scv )
        word |= 1 << 2;
    if ( m_scg )
        word |= 1 << 1;
    if ( openCircuit )
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

void Max31855::setExternalThermocouple( bool external ) {
    if ( external == m_externalThermocouple )
        return;
    if ( Simulator::self()->isRunning() )
        CircuitWidget::self()->powerCircOff();

    m_externalThermocouple = external;
    m_tcPlus.setUnused( !external );
    m_tcPlus.setEnabled( external );
    m_tcPlus.setVisible( external );
    m_tcMinus.setUnused( !external );
    m_tcMinus.setEnabled( external );
    m_tcMinus.setVisible( external );
    m_upButtonProxy->setVisible( !external );
    m_downButtonProxy->setVisible( !external );
    if ( external )
        m_externalTemp = readThermocoupleTemp();
    else {
        m_tcPlus.removeConnector();
        m_tcMinus.removeConnector();
    }
    if ( external )
        Simulator::self()->addToUpdateList( this );
    else
        Simulator::self()->remFromUpdateList( this );
    m_guiDirty = true;
    buildData();
    update();
}

double Max31855::readThermocoupleTemp() {
    double temperature = m_internalTemp + ( m_tcPlus.getVoltage() - m_tcMinus.getVoltage() ) / 41e-6;
    if ( temperature > 1000 )
        temperature = 1000;
    if ( temperature < -200 )
        temperature = -200;
    return temperature;
}

void Max31855::updateExternalTemp() {
    if ( !m_externalThermocouple )
        return;
    const double temperature = readThermocoupleTemp();
    if ( fabs( temperature - m_externalTemp ) < 1e-9 )
        return;
    m_externalTemp = temperature;
    buildData();
    m_guiDirty = true;
}

void Max31855::setInternalTemp( double t ) {
    m_internalTemp = t;
    if ( m_internalTemp > 125 )
        m_internalTemp = 125;
    if ( m_internalTemp < -40 )
        m_internalTemp = -40;
    if ( m_externalThermocouple )
        m_externalTemp = readThermocoupleTemp();
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
    if ( m_externalThermocouple ) {
        if ( m_guiDirty ) {
            m_guiDirty = false;
            update();
        }
        return;
    }
    setTemp( m_temp );
    Simulator::self()->remFromUpdateList( this );
}

void Max31855::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) {
    Component::paint( p, o, w );

    p->setBrush( QColor( 20, 30, 60 ) );
    QRect bodyRect( -18, -14, 36, 42 );
    if ( m_externalThermocouple )
        bodyRect.setTop( -24 );
    p->drawRoundedRect( bodyRect, 1, 1 );

    p->setOpacity( .6 );
    p->fillRect( QRectF( -18, -24, 36, 8 ), QColor( Qt::white ) );
    p->setOpacity( 1 );

    p->setPen( QColor( Qt::black ) );
    p->setFont( m_font );
    const double temperature = m_externalThermocouple ? m_externalTemp : m_temp;
    p->drawText( QRectF( -18, -24, 36, 8 ), Qt::AlignCenter, QString::number( temperature, 'f', 1 ) + "°C" );

    QFont chipFont = m_font;
    chipFont.setPixelSize( 7 );
    p->setPen( QColor( Qt::white ) );
    p->setFont( chipFont );
    p->drawText( QRectF( -17, -12, 34, 16 ), Qt::AlignCenter, "MAX31855" );

    p->setPen( QColor( 80, 95, 125 ) );
    p->drawLine( QPointF( -16, 6 ), QPointF( 16, 6 ) );

    Component::paintSelected( p );
}
