/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QDebug>
#include <QPainter>

#include "esp32pin.h"
#include "qemumodule.h"
#include "simulator.h"

Esp32OutputSignal::Esp32OutputSignal() : eElement( QString() ) { }

void Esp32OutputSignal::connectPad( Esp32Pin* pin ) {
    if ( !m_pads.contains( pin ) )
        m_pads.append( pin );
    pin->outputSignalChanged( this );
}

void Esp32OutputSignal::disconnectPad( Esp32Pin* pin ) {
    m_pads.removeOne( pin );
    if ( m_pads.isEmpty() && m_statePending ) {
        Simulator::self()->cancelEvents( this );
        m_statePending = false;
    }
}

void Esp32OutputSignal::setState( bool state ) {
    if ( m_statePending ) {
        Simulator::self()->cancelEvents( this );
        m_statePending = false;
    }
    if ( m_state == state )
        return;
    m_state = state;
    for ( Esp32Pin* pin : m_pads )
        pin->outputSignalChanged( this );
}

void Esp32OutputSignal::scheduleState( bool state, uint64_t delay ) {
    if ( m_statePending )
        Simulator::self()->cancelEvents( this );
    if ( !delay ) {
        m_statePending = false;
        setState( state );
        return;
    }
    m_scheduledState = state;
    m_statePending = true;
    Simulator::self()->addEvent( delay, this );
}

void Esp32OutputSignal::resetState( bool state ) {
    if ( m_statePending )
        Simulator::self()->cancelEvents( this );
    m_statePending = false;
    setState( state );
}

void Esp32OutputSignal::runEvent() {
    m_statePending = false;
    setState( m_scheduledState );
}

void Esp32OutputSignal::setOutputEnable( bool enabled ) {
    if ( m_outputEnable == enabled )
        return;
    m_outputEnable = enabled;
    for ( Esp32Pin* pin : m_pads )
        pin->outputSignalChanged( this );
}

void Esp32OutputSignal::setDriveMode( DriveMode mode ) {
    if ( m_driveMode == mode )
        return;
    m_driveMode = mode;
    for ( Esp32Pin* pin : m_pads )
        pin->outputSignalChanged( this );
}

Esp32Pin* Esp32InputSignal::activePin() const {
    return m_matrixSelected ? m_matrixPin : m_ioMuxPin;
}

void Esp32InputSignal::beginRouteChange() {
    Esp32Pin* pin = activePin();
    if ( pin && m_listener && m_watching )
        pin->changeCallBack( m_listener, false );
}

void Esp32InputSignal::endRouteChange() {
    Esp32Pin* pin = activePin();
    if ( pin && m_listener && m_watching )
        pin->changeCallBack( m_listener, true );
}

void Esp32InputSignal::setIoMuxRoute( Esp32Pin* pin ) {
    beginRouteChange();
    m_ioMuxPin = pin;
    endRouteChange();
}

void Esp32InputSignal::clearIoMuxRoute( Esp32Pin* pin ) {
    if ( pin && m_ioMuxPin != pin )
        return;
    setIoMuxRoute( nullptr );
}

void Esp32InputSignal::setMatrixRoute( Esp32Pin* pin, bool inverted ) {
    beginRouteChange();
    m_matrixPin = pin;
    m_matrixConstant = NoConstant;
    m_matrixInverted = inverted;
    endRouteChange();
}

void Esp32InputSignal::setMatrixConstant( Constant constant, bool inverted ) {
    beginRouteChange();
    m_matrixPin = nullptr;
    m_matrixConstant = constant;
    m_matrixInverted = inverted;
    endRouteChange();
}

void Esp32InputSignal::selectMatrix( bool selected ) {
    beginRouteChange();
    m_matrixSelected = selected;
    endRouteChange();
}

void Esp32InputSignal::clearMatrixRoute() {
    beginRouteChange();
    m_matrixPin = nullptr;
    m_matrixConstant = NoConstant;
    m_matrixInverted = false;
    m_matrixSelected = false;
    endRouteChange();
}

