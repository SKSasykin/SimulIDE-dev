/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <algorithm>

#include "esp32spi.h"
#include "iopin.h"
#include "qemudevice.h"
#include "simulator.h"

Esp32Spi::Esp32Spi( QemuDevice* mcu, QString name, int n, uint32_t* clk, uint64_t memStart, uint64_t memEnd,
                    bool modern, bool esp8266 )
    : QemuSpi( mcu, name, n, clk, memStart, memEnd ), m_modern( modern ), m_esp8266( esp8266 ) {
    m_data.fill( 0 );
    m_transactionActive = false;
    m_dataIndex = 0;
    m_dataBytes = 0;
}
Esp32Spi::~Esp32Spi() { }

void Esp32Spi::reset() {
    Simulator::self()->cancelEvents( this );
    m_transactionActive = false;
    m_dataIndex = 0;
    m_dataBytes = 0;
    m_data.fill( 0 );
    setMode( SPI_OFF );
    m_ckOutput.resetState( false );
    m_miOutput.resetState( true );
    m_moOutput.resetState( true );
    m_ssOutput.resetState( true );
    m_clock = false;
    updateOutputEnables();
}

void Esp32Spi::connected( bool c ) {
    (void)c;
}

void Esp32Spi::setMode( spiMode_t mode ) {
    SpiModule::setMode( mode );
    updateOutputEnables();
}

void Esp32Spi::ssChanged( bool enable ) {
    (void)enable;
    updateOutputEnables();
}

void Esp32Spi::writeRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    uint32_t dataBase = m_modern ? 0x98 : ( m_esp8266 ? 0x40 : 0x80 );
    write();

    if ( offset >= dataBase && offset < dataBase + 64 ) {
        m_data[( offset - dataBase ) / 4] = m_eventValue;
        return;
    }

    if ( offset == ( m_modern ? 0x0C : 0x18 ) )
        configureClock();
    else if ( offset == ( m_modern ? 0x20 : ( m_esp8266 ? 0x2C : 0x34 ) )
              || offset == ( m_modern ? 0x10 : 0x1C ) )
        configureMode();
    else if ( offset == ( m_modern ? 0xE0 : ( m_esp8266 ? 0x30 : 0x38 ) ) ) {
        setMode( m_eventValue & ( 1 << ( m_modern ? 26 : 30 ) ) ? SPI_SLAVE : SPI_MASTER );
        if ( !m_modern && ( m_eventValue & ( 1 << 4 ) ) )
            writeMem( m_eventAddress, readMem( m_eventAddress ) & ~( 1 << 4 ) );
    } else if ( m_modern && offset == 0x38 && ( m_eventValue & ( 1 << 12 ) ) ) {
        uint32_t rawAddress = m_memStart + 0x3C;
        writeMem( rawAddress, readMem( rawAddress ) & ~( 1 << 12 ) );
    } else if ( offset == 0 ) {
        if ( m_modern && ( m_eventValue & ( 1 << 23 ) ) )
            writeMem( m_eventAddress, readMem( m_eventAddress ) & ~( 1 << 23 ) );
        if ( m_eventValue & ( 1 << ( m_modern ? 24 : 18 ) ) )
            startUserTransaction();
    }
}

void Esp32Spi::readRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    uint32_t dataBase = m_modern ? 0x98 : ( m_esp8266 ? 0x40 : 0x80 );
    uint32_t value = read();
    if ( offset >= dataBase && offset < dataBase + 64 )
        value = m_data[( offset - dataBase ) / 4];
    m_arena->regData = value;
    m_arena->qemuAction = SIM_READ;
}

void Esp32Spi::endTransaction() {
    SpiModule::endTransaction();
    if ( !m_transactionActive )
        return;

    uint8_t byte = m_srReg;
    uint8_t word = m_dataIndex / 4;
    uint8_t shift = ( m_dataIndex % 4 ) * 8;
    m_data[word] = ( m_data[word] & ~( 0xFFu << shift ) ) | ( uint32_t( byte ) << shift );
    ++m_dataIndex;

    if ( m_dataIndex < m_dataBytes ) {
        loadByte();
        StartTransaction();
        return;
    }

    m_transactionActive = false;
    uint32_t cmdAddress = m_memStart;
    writeMem( cmdAddress, readMem( cmdAddress ) & ~( 1 << ( m_modern ? 24 : 18 ) ) );
    uint32_t doneAddress = m_memStart + ( m_modern ? 0x3C : ( m_esp8266 ? 0x30 : 0x38 ) );
    writeMem( doneAddress, readMem( doneAddress ) | ( 1 << ( m_modern ? 12 : 4 ) ) );
    if ( m_SS )
        driveSelect( true );
}

void Esp32Spi::configureClock() {
    uint32_t value = m_eventValue;
    uint32_t divider = 1;
    if ( !( value & 0x80000000 ) ) {
        uint32_t pre = ( value >> 18 ) & ( m_modern ? 0xF : 0x1FFF );
        uint32_t n = ( value >> 12 ) & 0x3F;
        divider = ( pre + 1 ) * ( n + 1 );
    }
    if ( m_frequency && *m_frequency )
        m_clockPeriod = uint64_t( divider ) * 1000000000000ULL / *m_frequency / 2;
}

