/***************************************************************************
 *   Copyright (C) 2026 by SimulIDE developers                             *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <algorithm>
#include <cmath>

#include <QPainter>

#include "acvoltage.h"
#include "boolprop.h"
#include "custombutton.h"
#include "doubleprop.h"
#include "iopin.h"
#include "itemlibrary.h"
#include "simulator.h"

#define tr( str ) simulideTr( "AcVoltage", str )

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double psPerSecond = 1e12;
constexpr double maxEventStep = 1e6;
constexpr int samplesPerCycle = 100;
constexpr double maxFrequency = psPerSecond / samplesPerCycle;
}

Component* AcVoltage::constructFixed( QString type, QString id ) {
    return new AcVoltage( type, id );
}

Component* AcVoltage::constructRail( QString type, QString id ) {
    return new AcVoltage( type, id );
}

LibraryItem* AcVoltage::fixedLibraryItem() {
    return new LibraryItem( tr( "Fixed AC Voltage" ), "Sources", "acvoltage.svg", "Fixed AC Voltage",
                            AcVoltage::constructFixed );
}

LibraryItem* AcVoltage::railLibraryItem() {
    return new LibraryItem( tr( "AC Rail" ), "Sources", "acrail.svg", "AC Rail", AcVoltage::constructRail );
}

AcVoltage::AcVoltage( QString type, QString id ) : ClockBase( type, id ) {
    m_isRail = type == "AC Rail";
    m_voltageRms = 220;
    m_eventStep = 0;
    m_eventRemainder = 0;
    m_eventStepInt = 0;

    remPropGroup( simulideTr( "FixedVolt", "Main" ) );

    if ( m_isRail ) {
        addPropGroup( { tr( "Main" ),
                        { new DoubProp<AcVoltage>( "Voltage_RMS", tr( "Voltage RMS" ), "V", this,
                                                   &AcVoltage::voltageRms, &AcVoltage::setVoltageRms ),
                          new DoubProp<AcVoltage>( "Freq", tr( "Frequency" ), "Hz", this, &AcVoltage::freq,
                                                   &AcVoltage::setFreq ) },
                        0 } );

        m_area = QRect( -2, -8, 12, 16 );
        m_button->setVisible( false );
        m_proxy->setVisible( false );
        setRotation( 90 );
        setValLabelPos( -12, 6, -90 );
        setLabelPos( -5, -10, -90 );
        m_alwaysOn = true;
        setRunning( true );
    } else {
        addPropGroup( { tr( "Main" ),
                        { new DoubProp<AcVoltage>( "Voltage_RMS", tr( "Voltage RMS" ), "V", this,
                                                   &AcVoltage::voltageRms, &AcVoltage::setVoltageRms ),
                          new DoubProp<AcVoltage>( "Freq", tr( "Frequency" ), "Hz", this, &AcVoltage::freq,
                                                   &AcVoltage::setFreq ),
                          new BoolProp<FixedVolt>( "Small", tr( "Small size" ), "", this, &FixedVolt::isSmall,
                                                   &FixedVolt::setSmall ) },
                        0 } );
    }

    setShowProp( "Voltage_RMS" );
    setPropStr( "Voltage_RMS", "220 V" );
    setPropStr( "Freq", "50 Hz" );
}

AcVoltage::~AcVoltage() {
    Simulator::self()->cancelEvents( this );
}

void AcVoltage::setup() {
    Component::setup();
    if ( m_isRail ) {
        m_alwaysOn = true;
        setRunning( true );
    }
}

void AcVoltage::initialize() {
    Simulator::self()->cancelEvents( this );

    if ( m_isRail )
        m_isRunning = m_freq > 0;

    m_eventRemainder = 0;
    m_outpin->setVoltage( 0 );
    if ( m_isRunning && m_eventStepInt > 0 )
        Simulator::self()->addEvent( m_eventStepInt, this );
}

void AcVoltage::stamp() {
    m_outpin->setImpedance( low_imp );
    setOutputVoltage();
}

void AcVoltage::runEvent() {
    if ( !m_isRunning || m_freq <= 0 ) {
        m_outpin->setVoltage( 0 );
        return;
    }
    setOutputVoltage();

    uint64_t nextEvent = m_eventStepInt;
    m_eventRemainder += m_eventStep - m_eventStepInt;
    if ( m_eventRemainder >= 1 ) {
        const uint64_t remainder = m_eventRemainder;
        nextEvent += remainder;
        m_eventRemainder -= remainder;
    }
    Simulator::self()->addEvent( nextEvent, this );
}

void AcVoltage::updateStep() {
    if ( !m_changed )
        return;
    m_changed = false;
    update();
}

void AcVoltage::setVoltageRms( double voltage ) {
    const bool wasPaused = Simulator::self()->isPaused();
    const bool resume = Simulator::self()->isRunning() && !wasPaused;
    if ( resume )
        Simulator::self()->pauseSim();
    m_voltageRms = std::abs( voltage );
    setOutputVoltage();
    if ( resume )
        Simulator::self()->resumeSim();
}

void AcVoltage::setFreq( double frequency ) {
    const bool wasPaused = Simulator::self()->isPaused();
    const bool simulationActive = Simulator::self()->isRunning();
    const bool resume = simulationActive && !wasPaused;
    if ( resume )
        Simulator::self()->pauseSim();

    m_freq = std::max( 0.0, std::min( frequency, maxFrequency ) );
    m_remainder = 0;
    if ( m_freq > 0 ) {
        m_psPerCycleDbl = psPerSecond / m_freq;
        m_psPerCycleInt = m_psPerCycleDbl;
        m_eventStep = std::max( std::min( m_psPerCycleDbl / samplesPerCycle, maxEventStep ), 1.0 );
        m_eventStepInt = m_eventStep;
    } else {
        m_psPerCycleDbl = 0;
        m_psPerCycleInt = 0;
        m_eventStep = 0;
        m_eventStepInt = 0;
    }

    setRunning( m_isRail || m_isRunning );
    if ( simulationActive )
        initialize();
    if ( resume )
        Simulator::self()->resumeSim();
}

void AcVoltage::onbuttonclicked() {
    const bool wasPaused = Simulator::self()->isPaused();
    const bool simulationActive = Simulator::self()->isRunning();
    const bool resume = simulationActive && !wasPaused;
    if ( resume )
        Simulator::self()->pauseSim();
    setRunning( !m_isRunning );
    if ( simulationActive )
        initialize();
    else if ( !m_isRunning )
        m_outpin->setVoltage( 0 );
    if ( resume )
        Simulator::self()->resumeSim();
}

void AcVoltage::setOutputVoltage() {
    if ( !m_isRunning || m_freq <= 0 || m_psPerCycleDbl <= 0 ) {
        m_outpin->setVoltage( 0 );
        return;
    }

    const double time = std::fmod( Simulator::self()->circTime(), m_psPerCycleDbl );
    const double peakVoltage = m_voltageRms * std::sqrt( 2.0 );
    m_outpin->setVoltage( peakVoltage * std::sin( 2 * pi * time / m_psPerCycleDbl ) );
}

QPainterPath AcVoltage::shape() const {
    if ( !m_isRail )
        return Component::shape();

    QPainterPath path;
    QVector<QPointF> points;
    points << QPointF( -4, -8 ) << QPointF( -4, 8 ) << QPointF( 8, 1 ) << QPointF( 8, -1 );
    path.addPolygon( QPolygonF( points ) );
    path.closeSubpath();
    return path;
}

void AcVoltage::paint( QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget ) {
    if ( !m_isRail ) {
        FixedVolt::paint( painter, option, widget );
        return;
    }

    Component::paint( painter, option, widget );
    painter->setBrush( QColor( 255, 166, 0 ) );
    static const QPointF points[4] = { QPointF( -1.5, -6.5 ), QPointF( -1.5, 6.5 ), QPointF( 9, 1 ),
                                       QPointF( 9, -1 ) };
    painter->drawPolygon( points, 4 );
    painter->drawText( QRectF( -5, -6, 7, 12 ), Qt::AlignCenter, "~" );
    Component::paintSelected( painter );
}
