/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "esp32usart.h"
#include "iopin.h"
#include "qemudevice.h"
#include "simulator.h"
#include "usartrx.h"
#include "usarttx.h"

#define UART_FIFO_LENGTH 128
#define RXFIFO_FULL_INT ( 1u << 0 )
#define TXFIFO_EMPTY_INT ( 1u << 1 )
#define RXFIFO_OVF_INT ( 1u << 4 )
#define TX_DONE_INT ( 1u << 14 )

Esp32Usart::Esp32Usart( QemuDevice* mcu, QString name, int n, uint32_t* clk, uint64_t memStart, uint64_t memEnd,
                        Esp32UartVariant variant, int interrupt )
    : QemuUsart( mcu, name, n, clk, memStart, memEnd ), m_variant( variant ), m_interrupt( interrupt ) {
    //m_prescList = {2,4,8,16,32,64,128,256};
}
Esp32Usart::~Esp32Usart() { }

void Esp32Usart::reset() {
    if ( m_irqLevel && m_interrupt >= 0 )
        setInterrupt( m_interrupt, 0 );

    m_txFifo.clear();
    m_txPending.clear();
    m_rxFifo.clear();

    m_rxFullThrhd = 0;
    m_txEmptyThrhd = 0;
    m_intRaw = 0;
    m_intEn = 0;
    m_intSt = 0;
    m_irqLevel = false;
    m_txActive = false;
    m_divider = 0;
    m_baudRate = 115200;

    m_apbClock = 1; /// TODO: implement REF_TICK

    // Sender always enabled: qemu UART TX must transmit regardless of the pad/matrix
    // configuration, otherwise the TX FIFO never drains and the ROM busy-waits forever
    // on its first TXFIFO_CNT poll (C3/S3/8266 boot silently dead, no UART output).
    m_txOutput.setState( true );
    m_txOutput.setOutputEnable( true );
    m_sender->enable( true );
}

void Esp32Usart::connected( bool c ) {
    m_receiver->enable( c );
}

void Esp32Usart::driveTx( bool state ) {
    m_txOutput.setState( state );
}

bool Esp32Usart::sampleRx() {
    return m_rxInput.state();
}

void Esp32Usart::watchRx( eElement* listener, bool enabled ) {
    m_rxInput.watch( listener, enabled );
}

void Esp32Usart::writeRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    uint32_t data = m_eventValue;
    //qDebug() << "Esp32Usart::writeRegister" << offset<<data ;

    switch ( offset ) {
    case 0x00: { // UART_FIFO:
        // The FIFO count excludes the byte already loaded into the shift register.
        // The shared-memory bridge cannot stall an APB write. Keep writes that arrive
        // while the hardware FIFO is full pending until the serializer frees a slot.
        if ( m_txPending.size() || m_txFifo.size() >= UART_FIFO_LENGTH )
            m_txPending.enqueue( data & 0xFF );
        else
            m_txFifo.enqueue( data & 0xFF );
        if ( !m_txActive ) {
            // Re-arm the sender: UartTR::initialize() disables it (m_enabled=false) after
            // reset(), and the qemu usart has no matrix/connection path to enable it for
            // C3/S3/8266. The sender must transmit whenever the guest pushes a byte.
            m_sender->enable( true );
            m_txActive = true;
            UsartModule::sendByte( m_txFifo.dequeue() );
            if ( m_txPending.size() )
                m_txFifo.enqueue( m_txPending.dequeue() );
        }
    } break;
    case 0x04: // UART_INT_RAW: RO
    case 0x08: // UART_INT_ST: RO
        break;
    case 0x0C: // UART_INT_ENA
        m_intEn = data;
        break;
    case 0x10: // UART_INT_CLR
        m_intRaw &= ~data;
        break;

    case 0x14: { // UART_CLKDIV:
        uint32_t clkFra = (data & 0x00F00000) >> 20;
        uint32_t clkInt = (data & 0x000FFFFF) << 4;
        m_divider = clkInt + clkFra;
        int br = 115200;
        if ( m_divider ) {
            // UART clock = APB (80 MHz on ESP32/ESP8266, 40 MHz on S3/C3).
            // m_frequency points to the chip's current APB clock (updated by the bridge).
            uint64_t freq = m_apbClock ? ( m_frequency ? *m_frequency : 80000000 ) : 1000000;
            br = (freq << 4) / m_divider;
        }
        if ( m_baudRate != br )
            m_baudRate = br;
        if ( br > 0 )
            setPeriod( 1e12 / br );
        //qDebug() << "Esp32Usart::writeRegister baudRate" << m_baudRate<< "period" << 1e12/br;
        //freqChanged();
    } break;
    //case 0x18:                                        // UART_AUTOBAUD:
    //    //Autobaud is only used in the ROM bootloader, and it doesn't care if the result is ready immediately.
    //    writeMem( m_memStart+0x30, (data & 1) ? 0x3FF :  0 );

    //    ///if( FIELD_EX32(value, UART_AUTOBAUD, EN) ) s->reg[R_UART_RXD_CNT] = 0x3FF;
    //    ///else                                       s->reg[R_UART_RXD_CNT] = 0;
    //    break;
    //case 0x1C:                break;          // UART_STATUS: RO
    case 0x20:
        writeMem( m_eventAddress, data );
        writeCR0();
        break; // UART_CONF0:
    case 0x24:
        writeMem( m_eventAddress, data );
        writeCR1();
        break; // UART_CONF1:
    default:
        write();
        break;
    }
    updateIrq();
}

