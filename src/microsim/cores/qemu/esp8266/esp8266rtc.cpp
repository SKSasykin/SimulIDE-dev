#include "esp8266gpio.h"
#include "esp8266rtc.h"

Esp8266Rtc::Esp8266Rtc( QemuDevice* mcu, QString name, int n, uint32_t* clk,
                        uint64_t memStart, uint64_t memEnd, Esp8266Gpio* gpio )
    : QemuModule( mcu, name, n, clk, memStart, memEnd )
    , m_gpio( gpio )
{
    reset();
}

void Esp8266Rtc::reset() {
    for ( uint32_t& reg : m_regs )
        reg = 0;
    Esp32Pin* pin = m_gpio->getPad( 16 );
    if ( pin )
        pin->setInternalPulldown( false );
}

void Esp8266Rtc::readRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    m_arena->regData = offset < 0x100 ? m_regs[offset / 4] : 0;
    m_arena->qemuAction = SIM_READ;
}

void Esp8266Rtc::writeRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    if ( offset >= 0x100 )
        return;
    m_regs[offset / 4] = m_eventValue;

    if ( offset != 0xA0 )
        return;
    Esp32Pin* pin = m_gpio->getPad( 16 );
    if ( pin )
        pin->setInternalPulldown( m_eventValue & (1u << 3) );
}
