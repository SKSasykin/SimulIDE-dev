#include "esp32gpio.h"
#include "esp32rtcio.h"

namespace {
struct RtcPullPad {
    uint8_t gpio;
    uint8_t reg;
    uint32_t pullupMask;
    uint32_t pulldownMask;
};

constexpr RtcPullPad rtcPullPads[] = {
    { 25, 0x84, 1u << 27, 1u << 28 },
    { 26, 0x88, 1u << 27, 1u << 28 },
    { 33, 0x8C, 1u << 27, 1u << 28 },
    { 32, 0x8C, 1u << 22, 1u << 23 },
    {  4, 0x94, 1u << 27, 1u << 28 },
    {  0, 0x98, 1u << 27, 1u << 28 },
    {  2, 0x9C, 1u << 27, 1u << 28 },
    { 15, 0xA0, 1u << 27, 1u << 28 },
    { 13, 0xA4, 1u << 27, 1u << 28 },
    { 12, 0xA8, 1u << 27, 1u << 28 },
    { 14, 0xAC, 1u << 27, 1u << 28 },
    { 27, 0xB0, 1u << 27, 1u << 28 },
};
}

Esp32RtcIo::Esp32RtcIo( QemuDevice* mcu, QString name, int n, uint32_t* clk,
                        uint64_t memStart, uint64_t memEnd, Esp32Gpio* gpio )
    : QemuModule( mcu, name, n, clk, memStart, memEnd )
    , m_gpio( gpio )
{
    reset();
}

void Esp32RtcIo::reset() {
    for ( uint32_t& reg : m_regs )
        reg = 0;
    for ( const RtcPullPad& desc : rtcPullPads ) {
        Esp32Pin* pin = m_gpio->getPad( desc.gpio );
        if ( pin ) {
            pin->setRtcPullup( false );
            pin->setRtcPulldown( false );
        }
    }
}

void Esp32RtcIo::readRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    m_arena->regData = offset < 0x400 ? m_regs[offset / 4] : 0;
    m_arena->qemuAction = SIM_READ;
}

void Esp32RtcIo::writeRegister() {
    uint64_t offset = m_eventAddress - m_memStart;
    if ( offset >= 0x400 )
        return;
    m_regs[offset / 4] = m_eventValue;

    for ( const RtcPullPad& desc : rtcPullPads ) {
        if ( desc.reg != offset )
            continue;
        Esp32Pin* pin = m_gpio->getPad( desc.gpio );
        if ( pin ) {
            pin->setRtcPullup( m_eventValue & desc.pullupMask );
            pin->setRtcPulldown( m_eventValue & desc.pulldownMask );
        }
    }
}