void Esp32InputSignal::clearRoutes() {
    beginRouteChange();
    m_ioMuxPin = nullptr;
    m_matrixPin = nullptr;
    m_matrixConstant = NoConstant;
    m_matrixInverted = false;
    m_matrixSelected = false;
    endRouteChange();
}

bool Esp32InputSignal::state() const {
    if ( !m_matrixSelected )
        return m_ioMuxPin ? m_ioMuxPin->getInpState() : true;
    bool state = true;
    if ( m_matrixPin )
        state = m_matrixPin->getInpState();
    else if ( m_matrixConstant != NoConstant )
        state = m_matrixConstant == ConstantHigh;
    return state ^ m_matrixInverted;
}

bool Esp32InputSignal::routed() const {
    if ( !m_matrixSelected )
        return m_ioMuxPin != nullptr;
    return m_matrixPin || m_matrixConstant != NoConstant;
}

void Esp32InputSignal::watch( eElement* listener, bool enabled ) {
    Esp32Pin* pin = activePin();
    if ( pin && m_listener && m_watching )
        pin->changeCallBack( m_listener, false );
    m_listener = listener;
    m_watching = enabled;
    pin = activePin();
    if ( pin && m_listener && m_watching )
        pin->changeCallBack( m_listener, true );
}

Esp32Pin::Esp32Pin( int i, QString id, QemuDevice* mcu, IoPin* dummyPin )
    : IoPin( 0, QPoint( 0, 0 ), mcu->getId() + "-" + id, i, mcu, input )
//, QemuModule( mcu, i )
{
    //m_id     = id;

    m_pullResistance = 4.5e4; // Nominal ESP weak pull resistance.

    double vdd = 3.3; //m_port->getMcu()->vdd();
    m_outHighV = vdd;
    m_inpHighV = vdd / 2;
    m_inpLowV = vdd / 2;

    m_pullUp = 0;
    m_pullDown = 0;
    m_iomuxPullUp = false;
    m_iomuxPullDown = false;
    m_rtcPullUp = false;
    m_rtcPullDown = false;
    m_rtcPullControl = false;

    m_pinMask = 1 << i;

    m_dummyPin = dummyPin;

    m_pinLabel = id;
    m_iomuxIndex = 2;
    m_matrixMuxIndex = 2;
    m_matrixOutConfig = 256;
    m_gpioState = false;
    m_gpioOutputEnable = false;

    for ( int i = 0; i < 6; ++i )
        m_iomuxFuncs[i] = { nullptr, nullptr, "- -" };
}
Esp32Pin::~Esp32Pin() { }

void Esp32Pin::initialize() {
    Pin::setLabelText( m_pinLabel );
    Pin::setLabelColor( QColor( 250, 250, 200 ) );

    //m_isAnalog = false;
    ////m_portState = false;

    //double vdd = 3.3; //m_port->getMcu()->vdd();
    //m_outHighV = vdd;
    //m_inpHighV = vdd/2;
    //m_inpLowV  = vdd/2;
    //

    IoPin::initialize();
}

void Esp32Pin::updateStep() {
    IoPin::updateStep();
    if ( Simulator::self()->isRunning() ) {
        QString label = m_iomuxFuncs[m_iomuxIndex].label;
        if ( label.isEmpty() || label == "---" || label == "- -" )
            label = m_pinLabel;
        Pin::setLabelText( label );
    } else {
        Pin::setLabelText( m_pinLabel );
    }
    //Simulator::self()->remFromUpdateList( this );
}

void Esp32Pin::stamp() {
    //m_alternate = false;
    //m_analog = false;
    //m_pull = false;

    setInternalPullup( false );
    setInternalPulldown( false );
    if ( m_rtcPullControl ) {
        setRtcPullup( false );
        setRtcPulldown( false );
    }
    m_inputEn = 0;
    setPinMode( input );

    m_iomuxIndex = -1;
    selectIoMuxFunc( 0 );

    //setPull( true );
    //updateStep();

    //update();

    IoPin::stamp();
}

