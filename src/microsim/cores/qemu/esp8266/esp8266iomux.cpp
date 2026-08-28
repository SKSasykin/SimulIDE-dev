#include "esp8266gpio.h"
#include "esp8266iomux.h"

Esp8266IoMux::Esp8266IoMux( QemuDevice* mcu, QString name, int n, uint32_t* clk,
                            uint64_t memStart, uint64_t memEnd, Esp8266Gpio* gpio )
    : QemuModule( mcu, name, n, clk, memStart, memEnd )
    , m_gpio( gpio )
{
    reset();
}

void Esp8266IoMux::reset() {
    for ( uint32_t& reg : m_regs )
        reg = 0;
    for ( int gpio = 0; gpio < 16; ++gpio ) {
        Esp32Pin* pin = m_gpio->getPad( gpio );
        if ( pin ) {
            pin->setInternalPullup( false );
            pin->setInternalPulldown( false );
        }
    }
}

void Esp8266IoMux::readRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    m_arena->regData = offset < 0x100 ? m_regs[offset / 4] : 0;
    m_arena->qemuAction = SIM_READ;
}

void Esp8266IoMux::writeRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    if ( offset >= 0x100 )
        return;
    m_regs[offset / 4] = m_eventValue;

    int gpio = gpioForOffset( offset );
    Esp32Pin* pin = m_gpio->getPad( gpio );
    if ( pin ) {
        pin->setInternalPullup( m_eventValue & (1u << 7) );
        pin->setInternalPulldown( m_eventValue & (1u << 6) );
    }
}

int Esp8266IoMux::gpioForOffset( uint64_t offset ) const {
    static constexpr int8_t gpioByRegister[] = {
        -1, 12, 13, 14, 15, 3, 1, 6, 7,
         8,  9, 10, 11,  0, 2, 4, 5,
    };
    uint64_t index = offset / 4;
    return index < sizeof( gpioByRegister ) ? gpioByRegister[index] : -1;
}
