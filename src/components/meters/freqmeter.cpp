/***************************************************************************
 *   Copyright (C) 2019 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QFont>
#include <QPainter>

#include "freqmeter.h"
#include "itemlibrary.h"
#include "label.h"
#include "pin.h"
#include "simulator.h"

#include "doubleprop.h"

#define tr( str ) simulideTr( "FreqMeter", str )

Component* FreqMeter::construct( QString type, QString id ) {
    return new FreqMeter( type, id );
}

LibraryItem* FreqMeter::libraryItem() {
    return new LibraryItem( tr( "Frequency Meter" ), "Meters", "frequencimeter.png", "FreqMeter,Frequencimeter",
                            FreqMeter::construct );
}

FreqMeter::FreqMeter( QString type, QString id ) : Component( type, id ), eElement( id ), m_display( this ) {
    m_area = QRectF( -32, -10, 85, 20 );
    m_color = Qt::black;
    m_graphical = true;

    m_ePin.resize( 1 );
    m_pin.resize( 1 );
    m_ePin[0] = m_pin[0] = new Pin( 180, QPoint( -40, 0 ), id + "-lPin", 0, this );

    m_idLabel->setPos( -12, -24 );
    setLabelPos( -32, -24, 0 );

    QFont font( "Ubuntu Mono", 13, QFont::Bold );
    font.setPixelSize( 13 );
    m_display.setFont( font );
    m_display.setBrush( Qt::yellow );
    m_display.setPos( -30, -6 );
    m_display.setVisible( true );

    m_filter = 0.1;

    Simulator::self()->addToUpdateList( this );

    FreqMeter::initialize();

    addPropGroup(
        { tr( "Main" ),
          {
              new DoubProp<FreqMeter>( "Filter", tr( "Filter" ), "V", this, &FreqMeter::filter, &FreqMeter::setFilter ),
          },
          0 } );
}
FreqMeter::~FreqMeter() { }

void FreqMeter::initialize() {
    m_rising = false;
    m_falling = false;

    m_lastData = 0;
    m_freq = 0;
    m_numMax = 0;
    m_lastMax = 0;
    m_totalP = 0;
    m_period = 0;

    m_display.setText( "0 Hz" );
}

void FreqMeter::stamp() // Called at Simulation Start
{
    m_ePin[0]->changeCallBack( this );
}

void FreqMeter::updateStep() {
    double freq = m_freq;
    if ( m_period > 0 ) // We have a wave
    {
        uint64_t simTime = Simulator::self()->circTime();
        uint64_t psPF = Simulator::self()->psPerFrame();
        uint64_t lost = m_period * 2;
        if ( lost < psPF * 2 )
            lost = psPF * 2;

        if ( simTime - m_lastMax > lost ) // Wave lost
        {
            freq = 0;
            m_period = 0;
            m_totalP = 0;
            m_numMax = 0;
        } else if ( m_numMax > 1 ) {
            freq = 1e12 / ( (double) m_totalP / (double) ( m_numMax - 1 ) );
            m_totalP = 0;
            m_numMax = 0;
        }
    }
    if ( m_freq != freq ) {
        m_freq = freq;

        int Fdecs = 1;
        QString unit = "  Hz";

        if ( freq > 999 ) {
            freq /= 1e3;
            unit = " kHz";
            if ( freq > 999 ) {
                freq /= 1e3;
                unit = " MHz";
                if ( freq > 999 ) {
                    freq /= 1e3;
                    unit = " GHz";
                }
            }
        }
        if ( freq < 10 )
            Fdecs = 4;
        else if ( freq < 100 )
            Fdecs = 3;
        else if ( freq < 1000 )
            Fdecs = 2;
        m_display.setText( QString::number( freq, 'f', Fdecs ) + unit );
    }
}

void FreqMeter::voltChanged() {
    uint64_t simTime = Simulator::self()->circTime();
    double data = m_ePin[0]->getVoltage();
    double delta = data - m_lastData;

    if ( delta > m_filter ) // Rising
    {
        if ( m_falling && !m_rising )
            m_falling = false; // Min Found

        m_rising = true;
        m_lastData = data;
    } else if ( delta < -m_filter ) // Falling
    {
        if ( m_rising && !m_falling ) // Max Found
        {
            if ( m_numMax > 0 ) {
                m_period = simTime - m_lastMax;
                m_totalP += m_period;
            }
            m_lastMax = simTime;
            m_numMax++;
            m_rising = false;
        }
        m_falling = true;
        m_lastData = data;
    }
}

void FreqMeter::setflip() {
    Component::setflip();
    int xlabelpos = ( m_Hflip == 1 ) ? -30 : 51;
    int ylabelpos = -6 * m_Vflip;

    m_display.setPos( xlabelpos, ylabelpos );
    m_display.setTransform( QTransform::fromScale( m_Hflip, m_Vflip ) );
}

void FreqMeter::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) {
    Component::paint( p, o, w );
    p->setBrush( Qt::black );
    p->drawRoundedRect( m_area, 1, 1 );

    Component::paintSelected( p );
}