void Esp32Pin::voltChanged() {
    bool oldState = m_inpState;
    bool newState = IoPin::getInpState();

    if ( oldState == newState )
        return;

    // while( m_arena->qemuAction )        // Wait for previous action executed
    // {
    //     ; /// TODO: add timeout
    // }
    // m_arena->data8 = m_port-1;
    // m_arena->mask8 = m_number;
    // m_arena->data16 = newState;
    // m_arena->qemuAction = SIM_GPIO_IN;
}

void Esp32Pin::setPinMode( pinMode_t mode ) {
    IoPin::setPinMode( mode );
    changeCallBack( this, mode == input );
}

//void Esp32Pin::setPull( bool p )
//{
//    if( m_pull == p ) return;
//    m_pull = p;
//    setOutState( m_outState );
//}
//
//bool Esp32Pin::setAlternate( bool a ) // If changing to Not Alternate, return false
//{
//    if( m_alternate == a ) return true;
//    m_alternate = a;
//    if( a ) qDebug() << "Esp32Pin::setAlternate" << this->m_id;
//    return a;
//}
//
//void Esp32Pin::setAnalog( bool a ) /// TODO: if changing to Not Analog, return false
//{
//    if( m_analog == a ) return;
//    m_analog = a;
//}

void Esp32Pin::setPortState( bool high ) // Set output from Port register
{
    //if( m_alternate ) return;
    setPinState( high );
}

void Esp32Pin::setGpioState( bool high ) {
    m_gpioState = high;
    if ( m_iomuxIndex == m_matrixMuxIndex )
        refreshMatrixOutput();
    else
        setPinState( high );
}

void Esp32Pin::setGpioOutputEnable( bool enabled ) {
    m_gpioOutputEnable = enabled;
    if ( m_iomuxIndex == m_matrixMuxIndex )
        refreshMatrixOutput();
    else
        setPinMode( enabled ? output : input );
}

void Esp32Pin::setOutState( bool high ) // Set output from Alternate (peripheral)
{
    //if( m_alternate )
    setPinState( high );
}

void Esp32Pin::scheduleState( bool high, uint64_t time ) {
    //if( m_alternate )
    IoPin::scheduleState( high, time );
}

void Esp32Pin::setPinState( bool high ) // Set Output to Hight or Low
{
    m_outState = m_nextState = high;
    //if( m_pinMode < openCo  || m_stateZ ) return;

    if ( m_inverted )
        high = !high;

    switch ( m_pinMode ) {
    case undef_mode:
        return;
    case input:
        /// if( m_pull ){
        ///     m_outVolt = high ? m_outHighV : m_outLowV;
        ///     ePin::stampCurrent( m_outVolt*m_pullAdmit );
        /// }
        break;
    case output:
        m_outVolt = high ? m_outHighV : m_outLowV;
        ePin::stampCurrent( m_outVolt * m_admit );
        break;
    case openCo:
        m_gndAdmit = high ? 1 / 1e8 : 1 / m_outputImp;
        updtState();
        break;
    default:
        return;
    }
}

void Esp32Pin::setIoMuxFuncs( QList<funcPin> functions ) // Set IO_MUX functions for this pad
{
    for ( int i = 0; i < 6; ++i ) {
        funcPin fp = functions.at( i );
        if ( fp.label.isEmpty() )
            fp.label = m_pinLabel;

        m_iomuxFuncs[i] = fp;
    }
}

void Esp32Pin::disconnectIoMuxFunc() {
    if ( m_iomuxIndex < 6 ) {
        funcPin& oldFunc = m_iomuxFuncs[m_iomuxIndex];
        if ( oldFunc.outputSignal )
            oldFunc.outputSignal->disconnectPad( this );
        if ( oldFunc.inputSignal )
            oldFunc.inputSignal->clearIoMuxRoute( this );
        if ( oldFunc.pinPointer && *oldFunc.pinPointer == this )
            *oldFunc.pinPointer = m_dummyPin;

        QemuModule* mod = oldFunc.module;
        if ( mod && oldFunc.inputSignal )
            mod->connected( oldFunc.inputSignal->routed() );
        else if ( mod && oldFunc.pinPointer )
            mod->connected( false );
    }
    m_iomuxIndex = 6;
}

