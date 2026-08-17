/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "esp32twi.h"
#include "iopin.h"
#include "qemudevice.h"
#include "simulator.h"

Esp32Twi::Esp32Twi( QemuDevice* mcu, QString name, int n, uint32_t* clk, uint64_t memStart, uint64_t memEnd,
                    bool modern, int interrupt )
    : QemuTwi( mcu, name, n, clk, memStart, memEnd ), m_modern( modern ), m_interrupt( interrupt ) {
    reset();
}
Esp32Twi::~Esp32Twi() { }

void Esp32Twi::reset() {
    Simulator::self()->cancelEvents( this );
    if ( m_scl && m_sda ) {
        m_scl->scheduleState( true, 0 );
        m_sda->scheduleState( true, 0 );
        setMode( TWI_OFF );
    } else {
        m_mode = TWI_OFF;
        m_i2cState = I2C_IDLE;
        m_toggleScl = false;
    }
    m_twiState = TWI_NO_STATE;
    m_nextState = TWI_NO_STATE;
    m_txFifo.clear();
    m_rxFifo.clear();
    m_busy = false;
    m_busBusy = false;
    m_expectAddress = false;
    m_ackCheck = false;
    m_commandIndex = 0;
    m_commandCount = m_modern ? 8 : 16;
    m_remaining = 0;
    m_interruptRaw = 0;
    m_interruptEnable = 0;
}

void Esp32Twi::connected( bool c ) {
    m_clkPin = m_scl;
}

void Esp32Twi::writeRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    write();

    switch ( offset ) {
    case 0x00:
        setPeriod();
        break; // LOW_PERIOD
    case 0x04:
        writeCTR();
        break; // I2C_CTR
    case 0x18: // FIFO_CONF
        if ( m_eventValue & ( 1 << 12 ) )
            m_rxFifo.clear();
        if ( m_eventValue & ( 1 << 13 ) )
            m_txFifo.clear();
        break;
    case 0x1C: // FIFO_DATA
        if ( m_txFifo.size() < 32 )
            m_txFifo.push_back( m_eventValue & 0xFF );
        else
            m_interruptRaw |= 1 << 11;
        break;
    case 0x24: // INT_CLR
        m_interruptRaw &= ~m_eventValue;
        break;
    case 0x28: // INT_ENA
        m_interruptEnable = m_eventValue;
        break;
    default:
        break;
    }
    updateInterrupt();
}

void Esp32Twi::readRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    uint32_t value = read();
    switch ( offset ) {
    case 0x08: // I2C_STATUS
        value = ( m_busBusy ? 1 << 4 : 0 ) | ( uint32_t( m_rxFifo.size() ) << 8 )
                | ( uint32_t( m_txFifo.size() ) << 18 );
        break;
    case 0x1C: // FIFO_DATA
        if ( m_rxFifo.empty() ) {
            value = 0;
            m_interruptRaw |= 1 << 12;
        } else {
            value = m_rxFifo.front();
            m_rxFifo.pop_front();
        }
        break;
    case 0x20: // INT_RAW
        value = m_interruptRaw;
        break;
    case 0x2C: // INT_STATUS
        value = m_interruptRaw & m_interruptEnable;
        break;
    default:
        break;
    }
    m_arena->regData = value;
    m_arena->qemuAction = SIM_READ;
}

void Esp32Twi::writeCTR() {
    uint32_t data = m_eventValue;

    // bit 0: I2C_SDA_FORCE_OUT 0: direct output; 1: open drain output.
    pinMode_t sdaMode = ( data & 1 << 0 ) ? openCo : output;
    this->m_sda->setPinMode( sdaMode );

    // bit 1: I2C_SCL_FORCE_OUT 0: direct output; 1: open drain output.
    pinMode_t sclMode = ( data & 1 << 1 ) ? openCo : output;
    this->m_scl->setPinMode( sclMode );

    // bit 2: I2C_SAMPLE_SCL_LEVEL 1: sample SDA on SCL low; 0: sample SDA on SCL high.

    // bit 4: I2C_MS_MODE 1: I2C Master. 0: I2C Slave.
    twiMode_t mode = ( data & 1 << 4 ) ? TWI_MASTER : TWI_SLAVE;
    if ( m_mode != mode )
        setMode( mode );

    // bit 5: I2C_TRANS_START Set this bit to start sending the data in txfifo.
    if ( data & 1 << 5 )
        startTransaction();

    // bit 6: I2C_TX_LSB_FIRST 1: send LSB; 0: send MSB.
    // bit 7: I2C_RX_LSB_FIRST 1: receive LSB; 0: receive MSB.
}

void Esp32Twi::startTransaction() {
    if ( m_busy || m_mode != TWI_MASTER )
        return;

    m_busy = true;
    m_expectAddress = false;
    m_commandIndex = 0;
    m_remaining = 0;
    m_interruptRaw &= ~( ( 1 << 3 ) | ( 1 << 7 ) | ( 1 << 10 ) );
    runCommand();
}