void Esp32Spi::configureMode() {
    uint32_t user = readMem( m_memStart + ( m_modern ? 0x10 : 0x1C ) );
    uint32_t misc = readMem( m_memStart + ( m_modern ? 0x20 : ( m_esp8266 ? 0x2C : 0x34 ) ) );
    bool idleHigh = misc & ( 1 << 29 );
    bool clockOutEdge = user & ( 1 << ( m_modern ? 9 : 7 ) );
    bool phase = clockOutEdge != idleHigh;

    m_leadEdge = idleHigh ? Clock_Falling : Clock_Rising;
    m_tailEdge = idleHigh ? Clock_Rising : Clock_Falling;
    m_sampleEdge = phase ? m_tailEdge : m_leadEdge;
    uint32_t control = readMem( m_memStart + 0x08 );
    m_lsbFirst = control & ( m_modern ? ( 3 << 25 ) : ( 1 << 26 ) );
    driveClock( idleHigh );
    m_clock = idleHigh;
}

void Esp32Spi::startUserTransaction() {
    if ( m_transactionActive )
        return;
    if ( m_mode != SPI_MASTER )
        setMode( SPI_MASTER );
    bool endpointsRouted = m_ckOutput.routed() && m_moOutput.routed() && m_miInput.routed();
    if ( m_SS )
        endpointsRouted = endpointsRouted && m_ssOutput.routed();
    if ( m_mode != SPI_MASTER || ( m_esp8266 ? ( !m_dataInPin || !m_dataOutPin || !m_clkPin )
                                                  : !endpointsRouted ) ) {
        writeMem( m_memStart, readMem( m_memStart ) & ~( 1 << ( m_modern ? 24 : 18 ) ) );
        return;
    }

    uint32_t user = readMem( m_memStart + ( m_modern ? 0x10 : 0x1C ) );
    uint32_t bits = 0;
    if ( m_modern ) {
        bits = ( readMem( m_memStart + 0x1C ) & 0x3FFFF ) + 1;
    } else if ( m_esp8266 ) {
        uint32_t user1 = readMem( m_memStart + 0x20 );
        if ( user & ( 1 << 27 ) )
            bits = ( ( user1 >> 17 ) & 0x1FF ) + 1;
        if ( user & ( 1 << 28 ) )
            bits = std::max( bits, ( ( user1 >> 8 ) & 0x1FF ) + 1 );
    } else {
        if ( user & ( 1 << 27 ) )
            bits = ( readMem( m_memStart + 0x28 ) & 0xFFFFFF ) + 1;
        if ( user & ( 1 << 28 ) )
            bits = std::max( bits, ( readMem( m_memStart + 0x2C ) & 0xFFFFFF ) + 1 );
    }
    m_dataBytes = std::min<uint32_t>( 64, ( bits + 7 ) / 8 );
    if ( !m_dataBytes ) {
        writeMem( m_memStart, readMem( m_memStart ) & ~( 1 << ( m_modern ? 24 : 18 ) ) );
        return;
    }

    configureClock();
    configureMode();
    if ( m_esp8266 ) {
        m_MOSI->setPinMode( output );
        m_MISO->setPinMode( input );
        m_clkPin->setPinMode( output );
        if ( m_SS )
            m_SS->setPinMode( output );
    }
    m_dataIndex = 0;
    m_transactionActive = true;
    uint32_t doneAddress = m_memStart + ( m_modern ? 0x3C : ( m_esp8266 ? 0x30 : 0x38 ) );
    writeMem( doneAddress, readMem( doneAddress ) & ~( 1 << ( m_modern ? 12 : 4 ) ) );
    if ( m_SS )
        driveSelect( false );
    loadByte();
    StartTransaction();
}

void Esp32Spi::loadByte() {
    uint8_t word = m_dataIndex / 4;
    uint8_t shift = ( m_dataIndex % 4 ) * 8;
    m_srReg = ( m_data[word] >> shift ) & 0xFF;
}

void Esp32Spi::updateOutputEnables() {
    if ( m_esp8266 )
        return;

    bool master = m_mode == SPI_MASTER;
    bool slave = m_mode == SPI_SLAVE;
    m_ckOutput.setOutputEnable( master );
    m_moOutput.setOutputEnable( master );
    m_ssOutput.setOutputEnable( master );
    m_miOutput.setOutputEnable( slave && ( !m_useSS || !sampleSelect() ) );
}

void Esp32Spi::driveClock( bool state ) {
    if ( m_esp8266 )
        SpiModule::driveClock( state );
    else
        m_ckOutput.setState( state );
}

void Esp32Spi::driveData( bool state ) {
    if ( m_esp8266 )
        SpiModule::driveData( state );
    else if ( m_mode == SPI_SLAVE )
        m_miOutput.setState( state );
    else
        m_moOutput.setState( state );
}

void Esp32Spi::driveSelect( bool state ) {
    if ( m_esp8266 )
        SpiModule::driveSelect( state );
    else
        m_ssOutput.setState( state );
}

bool Esp32Spi::sampleClock() {
    return m_esp8266 ? SpiModule::sampleClock() : m_ckInput.state();
}

bool Esp32Spi::sampleData() {
    if ( m_esp8266 )
        return SpiModule::sampleData();
    return m_mode == SPI_SLAVE ? m_moInput.state() : m_miInput.state();
}

bool Esp32Spi::sampleSelect() {
    return m_esp8266 ? SpiModule::sampleSelect() : m_ssInput.state();
}

bool Esp32Spi::drivenClock() {
    return m_esp8266 ? SpiModule::drivenClock() : m_ckOutput.state();
}

void Esp32Spi::watchClock( eElement* listener, bool enabled ) {
    if ( m_esp8266 )
        SpiModule::watchClock( listener, enabled );
    else
        m_ckInput.watch( listener, enabled );
}

void Esp32Spi::watchSelect( eElement* listener, bool enabled ) {
    if ( m_esp8266 )
        SpiModule::watchSelect( listener, enabled );
    else
        m_ssInput.watch( listener, enabled );
}