void Esp32Pin::selectIoMuxFunc( uint8_t func ) // Select IO_MUX function
{
    if ( func > 5 ) {
        qDebug() << this->pinId() << "Selected func ERROR" << func;
        return;
    }
    disconnectIoMuxFunc();
    if ( m_iomuxFuncs[func].label == "GPIO" )
        m_iomuxFuncs[func].label = m_pinLabel;

    Simulator::self()->addToUpdateList( this ); /// FIXME
    m_iomuxIndex = func;

    if ( m_iomuxFuncs[func].outputSignal ) {
        m_iomuxFuncs[func].outputSignal->connectPad( this );
    }
    if ( m_iomuxFuncs[func].inputSignal ) {
        m_iomuxFuncs[func].inputSignal->setIoMuxRoute( this );
        QemuModule* mod = m_iomuxFuncs[func].module;
        if ( mod )
            mod->connected( m_iomuxFuncs[func].inputSignal->routed() );
    } else if ( m_iomuxFuncs[func].pinPointer ) {
        //qDebug() << this->pinId() << "Selected func"<< func << m_iomuxPin[func].label;
        *m_iomuxFuncs[func].pinPointer = this;

        QemuModule* mod = m_iomuxFuncs[func].module;
        if ( mod )
            mod->connected( true );
    }
    if ( func == m_matrixMuxIndex )
        refreshMatrixOutput();
    if ( m_iomuxFuncs[func].pinPointer || m_iomuxFuncs[func].label == m_pinLabel ) {
        Pin::setLabelColor( QColor( 255, 255, 100 ) );
    } else {
        Pin::setLabelColor( QColor( 250, 250, 200 ) );
    }
    //qDebug() << this->pinId() << "Selected func"<< func << m_iomuxFuncs[func].label;
    //update();
}

void Esp32Pin::setMatrixFunc( uint16_t val, funcPin func ) // Set Function for GPIO Matrix: index=2
{
    //qDebug() << this->pinId() << "Matrix function"<< func.label<< (val & 0b111000000000);
    m_iomuxFuncs[m_matrixMuxIndex] = func;
    if ( m_iomuxIndex == m_matrixMuxIndex )
        selectIoMuxFunc( m_matrixMuxIndex );
}

void Esp32Pin::setMatrixOutput( uint16_t val, funcPin func ) {
    bool selected = m_iomuxIndex == m_matrixMuxIndex;
    if ( selected )
        disconnectIoMuxFunc();
    m_matrixOutConfig = val;
    m_iomuxFuncs[m_matrixMuxIndex] = func;
    if ( selected )
        selectIoMuxFunc( m_matrixMuxIndex );
}

void Esp32Pin::outputSignalChanged( Esp32OutputSignal* signal ) {
    if ( m_iomuxIndex == m_matrixMuxIndex && m_iomuxFuncs[m_matrixMuxIndex].outputSignal == signal )
        refreshMatrixOutput();
}

void Esp32Pin::resetMatrixOutput() {
    funcPin& matrixFunc = m_iomuxFuncs[m_matrixMuxIndex];
    if ( matrixFunc.outputSignal )
        matrixFunc.outputSignal->disconnectPad( this );
    if ( matrixFunc.inputSignal )
        matrixFunc.inputSignal->clearIoMuxRoute( this );
    if ( matrixFunc.pinPointer && *matrixFunc.pinPointer == this )
        *matrixFunc.pinPointer = m_dummyPin;

    m_matrixOutConfig = 256;
    m_gpioState = false;
    m_gpioOutputEnable = false;
    m_iomuxFuncs[m_matrixMuxIndex] = { nullptr, nullptr, "GPIO" };
    if ( m_iomuxIndex == m_matrixMuxIndex )
        refreshMatrixOutput();
}

void Esp32Pin::resetRoutes() {
    disconnectIoMuxFunc();
    resetMatrixOutput();
}