void Esp32Usart::readRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    uint32_t value = 0;

    switch ( offset ) {
    case 0x00: // UART_FIFO:
        if ( m_rxFifo.size() )
            value = m_rxFifo.dequeue();
        break;

        case 0x04: value = m_intRaw;  break; // UART_INT_RAW: RO
        case 0x08: value = m_intSt;   break; // UART_INT_ST:  RO
        case 0x0C: value = m_intEn;   break; // UART_INT_ENA
        case 0x10:                    break; // UART_INT_CLR: WO
        ////case 0x14:                    break; // UART_CLKDIV:
        ////case 0x18:                    break; // UART_AUTOBAUD:
        case 0x1C:                           // UART_STATUS: RO
        {
            value  = m_rxFifo.size() & 0xFF;
            value |= ( m_txFifo.size() << 16 ) & 0xFF0000;
            if ( m_txFifo.isEmpty() && !m_txActive ) value |= 1 << 9;  // TX_IDLE
        } break;
        ////case 0x20:                    break; // UART_CONF0:
        ////case 0x24:                    break; // UART_CONF1:
        //case 0x28:                           // UART_LOWPULSE
        //case 0x2C: value = 337;       break; // UART_HIGHPULSE  /* FIXME: this should depend on the APB frequency */
        //case 0x58:                           // UART_MEM_CONF
        //{
        //    value = 1<<3 | 1<<7;
        //    //r = FIELD_DP32(r, UART_MEM_CONF, RX_SIZE, (unsigned char)(UART_FIFO_LENGTH/128));
        //    //r = FIELD_DP32(r, UART_MEM_CONF, TX_SIZE,  (unsigned char)(UART_FIFO_LENGTH/128));
        //}break;
        //case 0x60:
        //{
        //    //uint32_t fifo_size = fifo8_num_used(&s->rx_fifo);
        //    /* The software only cares about the differene between WR_ADDR and RD_ADDR;
        //     * to keep things simpler, set RD_ADDR to 0 and WR_ADDR to the number of bytes
        //     * in the FIFO. 128 is a special case — write and read pointers should be
        //     * the same in this case.
        //     */
        //    //r = FIELD_DP32(0, UART_MEM_RX_STATUS, WR_ADDR, (fifo_size == 128) ? 0 : fifo_size);
        //    value = m_rxFifo.size() & 0x7F;
        //    value <<= 13;
        //}break;
        //case 0x78: value = 0x15122500; break; // UART_DATE:
    default:
        value = read();
        break;
    }
    updateIrq();
    m_arena->regData = value;
    m_arena->qemuAction = SIM_READ;
    //qDebug() << "\nEsp32Usart::readRegister"<<QString::number( offset )<<value ;
}

