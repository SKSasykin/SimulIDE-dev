/***************************************************************************
 *   Copyright (C) 2026 by SimulIDE contributors                           *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "heater.h"

#include <algorithm>
#include <cmath>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QGraphicsSceneMouseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QVBoxLayout>

#include "circuitwidget.h"
#include "itemlibrary.h"
#include "label.h"
#include "simulator.h"

#include "doubleprop.h"
#include "intprop.h"
#include "stringprop.h"

#define tr( str ) simulideTr( "Heater", str )

namespace {
constexpr double pi = 3.14159265358979323846;
constexpr double stefanBoltzmann = 5.670374419e-8;

double boundedPositive( double value, double minimum ) {
    return std::max( value, minimum );
}
}

HeaterPlot::HeaterPlot( QWidget* parent ) : QWidget( parent ) {
    m_windowSeconds = 60;
    m_viewEnd = 0;
    setMinimumSize( 620, 340 );
    setMouseTracking( true );
}

void HeaterPlot::setData( const QVector<double>& time, const QVector<double>& bodyTemp,
                          const QVector<double>& sensorTemp ) {
    m_time = time;
    m_bodyTemp = bodyTemp;
    m_sensorTemp = sensorTemp;
    update();
}

void HeaterPlot::setWindowSeconds( double seconds ) {
    m_windowSeconds = boundedPositive( seconds, 0.001 );
    update();
}

void HeaterPlot::setViewEnd( double seconds ) {
    m_viewEnd = seconds;
    update();
}

void HeaterPlot::paintEvent( QPaintEvent* ) {
    QPainter painter( this );
    painter.setRenderHint( QPainter::Antialiasing );
    painter.fillRect( rect(), QColor( 12, 17, 24 ) );

    const QRectF graph( 58, 24, width() - 78, height() - 72 );
    painter.setPen( QPen( QColor( 58, 68, 80 ), 1 ) );
    for ( int i = 0; i <= 10; ++i ) {
        const double x = graph.left() + graph.width() * i / 10.0;
        painter.drawLine( QPointF( x, graph.top() ), QPointF( x, graph.bottom() ) );
    }
    for ( int i = 0; i <= 8; ++i ) {
        const double y = graph.top() + graph.height() * i / 8.0;
        painter.drawLine( QPointF( graph.left(), y ), QPointF( graph.right(), y ) );
    }

    painter.setPen( QColor( 210, 218, 226 ) );
    if ( m_time.isEmpty() ) {
        painter.drawText( graph, Qt::AlignCenter, tr( "No temperature data" ) );
        return;
    }

    const double end = m_viewEnd > 0 ? m_viewEnd : m_time.last();
    const double start = end - m_windowSeconds;
    double minTemp = 1e12;
    double maxTemp = -1e12;
    int first = 0;
    while ( first + 1 < m_time.size() && m_time.at( first + 1 ) < start )
        ++first;

    int last = first;
    for ( int i = first; i < m_time.size() && m_time.at( i ) <= end; ++i ) {
        last = i;
        minTemp = std::min( minTemp, std::min( m_bodyTemp.at( i ), m_sensorTemp.at( i ) ) );
        maxTemp = std::max( maxTemp, std::max( m_bodyTemp.at( i ), m_sensorTemp.at( i ) ) );
    }
    if ( minTemp > maxTemp ) {
        minTemp = m_sensorTemp.last() - 1;
        maxTemp = m_sensorTemp.last() + 1;
    }
    double range = maxTemp - minTemp;
    if ( range < 2 ) {
        const double middle = ( maxTemp + minTemp ) / 2;
        minTemp = middle - 1;
        maxTemp = middle + 1;
        range = 2;
    } else {
        minTemp -= range * 0.08;
        maxTemp += range * 0.08;
        range = maxTemp - minTemp;
    }

    auto makePath = [&]( const QVector<double>& values ) {
        QPainterPath path;
        bool started = false;
        for ( int i = first; i <= last; ++i ) {
            const double x = graph.left() + ( m_time.at( i ) - start ) * graph.width() / m_windowSeconds;
            const double y = graph.bottom() - ( values.at( i ) - minTemp ) * graph.height() / range;
            if ( !started ) {
                path.moveTo( x, y );
                started = true;
            } else
                path.lineTo( x, y );
        }
        return path;
    };

    painter.setClipRect( graph );
    painter.setPen( QPen( QColor( 255, 132, 48 ), 2 ) );
    painter.drawPath( makePath( m_bodyTemp ) );
    painter.setPen( QPen( QColor( 56, 214, 225 ), 2 ) );
    painter.drawPath( makePath( m_sensorTemp ) );
    painter.setClipping( false );

    painter.setPen( QColor( 210, 218, 226 ) );
    painter.drawText( QRectF( 4, graph.top() - 8, 50, 20 ), Qt::AlignRight | Qt::AlignVCenter,
                      QString::number( maxTemp, 'f', 1 ) + " C" );
    painter.drawText( QRectF( 4, graph.bottom() - 10, 50, 20 ), Qt::AlignRight | Qt::AlignVCenter,
                      QString::number( minTemp, 'f', 1 ) + " C" );
    painter.drawText( QRectF( graph.left(), graph.bottom() + 8, 100, 20 ), Qt::AlignLeft,
                      QString::number( start, 'f', 2 ) + " s" );
    painter.drawText( QRectF( graph.right() - 100, graph.bottom() + 8, 100, 20 ), Qt::AlignRight,
                      QString::number( end, 'f', 2 ) + " s" );

    painter.setPen( QPen( QColor( 255, 132, 48 ), 2 ) );
    painter.drawLine( graph.left(), 12, graph.left() + 18, 12 );
    painter.setPen( QColor( 230, 230, 230 ) );
    painter.drawText( graph.left() + 24, 17, tr( "Body" ) );
    painter.setPen( QPen( QColor( 56, 214, 225 ), 2 ) );
    painter.drawLine( graph.left() + 90, 12, graph.left() + 108, 12 );
    painter.setPen( QColor( 230, 230, 230 ) );
    painter.drawText( graph.left() + 114, 17, tr( "Thermocouple" ) );
}

HeaterDialog::HeaterDialog( QWidget* parent, Heater* heater ) : QDialog( parent ) {
    m_heater = heater;
    m_firstTime = 0;
    m_lastTime = 0;
    setWindowFlags( Qt::Window | Qt::WindowTitleHint | Qt::Tool | Qt::WindowSystemMenuHint
                    | Qt::WindowCloseButtonHint );
    resize( 760, 470 );

    QVBoxLayout* layout = new QVBoxLayout( this );
    m_status = new QLabel( this );
    m_plot = new HeaterPlot( this );
    m_scrollBar = new QScrollBar( Qt::Horizontal, this );
    m_scrollBar->setRange( 0, 10000 );
    m_scrollBar->setPageStep( 1000 );
    m_scrollBar->setValue( 10000 );

    QHBoxLayout* controls = new QHBoxLayout;
    controls->addWidget( new QLabel( tr( "Visible time:" ), this ) );
    m_windowBox = new QDoubleSpinBox( this );
    m_windowBox->setRange( 0.1, 86400 );
    m_windowBox->setDecimals( 2 );
    m_windowBox->setSuffix( " s" );
    controls->addWidget( m_windowBox );
    m_followBox = new QCheckBox( tr( "Follow latest" ), this );
    m_followBox->setChecked( true );
    controls->addWidget( m_followBox );
    controls->addStretch();

    layout->addWidget( m_status );
    layout->addWidget( m_plot, 1 );
    layout->addWidget( m_scrollBar );
    layout->addLayout( controls );

    QObject::connect( m_windowBox, QOverload<double>::of( &QDoubleSpinBox::valueChanged ),
                      [=]( double value ) {
                          m_heater->setPlotWindow( value );
                          m_plot->setWindowSeconds( value );
                          updateViewEnd();
                      } );
    QObject::connect( m_scrollBar, &QScrollBar::valueChanged, [=]( int ) {
        if ( m_followBox->isChecked() )
            m_followBox->setChecked( false );
        updateViewEnd();
    } );
    QObject::connect( m_followBox, &QCheckBox::toggled, [=]( bool follow ) {
        if ( follow ) {
            m_scrollBar->blockSignals( true );
            m_scrollBar->setValue( m_scrollBar->maximum() );
            m_scrollBar->blockSignals( false );
        }
        updateViewEnd();
    } );
}

void HeaterDialog::setWindowSeconds( double seconds ) {
    m_windowBox->blockSignals( true );
    m_windowBox->setValue( seconds );
    m_windowBox->blockSignals( false );
    m_plot->setWindowSeconds( seconds );
    updateViewEnd();
}

void HeaterDialog::updateData( const QVector<double>& time, const QVector<double>& bodyTemp,
                               const QVector<double>& sensorTemp, double power1, double power2,
                               double capacity, double loss, double sensorTau ) {
    m_plot->setData( time, bodyTemp, sensorTemp );
    if ( !time.isEmpty() ) {
        m_firstTime = time.first();
        m_lastTime = time.last();
    }
    const double body = bodyTemp.isEmpty() ? 0 : bodyTemp.last();
    const double sensor = sensorTemp.isEmpty() ? 0 : sensorTemp.last();
    m_status->setText( tr( "Body %1 C | Thermocouple %2 C | Power %3 + %4 W | C %5 J/K | G %6 W/K | lag %7 s" )
                           .arg( body, 0, 'f', 2 )
                           .arg( sensor, 0, 'f', 2 )
                           .arg( power1, 0, 'f', 1 )
                           .arg( power2, 0, 'f', 1 )
                           .arg( capacity, 0, 'f', 1 )
                           .arg( loss, 0, 'f', 3 )
                           .arg( sensorTau, 0, 'f', 3 ) );
    updateViewEnd();
}

void HeaterDialog::updateViewEnd() {
    const double window = m_windowBox->value();
    const double firstEnd = m_firstTime + window;
    const double available = std::max( 0.0, m_lastTime - firstEnd );
    if ( m_followBox->isChecked() ) {
        m_scrollBar->blockSignals( true );
        m_scrollBar->setValue( m_scrollBar->maximum() );
        m_scrollBar->blockSignals( false );
        m_plot->setViewEnd( m_lastTime );
    } else {
        const double fraction = m_scrollBar->value() / (double) m_scrollBar->maximum();
        m_plot->setViewEnd( available > 0 ? firstEnd + available * fraction : m_lastTime );
    }
    m_scrollBar->setEnabled( available > 0 );
}

Component* Heater::construct( QString type, QString id ) {
    return new Heater( type, id );
}

LibraryItem* Heater::libraryItem() {
    return new LibraryItem( tr( "Dual Heater" ), "Heaters", "heater.svg", "Heater", Heater::construct );
}

Heater::Heater( QString type, QString id )
    : Component( type, id ), eElement( id + "-thermal" ), m_heater1( id + "-heater1" ),
      m_heater2( id + "-heater2" ), m_tcOutput( id + "-thermocouple" ),
      m_h1Plus( 270, QPoint( -48, 64 ), id + "-H1P", 0, this, 16 ),
      m_h1Minus( 270, QPoint( -32, 64 ), id + "-H1M", 1, this, 16 ),
      m_tcPlus( 270, QPoint( -8, 64 ), id + "-TCP", 2, this, 16 ),
      m_tcMinus( 270, QPoint( 8, 64 ), id + "-TCM", 3, this, 16 ),
      m_h2Plus( 270, QPoint( 32, 64 ), id + "-H2P", 4, this, 16 ),
      m_h2Minus( 270, QPoint( 48, 64 ), id + "-H2M", 5, this, 16 ) {
    m_graphical = true;
    m_area = QRectF( -64, -48, 128, 112 );
    m_color = QColor( 220, 225, 230 );

    m_pin = { &m_h1Plus, &m_h1Minus, &m_tcPlus, &m_tcMinus, &m_h2Plus, &m_h2Minus };
    const QStringList labels = { "H1+", "H1-", "TC+", "TC-", "H2+", "H2-" };
    for ( int i = 0; i < 6; ++i ) {
        m_pin[i]->setLabelText( labels.at( i ) );
        m_pin[i]->setFontSize( 8 );
        m_pin[i]->setSpace( 6 );
        m_pin[i]->setLabelColor( Qt::transparent );
    }
    m_h1Plus.setColor( QColor( 190, 55, 40 ) );
    m_h1Minus.setColor( QColor( 190, 55, 40 ) );
    m_h2Plus.setColor( QColor( 190, 55, 40 ) );
    m_h2Minus.setColor( QColor( 190, 55, 40 ) );
    m_tcPlus.setColor( QColor( 220, 155, 30 ) );
    m_tcMinus.setColor( QColor( 50, 125, 185 ) );

    m_heater1.setEpin( 0, &m_h1Plus );
    m_heater1.setEpin( 1, &m_h1Minus );
    m_heater2.setEpin( 0, &m_h2Plus );
    m_heater2.setEpin( 1, &m_h2Minus );
    m_tcOutput.setEpin( 0, &m_tcPlus );
    m_tcOutput.setEpin( 1, &m_tcMinus );

    m_supplyType = "AC";
    m_material = "Aluminum";
    m_environment = "Air";
    m_ratedPower1 = 1000;
    m_ratedPower2 = 1000;
    m_ratedVoltage1 = 220;
    m_ratedVoltage2 = 220;
    m_resistance1 = m_ratedVoltage1 * m_ratedVoltage1 / m_ratedPower1;
    m_resistance2 = m_ratedVoltage2 * m_ratedVoltage2 / m_ratedPower2;
    m_wireTemperatureCoefficient = 0.0004;
    m_heater1.setResistance( m_resistance1 );
    m_heater2.setResistance( m_resistance2 );

    m_length = 0.1;
    m_width = 0.06;
    m_height = 0.02;
    m_airCoefficient = 10;
    m_emissivity = 0.2;
    m_sensorDistance = 0.01;
    m_ambientTemp = 20;
    m_initialTemp = 20;
    m_bodyTemp = 20;
    m_sensorTemp = 20;
    m_displayTemp = 20;
    m_tcSensitivity = 41e-6;
    m_tcResistance = 5;
    m_tcOutput.setResistance( m_tcResistance );
    m_thermalStep = 0.1;
    m_thermalPeriod = 100000000000ULL;
    m_lastThermalTime = 0;
    m_lastPower1 = 0;
    m_lastPower2 = 0;
    m_plotWindow = 60;
    m_historySize = 100000;
    m_tcConnected = false;
    m_electricalDirty = false;
    m_rescheduleEvent = false;
    m_guiDirty = true;
    updateGeometryValues();

    setLabelPos( -28, -64 );
    setShowId( true );
    setShowVal( true );
    const QString temperatureText = "20.0 C";
    setValLabelText( temperatureText );
    setValLabelPos( -QFontMetrics( m_valLabel->font() ).horizontalAdvance( temperatureText ) / 2.0,
                    -28, 0 );

    m_dialog = new HeaterDialog( CircuitWidget::self(), this );
    m_dialog->setWindowTitle( idLabel() + " - " + tr( "Temperature" ) );
    m_dialog->setWindowSeconds( m_plotWindow );
    Simulator::self()->addToUpdateList( this );

    addPropGroup( { tr( "Heaters" ),
                    { new StrProp<Heater>( "SupplyType", tr( "Rated supply" ),
                                           "AC,DC;AC,DC", this, &Heater::supplyType,
                                           &Heater::setSupplyType, 0, "enum" ),
                      new DoubProp<Heater>( "Heater1Power", tr( "Heater 1 power" ), "W", this,
                                            &Heater::heater1Power, &Heater::setHeater1Power ),
                      new DoubProp<Heater>( "Heater1Voltage", tr( "Heater 1 rated voltage" ), "V", this,
                                            &Heater::heater1Voltage, &Heater::setHeater1Voltage ),
                      new DoubProp<Heater>( "Heater1Resistance", tr( "Heater 1 resistance at 20 °C" ), "Ω", this,
                                            &Heater::heater1Resistance, &Heater::setHeater1Resistance ),
                      new DoubProp<Heater>( "Heater2Power", tr( "Heater 2 power" ), "W", this,
                                            &Heater::heater2Power, &Heater::setHeater2Power ),
                      new DoubProp<Heater>( "Heater2Voltage", tr( "Heater 2 rated voltage" ), "V", this,
                                            &Heater::heater2Voltage, &Heater::setHeater2Voltage ),
                      new DoubProp<Heater>( "Heater2Resistance", tr( "Heater 2 resistance at 20 °C" ), "Ω", this,
                                            &Heater::heater2Resistance, &Heater::setHeater2Resistance ),
                      new DoubProp<Heater>( "WireTemperatureCoefficient",
                                            tr( "Wire temperature coefficient" ), "1/°C", this,
                                            &Heater::wireTemperatureCoefficient,
                                            &Heater::setWireTemperatureCoefficient ) },
                    0 } );

    addPropGroup( { tr( "Thermal body" ),
                    { new StrProp<Heater>( "Material", tr( "Material" ),
                                           "Aluminum;" + tr( "Aluminum" ), this, &Heater::material,
                                           &Heater::setMaterial, 0, "enum" ),
                      new DoubProp<Heater>( "BodyLength", tr( "Length" ), "mm", this,
                                            &Heater::bodyLength, &Heater::setBodyLength ),
                      new DoubProp<Heater>( "BodyWidth", tr( "Width" ), "mm", this,
                                            &Heater::bodyWidth, &Heater::setBodyWidth ),
                      new DoubProp<Heater>( "BodyHeight", tr( "Height" ), "mm", this,
                                            &Heater::bodyHeight, &Heater::setBodyHeight ),
                      new DoubProp<Heater>( "InitialTemperature", tr( "Initial temperature" ), "°C", this,
                                            &Heater::initialTemperature, &Heater::setInitialTemperature ),
                      new DoubProp<Heater>( "SensorDistance", tr( "Heater to thermocouple distance" ), "mm",
                                            this, &Heater::sensorDistance, &Heater::setSensorDistance ) },
                    0 } );

    addPropGroup( { tr( "Environment" ),
                    { new StrProp<Heater>( "Environment", tr( "Environment" ),
                                           "Air;" + tr( "Air" ), this, &Heater::environment,
                                           &Heater::setEnvironment, 0, "enum" ),
                      new DoubProp<Heater>( "AmbientTemperature", tr( "Ambient temperature" ), "°C", this,
                                            &Heater::ambientTemperature, &Heater::setAmbientTemperature ),
                      new DoubProp<Heater>( "AirCoefficient", tr( "Air heat transfer coefficient" ),
                                            "W/m²K", this, &Heater::airCoefficient,
                                            &Heater::setAirCoefficient ),
                      new DoubProp<Heater>( "Emissivity", tr( "Surface emissivity" ), "", this,
                                            &Heater::emissivity, &Heater::setEmissivity ) },
                    0 } );

    addPropGroup( { tr( "Thermocouple" ),
                    { new DoubProp<Heater>( "TcSensitivity", tr( "Sensitivity" ), "µV/°C", this,
                                            &Heater::thermocoupleSensitivity,
                                            &Heater::setThermocoupleSensitivity ),
                      new DoubProp<Heater>( "TcResistance", tr( "Output resistance" ), "Ω", this,
                                            &Heater::thermocoupleResistance,
                                            &Heater::setThermocoupleResistance ),
                      new DoubProp<Heater>( "ThermalStep", tr( "Thermal calculation step" ), "ms", this,
                                            &Heater::thermalStep, &Heater::setThermalStep ) },
                    0 } );

    addPropGroup( { tr( "Temperature plot" ),
                    { new DoubProp<Heater>( "PlotWindow", tr( "Visible time" ), "s", this,
                                            &Heater::plotWindow, &Heater::setPlotWindow ),
                      new IntProp<Heater>( "HistorySize", tr( "History samples" ), "", this,
                                           &Heater::historySize, &Heater::setHistorySize, 0, "uint" ) },
                    0 } );
}

Heater::~Heater() {
    m_dialog->setParent( nullptr );
    m_dialog->close();
    delete m_dialog;
}

void Heater::initialize() {
    m_electricalDirty = false;
    m_lastPower1 = 0;
    m_lastPower2 = 0;
    m_rescheduleEvent = false;
    m_lastThermalTime = Simulator::self()->circTime();
    m_tcConnected = false;
    if ( Simulator::self()->simState() == SIM_STARTING ) {
        m_bodyTemp = m_initialTemp;
        m_sensorTemp = m_initialTemp;
        m_historyTime.clear();
        m_historyBody.clear();
        m_historySensor.clear();
        appendHistory( m_lastThermalTime );
    }
    // Loaded property values and initial temperature must reach the solver before its first stamp.
    m_heater1.setResistance( resistanceAtTemperature( m_resistance1 ) );
    m_heater2.setResistance( resistanceAtTemperature( m_resistance2 ) );
    m_tcOutput.setResistance( m_tcResistance );
    m_guiDirty = true;
}

void Heater::stamp() {
    m_h1Plus.changeCallBack( this );
    m_h1Minus.changeCallBack( this );
    m_h2Plus.changeCallBack( this );
    m_h2Minus.changeCallBack( this );

    m_tcConnected = m_tcPlus.isConnected() && m_tcMinus.isConnected();
    if ( m_tcConnected ) {
        m_tcPlus.createCurrent();
        m_tcMinus.createCurrent();
        stampThermocouple();
    }
    m_lastThermalTime = Simulator::self()->circTime();
    Simulator::self()->addEvent( m_thermalPeriod, this );
}

void Heater::runEvent() {
    QMutexLocker locker( &m_stateMutex );
    const uint64_t time = Simulator::self()->circTime();
    advanceThermal( time );
    updatePower();
    appendHistory( time );
    m_guiDirty = true;
    Simulator::self()->addEvent( m_thermalPeriod, this );
}

void Heater::voltChanged() {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime() );
    updatePower();
}

void Heater::advanceThermal( uint64_t time, bool stampOutput ) {
    if ( time <= m_lastThermalTime )
        return;
    const double dt = ( time - m_lastThermalTime ) * 1e-12;
    m_lastThermalTime = time;

    const double capacity = thermalCapacity();
    const double conductance = heatLossCoefficient();
    const double totalPower = m_lastPower1 + m_lastPower2;
    if ( conductance < 1e-9 ) {
        const double rate = totalPower / capacity;
        const double sensorRate = 1 / sensorTimeConstant();
        const double sensorDecay = std::exp( -sensorRate * dt );
        const double oldBody = m_bodyTemp;
        m_bodyTemp = oldBody + rate * dt;
        m_sensorTemp = oldBody + rate * ( dt - 1 / sensorRate )
                       + ( m_sensorTemp - oldBody + rate / sensorRate ) * sensorDecay;
        m_bodyTemp = std::max( -273.14, std::min( m_bodyTemp, 5000.0 ) );
        m_sensorTemp = std::max( -273.14, std::min( m_sensorTemp, 5000.0 ) );
        m_guiDirty = true;
        if ( stampOutput )
            stampThermocouple();
        return;
    }

    const double equilibrium = m_ambientTemp + totalPower / conductance;
    const double bodyRate = conductance / capacity;
    const double sensorRate = 1 / sensorTimeConstant();
    const double oldBody = m_bodyTemp;
    const double oldSensor = m_sensorTemp;
    const double bodyDecay = std::exp( -bodyRate * dt );
    const double sensorDecay = std::exp( -sensorRate * dt );

    m_bodyTemp = equilibrium + ( oldBody - equilibrium ) * bodyDecay;
    if ( std::abs( sensorRate - bodyRate ) < 1e-12 )
        m_sensorTemp = equilibrium + ( oldSensor - equilibrium ) * sensorDecay
                       + sensorRate * ( oldBody - equilibrium ) * dt * sensorDecay;
    else
        m_sensorTemp = equilibrium + ( oldSensor - equilibrium ) * sensorDecay
                       + sensorRate * ( oldBody - equilibrium ) * ( bodyDecay - sensorDecay )
                             / ( sensorRate - bodyRate );

    m_bodyTemp = std::max( -273.14, std::min( m_bodyTemp, 5000.0 ) );
    m_sensorTemp = std::max( -273.14, std::min( m_sensorTemp, 5000.0 ) );
    m_guiDirty = true;
    if ( stampOutput )
        stampThermocouple();
}

void Heater::updatePower() {
    const double voltage1 = m_h1Plus.getVoltage() - m_h1Minus.getVoltage();
    const double voltage2 = m_h2Plus.getVoltage() - m_h2Minus.getVoltage();
    m_lastPower1 = std::abs( voltage1 * m_heater1.current() );
    m_lastPower2 = std::abs( voltage2 * m_heater2.current() );
}

void Heater::stampThermocouple() {
    if ( !m_tcConnected )
        return;
    const double voltage = m_tcSensitivity * ( m_sensorTemp - m_ambientTemp );
    const double current = voltage / m_tcResistance;
    m_tcPlus.stampCurrent( current );
    m_tcMinus.stampCurrent( -current );
}

void Heater::appendHistory( uint64_t time ) {
    if ( m_historyTime.size() >= m_historySize ) {
        const int removeCount = std::max( 1, m_historySize / 10 );
        m_historyTime.remove( 0, removeCount );
        m_historyBody.remove( 0, removeCount );
        m_historySensor.remove( 0, removeCount );
    }
    m_historyTime.append( time * 1e-12 );
    m_historyBody.append( m_bodyTemp );
    m_historySensor.append( m_sensorTemp );
}

void Heater::updateStep() {
    QVector<double> time;
    QVector<double> bodyTemp;
    QVector<double> sensorTemp;
    double power1 = 0;
    double power2 = 0;
    double capacity = 0;
    double loss = 0;
    double sensorTau = 0;
    const bool plotVisible = m_dialog->isVisible();
    {
        QMutexLocker locker( &m_stateMutex );
        bool resistanceChanged = false;
        if ( m_electricalDirty ) {
            m_electricalDirty = false;
            m_tcOutput.setResistance( m_tcResistance );
            stampThermocouple();
            resistanceChanged = true;
        }
        const double hotResistance1 = resistanceAtTemperature( m_resistance1 );
        const double hotResistance2 = resistanceAtTemperature( m_resistance2 );
        if ( std::abs( hotResistance1 - m_heater1.resistance() ) > hotResistance1 * 1e-9 ) {
            m_heater1.setResistance( hotResistance1 );
            resistanceChanged = true;
        }
        if ( std::abs( hotResistance2 - m_heater2.resistance() ) > hotResistance2 * 1e-9 ) {
            m_heater2.setResistance( hotResistance2 );
            resistanceChanged = true;
        }
        if ( resistanceChanged )
            updatePower();
        if ( m_rescheduleEvent ) {
            m_rescheduleEvent = false;
            if ( Simulator::self()->isRunning() ) {
                Simulator::self()->cancelEvents( this );
                Simulator::self()->addEvent( m_thermalPeriod, this );
            }
        }
        if ( !m_guiDirty )
            return;

        m_guiDirty = false;
        m_displayTemp = m_sensorTemp;
        if ( plotVisible ) {
            const int stride = std::max( 1, ( m_historyTime.size() + 4999 ) / 5000 );
            for ( int i = 0; i < m_historyTime.size(); i += stride ) {
                time.append( m_historyTime.at( i ) );
                bodyTemp.append( m_historyBody.at( i ) );
                sensorTemp.append( m_historySensor.at( i ) );
            }
            if ( !m_historyTime.isEmpty() && time.last() != m_historyTime.last() ) {
                time.append( m_historyTime.last() );
                bodyTemp.append( m_historyBody.last() );
                sensorTemp.append( m_historySensor.last() );
            }
            power1 = m_lastPower1;
            power2 = m_lastPower2;
            capacity = thermalCapacity();
            loss = heatLossCoefficient();
            sensorTau = sensorTimeConstant();
        }
    }
    const QString temperatureText = QString::number( m_displayTemp, 'f', 1 ) + " C";
    setValLabelText( temperatureText );
    setValLabelPos( -QFontMetrics( m_valLabel->font() ).horizontalAdvance( temperatureText ) / 2.0,
                    -28, 0 );
    if ( plotVisible )
        m_dialog->updateData( time, bodyTemp, sensorTemp, power1, power2, capacity, loss, sensorTau );
    update();
}

void Heater::setHeater1Power( double power ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_ratedPower1 = boundedPositive( power, 1e-6 );
    m_resistance1 = m_ratedVoltage1 * m_ratedVoltage1 / m_ratedPower1;
    m_electricalDirty = true;
}

void Heater::setHeater2Power( double power ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_ratedPower2 = boundedPositive( power, 1e-6 );
    m_resistance2 = m_ratedVoltage2 * m_ratedVoltage2 / m_ratedPower2;
    m_electricalDirty = true;
}

void Heater::setHeater1Voltage( double voltage ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_ratedVoltage1 = boundedPositive( voltage, 1e-6 );
    m_resistance1 = m_ratedVoltage1 * m_ratedVoltage1 / m_ratedPower1;
    m_electricalDirty = true;
}

void Heater::setHeater2Voltage( double voltage ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_ratedVoltage2 = boundedPositive( voltage, 1e-6 );
    m_resistance2 = m_ratedVoltage2 * m_ratedVoltage2 / m_ratedPower2;
    m_electricalDirty = true;
}

void Heater::setHeater1Resistance( double resistance ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_resistance1 = boundedPositive( resistance, 1e-9 );
    m_ratedPower1 = m_ratedVoltage1 * m_ratedVoltage1 / m_resistance1;
    m_electricalDirty = true;
}

void Heater::setHeater2Resistance( double resistance ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_resistance2 = boundedPositive( resistance, 1e-9 );
    m_ratedPower2 = m_ratedVoltage2 * m_ratedVoltage2 / m_resistance2;
    m_electricalDirty = true;
}

void Heater::setWireTemperatureCoefficient( double coefficient ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_wireTemperatureCoefficient = std::max( 0.0, std::min( coefficient, 0.1 ) );
    m_electricalDirty = true;
    m_guiDirty = true;
}

void Heater::setSupplyType( QString type ) {
    QMutexLocker locker( &m_stateMutex );
    m_supplyType = type == "DC" ? "DC" : "AC";
}

void Heater::setMaterial( QString material ) {
    QMutexLocker locker( &m_stateMutex );
    m_material = "Aluminum";
    if ( material == "Aluminum" )
        updateGeometryValues();
}

void Heater::setEnvironment( QString environment ) {
    QMutexLocker locker( &m_stateMutex );
    m_environment = "Air";
    if ( environment == "Air" )
        m_guiDirty = true;
}

void Heater::setBodyLength( double length ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_length = boundedPositive( length, 1e-6 );
    m_guiDirty = true;
}

void Heater::setBodyWidth( double width ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_width = boundedPositive( width, 1e-6 );
    m_guiDirty = true;
}

void Heater::setBodyHeight( double height ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_height = boundedPositive( height, 1e-6 );
    m_guiDirty = true;
}

void Heater::setAmbientTemperature( double temperature ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_ambientTemp = std::max( temperature, -273.14 );
    m_electricalDirty = true;
    m_guiDirty = true;
}

void Heater::setInitialTemperature( double temperature ) {
    QMutexLocker locker( &m_stateMutex );
    m_initialTemp = std::max( temperature, -273.14 );
}

void Heater::setAirCoefficient( double coefficient ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_airCoefficient = boundedPositive( coefficient, 0 );
    m_guiDirty = true;
}

void Heater::setEmissivity( double emissivity ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_emissivity = std::max( 0.0, std::min( emissivity, 1.0 ) );
    m_guiDirty = true;
}

void Heater::setSensorDistance( double distance ) {
    QMutexLocker locker( &m_stateMutex );
    advanceThermal( Simulator::self()->circTime(), false );
    m_sensorDistance = boundedPositive( distance, 1e-6 );
    m_guiDirty = true;
}

void Heater::setThermocoupleSensitivity( double sensitivity ) {
    QMutexLocker locker( &m_stateMutex );
    m_tcSensitivity = sensitivity;
    m_electricalDirty = true;
}

void Heater::setThermocoupleResistance( double resistance ) {
    QMutexLocker locker( &m_stateMutex );
    m_tcResistance = boundedPositive( resistance, 1e-9 );
    m_electricalDirty = true;
}

void Heater::setThermalStep( double step ) {
    QMutexLocker locker( &m_stateMutex );
    m_thermalStep = std::max( 0.001, std::min( step, 10.0 ) );
    m_thermalPeriod = (uint64_t) ( m_thermalStep * 1e12 );
    m_rescheduleEvent = true;
}

void Heater::setPlotWindow( double seconds ) {
    m_plotWindow = std::max( 0.1, std::min( seconds, 86400.0 ) );
    if ( m_dialog )
        m_dialog->setWindowSeconds( m_plotWindow );
}

void Heater::setHistorySize( int size ) {
    QMutexLocker locker( &m_stateMutex );
    m_historySize = std::max( 1000, std::min( size, 1000000 ) );
    if ( m_historyTime.size() > m_historySize ) {
        const int removeCount = m_historyTime.size() - m_historySize;
        m_historyTime.remove( 0, removeCount );
        m_historyBody.remove( 0, removeCount );
        m_historySensor.remove( 0, removeCount );
    }
}

void Heater::updateGeometryValues() {
    m_density = 2700;
    m_specificHeat = 897;
    m_thermalConductivity = 205;
    m_guiDirty = true;
}

double Heater::thermalCapacity() const {
    return boundedPositive( m_length * m_width * m_height * m_density * m_specificHeat, 1e-9 );
}

double Heater::heatLossCoefficient() const {
    const double area = 2 * ( m_length * m_width + m_length * m_height + m_width * m_height );
    const double bodyK = std::max( m_bodyTemp + 273.15, 1.0 );
    const double ambientK = std::max( m_ambientTemp + 273.15, 1.0 );
    const double radiation = m_emissivity * stefanBoltzmann * area * ( bodyK + ambientK )
                             * ( bodyK * bodyK + ambientK * ambientK );
    return std::max( m_airCoefficient * area + radiation, 0.0 );
}

double Heater::sensorTimeConstant() const {
    const double diffusivity = m_thermalConductivity / ( m_density * m_specificHeat );
    return boundedPositive( m_sensorDistance * m_sensorDistance / ( pi * pi * diffusivity ), 0.001 );
}

double Heater::resistanceAtTemperature( double referenceResistance ) const {
    const double factor = 1 + m_wireTemperatureCoefficient * ( m_bodyTemp - 20 );
    return boundedPositive( referenceResistance * factor, 1e-9 );
}

void Heater::openPlot() {
    m_dialog->setWindowTitle( idLabel() + " - " + tr( "Temperature" ) );
    {
        QMutexLocker locker( &m_stateMutex );
        m_dialog->updateData( m_historyTime, m_historyBody, m_historySensor, m_lastPower1,
                              m_lastPower2, thermalCapacity(), heatLossCoefficient(), sensorTimeConstant() );
        m_guiDirty = true;
    }
    m_dialog->show();
    m_dialog->raise();
    m_dialog->activateWindow();
}

void Heater::mouseDoubleClickEvent( QGraphicsSceneMouseEvent* event ) {
    if ( event->button() == Qt::LeftButton ) {
        event->accept();
        openPlot();
    } else
        Component::mouseDoubleClickEvent( event );
}

void Heater::contextMenu( QGraphicsSceneContextMenuEvent* event, QMenu* menu ) {
    QAction* plotAction = menu->addAction( tr( "Open temperature plot" ) );
    QObject::connect( plotAction, &QAction::triggered, [=]() { openPlot(); } );
    menu->addSeparator();
    Component::contextMenu( event, menu );
}

QPainterPath Heater::shape() const {
    QPainterPath path;
    path.addRoundedRect( QRectF( -64, -44, 128, 92 ), 8, 8 );
    return path;
}

void Heater::paint( QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget ) {
    Component::paint( painter, option, widget );
    painter->setRenderHint( QPainter::Antialiasing );

    const double heat = std::max( 0.0, std::min( ( m_displayTemp - m_ambientTemp ) / 300.0, 1.0 ) );
    QColor bodyColor( 175 + 70 * heat, 185 - 70 * heat, 195 - 115 * heat );
    QLinearGradient bodyGradient( 0, -44, 0, 44 );
    bodyGradient.setColorAt( 0, bodyColor.lighter( 125 ) );
    bodyGradient.setColorAt( 1, bodyColor.darker( 120 ) );
    painter->setPen( QPen( QColor( 65, 72, 78 ), 2 ) );
    painter->setBrush( bodyGradient );
    painter->drawRoundedRect( QRectF( -60, -44, 120, 88 ), 8, 8 );

    painter->setBrush( QColor( 32, 37, 42, 205 ) );
    painter->setPen( QPen( QColor( 220, 225, 230 ), 1 ) );
    painter->drawRoundedRect( QRectF( -56, -34, 40, 32 ), 5, 5 );
    painter->drawRoundedRect( QRectF( 16, -34, 40, 32 ), 5, 5 );

    auto drawHeater = [&]( double left, double right ) {
        QPainterPath path;
        path.moveTo( left, -18 );
        const int sections = 8;
        for ( int i = 1; i < sections; ++i ) {
            const double x = left + ( right - left ) * i / sections;
            path.lineTo( x, i & 1 ? -29 : -7 );
        }
        path.lineTo( right, -18 );
        painter->drawPath( path );
    };
    painter->setPen( QPen( QColor( 245, 95 + 80 * heat, 45 ), 3, Qt::SolidLine, Qt::RoundCap ) );
    drawHeater( -50, -22 );
    drawHeater( 22, 50 );

    painter->setPen( QPen( QColor( 70, 65, 60 ), 2 ) );
    painter->drawLine( QPointF( -48, 48 ), QPointF( -50, -18 ) );
    painter->drawLine( QPointF( -32, 48 ), QPointF( -22, -18 ) );
    painter->drawLine( QPointF( 32, 48 ), QPointF( 22, -18 ) );
    painter->drawLine( QPointF( 48, 48 ), QPointF( 50, -18 ) );

    painter->setPen( QPen( QColor( 220, 155, 30 ), 2 ) );
    painter->drawLine( QPointF( -8, 48 ), QPointF( -8, 12 ) );
    painter->drawLine( QPointF( -8, 12 ), QPointF( 0, 3 ) );
    painter->setPen( QPen( QColor( 50, 125, 185 ), 2 ) );
    painter->drawLine( QPointF( 8, 48 ), QPointF( 8, 12 ) );
    painter->drawLine( QPointF( 8, 12 ), QPointF( 0, 3 ) );
    painter->setPen( Qt::NoPen );
    painter->setBrush( QColor( 255, 225, 90 ) );
    painter->drawEllipse( QPointF( 0, 3 ), 4, 4 );

    const QFont baseFont = painter->font();
    QFont heaterFont = baseFont;
    heaterFont.setPointSizeF( heaterFont.pointSizeF() * 0.82 );
    painter->setFont( heaterFont );
    painter->setPen( QColor( 25, 25, 25 ) );
    painter->drawText( QRectF( -56, -43, 40, 10 ), Qt::AlignCenter, "HEAT1" );
    painter->drawText( QRectF( 16, -43, 40, 10 ), Qt::AlignCenter, "HEAT2" );

    const int pinX[] = { -48, -32, -8, 8, 32, 48 };
    const QString pinLabels[] = { "H1+", "H1-", "TC+", "TC-", "H2+", "H2-" };
    QFont pinFont = painter->font();
    pinFont.setPixelSize( 8 );
    painter->setFont( pinFont );
    for ( int i = 0; i < 6; ++i ) {
        painter->save();
        painter->translate( pinX[i] - 7, 44 );
        painter->rotate( -90 );
        painter->drawText( QRectF( 0, -5, 26, 9 ), Qt::AlignLeft | Qt::AlignVCenter, pinLabels[i] );
        painter->restore();
    }
    painter->setFont( baseFont );
    painter->setPen( QColor( 40, 45, 50 ) );
    painter->drawText( QRectF( -22, 10, 44, 14 ), Qt::AlignCenter, "TC" );

    Component::paintSelected( painter );
}