void Esp32Pin::refreshMatrixOutput() {
    if ( m_iomuxIndex != m_matrixMuxIndex )
        return;

    Esp32OutputSignal* signal = m_iomuxFuncs[m_matrixMuxIndex].outputSignal;
    bool state = signal ? signal->state() : m_gpioState;
    bool outputEnable = signal ? signal->outputEnable() : m_gpioOutputEnable;

    if ( m_matrixOutConfig & ( 1 << 10 ) )
        outputEnable = m_gpioOutputEnable;
    if ( m_matrixOutConfig & ( 1 << 11 ) )
        outputEnable = !outputEnable;
    if ( m_matrixOutConfig & ( 1 << 9 ) )
        state = !state;

    pinMode_t mode = output;
    if ( signal && signal->driveMode() == Esp32OutputSignal::OpenDrain )
        mode = openCo;
    setPinMode( outputEnable ? mode : input );
    setPinState( state );
}

void Esp32Pin::writeIoMuxReg( uint16_t value ) {
    // Sleep bits 0-6
    // PD bit 7
    // PU bit 8
    // IE bit 9
    // Drive bits 10-11
    // function bits 12-14

    bool puld = ( value >> 7 ) & 1;
    setInternalPulldown( puld );

    uint8_t pulu = ( value >> 8 ) & 1;
    setInternalPullup( pulu );

    m_inputEn = ( value >> 9 ) & 1;

    uint8_t func = ( value >> 12 ) & 7;
    if ( m_iomuxIndex == func )
        return;
    selectIoMuxFunc( func );
}

void Esp32Pin::setInternalPullup( bool enabled ) {
    if ( m_iomuxPullUp == enabled )
        return;
    m_iomuxPullUp = enabled;
    updateInternalPullup();
}

void Esp32Pin::setInternalPulldown( bool enabled ) {
    if ( m_iomuxPullDown == enabled )
        return;
    m_iomuxPullDown = enabled;
    updateInternalPulldown();
}

void Esp32Pin::setRtcPullup( bool enabled ) {
    bool sourceChanged = !m_rtcPullControl;
    m_rtcPullControl = true;
    if ( !sourceChanged && m_rtcPullUp == enabled )
        return;
    m_rtcPullUp = enabled;
    updateInternalPullup();
}

void Esp32Pin::setRtcPulldown( bool enabled ) {
    bool sourceChanged = !m_rtcPullControl;
    m_rtcPullControl = true;
    if ( !sourceChanged && m_rtcPullDown == enabled )
        return;
    m_rtcPullDown = enabled;
    updateInternalPulldown();
}

void Esp32Pin::updateInternalPullup() {
    bool enabled = m_rtcPullControl ? m_rtcPullUp : m_iomuxPullUp;
    if ( m_pullUp == enabled )
        return;
    m_pullUp = enabled;
    IoPin::setPullup( enabled ? m_pullResistance : 0 );
}

void Esp32Pin::updateInternalPulldown() {
    bool enabled = m_rtcPullControl ? m_rtcPullDown : m_iomuxPullDown;
    if ( m_pullDown == enabled )
        return;
    m_pullDown = enabled;
    IoPin::setPulldown( enabled ? m_pullResistance : 0 );
}

void Esp32Pin::writePinReg( uint32_t value ) { }

void Esp32Pin::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) {
    if ( !isVisible() )
        return;
    Pin::paint( p, o, w );

    if ( !m_pullUp && !m_pullDown )
        return;
    if ( m_pinMode > openCo )
        return;

    // Draw pullUp/Down dot

    if ( m_pullUp )
        p->setBrush( QColor( 255, 180, 0 ) );
    else
        p->setBrush( QColor( 0, 180, 255 ) );

    QPen pen = p->pen();
    pen.setWidthF( 0 );
    p->setPen( pen );
    int start = ( m_length > 4 ) ? m_length - 4.5 : 3.5;
    QRectF rect( start + 0.6, -1.5, 3, 3 );
    p->drawEllipse( rect );
}