void Esp32Twi::runCommand() {
    while ( m_busy && m_commandIndex < m_commandCount ) {
        uint32_t address = m_memStart + 0x58 + m_commandIndex * 4;
        uint32_t command = readMem( address );
        uint8_t opcode = ( command >> 11 ) & 7;
        m_remaining = command & 0xFF;
        m_ackCheck = command & ( 1 << 8 );

        uint8_t rstartOpcode = m_modern ? 6 : 0;
        uint8_t readOpcode = m_modern ? 3 : 2;
        uint8_t stopOpcode = m_modern ? 2 : 3;

        if ( opcode == rstartOpcode ) {
            m_busBusy = true;
            m_expectAddress = true;
            masterStart();
            return;
        } else if ( opcode == 1 ) { // WRITE
            if ( !m_remaining ) {
                commandDone();
                continue;
            }
            writeNextByte();
            return;
        } else if ( opcode == readOpcode ) { // READ
            if ( !m_remaining ) {
                commandDone();
                continue;
            }
            readNextByte();
            return;
        } else if ( opcode == stopOpcode ) { // STOP
            masterStop();
            return;
        } else if ( opcode == 4 ) { // END
            commandDone();
            finishTransaction( 1 << 3, false );
            return;
        } else {
            finishTransaction();
            return;
        }
    }
    finishTransaction();
}

bool Esp32Twi::writeNextByte() {
    if ( m_txFifo.empty() ) {
        finishTransaction( 1 << 6 );
        return false;
    }

    uint8_t data = m_txFifo.front();
    m_txFifo.pop_front();
    bool addressByte = m_expectAddress;
    if ( addressByte ) {
        m_write = ( data & 1 ) == 0;
        m_expectAddress = false;
    }
    masterWrite( data, addressByte, m_write );
    return true;
}

void Esp32Twi::readNextByte() {
    uint32_t command = readMem( m_memStart + 0x58 + m_commandIndex * 4 );
    masterRead( ( command & ( 1 << 10 ) ) == 0 );
}

void Esp32Twi::commandDone() {
    uint32_t address = m_memStart + 0x58 + m_commandIndex * 4;
    writeMem( address, readMem( address ) | 0x80000000 );
    ++m_commandIndex;
    m_remaining = 0;
}

void Esp32Twi::finishTransaction( uint32_t interruptMask, bool releaseBus ) {
    m_busy = false;
    if ( releaseBus )
        m_busBusy = false;
    uint32_t ctr = readMem( m_memStart + 0x04 ) & ~( 1 << 5 );
    writeMem( m_memStart + 0x04, ctr );
    m_interruptRaw |= interruptMask;
    updateInterrupt();
}

void Esp32Twi::updateInterrupt() {
    if ( m_interrupt >= 0 )
        setInterrupt( m_interrupt, ( m_interruptRaw & m_interruptEnable ) != 0 );
}

void Esp32Twi::setPeriod() {
    uint32_t cycles = m_eventValue & ( m_modern ? 0x1FF : 0x3FFF );
    if ( cycles && m_frequency && *m_frequency )
        m_clockPeriod = uint64_t( cycles ) * 1000000000000ULL / *m_frequency;
}

void Esp32Twi::setTwiState( twiState_t state ) {
    TwiModule::setTwiState( state );
    if ( !m_busy )
        return;

    if ( state == TWI_START || state == TWI_REP_START ) {
        commandDone();
    } else if ( state == TWI_MTX_ADR_ACK || state == TWI_MTX_DATA_ACK || state == TWI_MRX_ADR_ACK ) {
        if ( m_remaining && --m_remaining == 0 )
            commandDone();
        else if ( m_remaining ) {
            writeNextByte();
            return;
        }
    } else if ( state == TWI_MTX_ADR_NACK || state == TWI_MTX_DATA_NACK || state == TWI_MRX_ADR_NACK ) {
        m_interruptRaw |= 1 << 10;
        if ( m_ackCheck ) {
            finishTransaction( 1 << 10 );
            return;
        }
        if ( m_remaining && --m_remaining == 0 )
            commandDone();
        else if ( m_remaining ) {
            writeNextByte();
            return;
        }
    } else if ( state == TWI_MRX_DATA_ACK || state == TWI_MRX_DATA_NACK ) {
        if ( m_rxFifo.size() < 32 )
            m_rxFifo.push_back( m_rxReg );
        else
            m_interruptRaw |= 1 << 2;
        if ( m_remaining && --m_remaining == 0 )
            commandDone();
        else if ( m_remaining ) {
            readNextByte();
            return;
        }
    } else if ( state == TWI_NO_STATE ) {
        commandDone();
        finishTransaction();
        return;
    } else {
        return;
    }
    runCommand();
}