void Esp32Usart::writeCR0() {
    uint32_t data = m_eventValue;

    uint8_t parityOdd = ( data & 1 << 0 ) ? 1 : 0;
    uint8_t parityEn = ( data & 1 << 1 ) ? 1 : 0;

    if ( parityEn )
        m_parity = parityOdd ? parODD : parEVEN;
    else
        m_parity = parNONE;

    uint8_t dataBits = ( data & 0b001100 ) >> 2;
    setDataBits( 5 + dataBits );

    uint8_t stopBits = ( data & 0b110000 ) >> 4;
    switch ( stopBits ) {
    case 0:
        break;
    case 1:
        m_stopBits = 1;
        break;
    case 2:
        m_stopBits = 1;
        break;
    case 3:
        m_stopBits = 2;
        break;
    }
    //qDebug() << "writeCR0"<< data << 5 + dataBits << m_stopBits;
    uint8_t txFifoRst = data & 1 << 18;
    if ( txFifoRst ) {
        m_txFifo.clear();
        m_txPending.clear();
        m_intRaw &= ~( TXFIFO_EMPTY_INT | TX_DONE_INT );
    }

    uint8_t rxFifoRst = data & 1 << 17;
    if ( rxFifoRst ) {
        m_rxFifo.clear();
        m_intRaw &= ~( RXFIFO_FULL_INT | RXFIFO_OVF_INT );
    }

    //m_apbClock = (data & 1<<27) ? 1 : 0;
}

void Esp32Usart::writeCR1() {
    uint32_t data = m_eventValue;
    uint8_t txShift = 8;
    uint16_t thresholdMask = 0x7F;

    if ( m_variant == Esp32s3Uart ) {
        txShift = 10;
        thresholdMask = 0x3FF;
    } else if ( m_variant == Esp32c3Uart ) {
        txShift = 9;
        thresholdMask = 0x1FF;
    }
    m_rxFullThrhd = data & thresholdMask;
    m_txEmptyThrhd = ( data >> txShift ) & thresholdMask;
}

void Esp32Usart::frameSent( uint8_t data ) {
    QemuUsart::frameSent( data );

    m_txActive = false;
    if ( m_txFifo.size() ) {
        m_txActive = true;
        UsartModule::sendByte( m_txFifo.dequeue() );
        if ( m_txPending.size() )
            m_txFifo.enqueue( m_txPending.dequeue() );
    }
    updateIrq();
}

void Esp32Usart::byteReceived( uint8_t data ) {
    UsartModule::byteReceived( data );

    if ( m_rxFifo.size() < UART_FIFO_LENGTH )
        m_rxFifo.enqueue( data );
    else
        m_intRaw |= RXFIFO_OVF_INT;
    updateIrq();
}

void Esp32Usart::updateIrq() {
    m_intRaw &= ~( RXFIFO_FULL_INT | TXFIFO_EMPTY_INT | TX_DONE_INT );
    if ( m_rxFifo.size() >= m_rxFullThrhd )
        m_intRaw |= RXFIFO_FULL_INT;
    if ( m_txFifo.size() <= m_txEmptyThrhd )
        m_intRaw |= TXFIFO_EMPTY_INT;
    if ( m_txFifo.isEmpty() && m_txPending.isEmpty() && !m_txActive )
        m_intRaw |= TX_DONE_INT;

    m_intSt = m_intRaw & m_intEn;
    bool irqLevel = m_intSt != 0;
    if ( m_interrupt >= 0 && m_irqLevel != irqLevel )
        setInterrupt( m_interrupt, irqLevel );
    m_irqLevel = irqLevel;
}

void Esp32Usart::freqChanged() {
    //if( !m_divider ) return;

    //uint64_t freq = m_apbClock ? *m_frequency : 1000000; // m_frequency = APB Clock
    //int baudRate = (freq << 4) / m_divider;

    //if( m_baudRate == baudRate ) return;
    //setBaudRate( baudRate );
    ////qDebug() << "Esp32Usart::freqChanged baudRate" << baudRate<<m_apbClock<<*m_frequency;
}
