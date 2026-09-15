/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "qemubt.h"
#include <atomic>
#include <cstring>
#include <cstdint>

namespace {

constexpr uint32_t RING_FRAMES = 64;
constexpr uint32_t FRAME_MAX = 1536;

constexpr uint8_t HCI_SUCCESS = 0x00;
constexpr uint8_t HCI_UNKNOWN_HCI_COMMAND = 0x01;
constexpr uint8_t HCI_INVALID_HCI_COMMAND_PARAMETERS = 0x12;

constexpr uint8_t H4_CMD = 0x01;
constexpr uint8_t H4_EVT = 0x04;

constexpr uint16_t HCI_RESET = 0x0C03;
constexpr uint16_t HCI_READ_LOCAL_VERSION_INFO = 0x1001;
constexpr uint16_t HCI_READ_LOCAL_SUPPORTED_FEATURES = 0x1003;
constexpr uint16_t HCI_SET_EVENT_MASK = 0x0C01;
constexpr uint16_t HCI_SET_EVENT_MASK_PAGE_2 = 0x0C63;
constexpr uint16_t HCI_LE_SET_EVENT_MASK = 0x2001;
constexpr uint16_t HCI_LE_READ_BUFFER_SIZE = 0x2002;
constexpr uint16_t HCI_LE_READ_LOCAL_SUPPORTED_FEATURES = 0x2003;
constexpr uint16_t HCI_READ_BD_ADDR = 0x1009;
constexpr uint16_t HCI_SET_CONTROLLER_TO_HOST_FLOW_CONTROL = 0x0C31;
constexpr uint16_t HCI_HOST_BUFFER_SIZE = 0x0C33;
constexpr uint16_t HCI_LE_SET_ADDRESS_RESOLUTION_ENABLE = 0x202D;
constexpr uint16_t HCI_LE_CLEAR_RESOLVING_LIST = 0x2029;
constexpr uint16_t HCI_LE_ADD_DEVICE_TO_RESOLVING_LIST = 0x2027;
constexpr uint16_t HCI_LE_SET_PRIVACY_MODE = 0x204E;

constexpr uint8_t EVT_CMD_COMPLETE = 0x0E;
constexpr uint8_t EVT_CMD_STATUS = 0x0F;

struct QemuBt::ControllerState {
    uint64_t eventMask = 0;
    uint64_t leEventMask = 0;
    bool controllerToHostFlow = false;
    uint16_t hostAclLength = 0;
    uint16_t hostAclPackets = 0;
    std::array<uint8_t, 6> bdAddr{};
};

bool readLe64(const uint8_t* src, uint64_t& dst) {
    dst = 0;
    for (int i = 7; i >= 0; --i) {
        dst = (dst << 8) | src[i];
    }
    return true;
}

bool writeLe64(uint64_t src, uint8_t* dst) {
    for (int i = 0; i < 8; ++i) {
        dst[i] = src & 0xFF;
        src >>= 8;
    }
    return true;
}

bool readLe16(const uint8_t* src, uint16_t& dst) {
    dst = uint16_t(src[0]) | (uint16_t(src[1]) << 8);
    return true;
}

bool writeLe16(uint16_t src, uint8_t* dst) {
    dst[0] = src & 0xFF;
    dst[1] = (src >> 8) & 0xFF;
    return true;
}

} // namespace

QemuBt::QemuBt( QemuDevice* mcu, QString name, int n,
                uint64_t memStart, uint64_t memEnd )
    : QemuModule( mcu, name, n, nullptr, memStart, memEnd )
    , m_rxCount( 0 )
    , m_txCount( 0 )
{
    m_type = "bt";
    m_arena->bt_rx.head = m_arena->bt_rx.tail = 0;
    m_arena->bt_tx.head = m_arena->bt_tx.tail = 0;

    m_state.bdAddr[0] = 0x11;
    m_state.bdAddr[1] = 0x22;
    m_state.bdAddr[2] = 0x33;
    m_state.bdAddr[3] = 0x44;
    m_state.bdAddr[4] = 0x55;
    m_state.bdAddr[5] = 0x66;
}

void QemuBt::reset() {
    QemuModule::reset();
    m_arena->bt_rx.head = m_arena->bt_rx.tail = 0;
    m_arena->bt_tx.head = m_arena->bt_tx.tail = 0;
    m_rxCount = m_txCount = 0;

    m_state.eventMask = 0;
    m_state.leEventMask = 0;
    m_state.controllerToHostFlow = false;
    m_state.hostAclLength = 0;
    m_state.hostAclPackets = 0;
}

bool QemuBt::buildCommandComplete(uint16_t opcode, uint8_t status, const uint8_t* data, uint8_t dataLen, uint8_t* out, uint32_t& outLen) {
    if (!out) return false;

    const uint8_t paramLen = 4 + dataLen;
    if (paramLen > FRAME_MAX - 3) return false;

    out[0] = H4_EVT;
    out[1] = EVT_CMD_COMPLETE;
    out[2] = paramLen;
    out[3] = 0x01;
    out[4] = opcode & 0xFF;
    out[5] = (opcode >> 8) & 0xFF;
    out[6] = status;

    if (status == HCI_SUCCESS && data && dataLen > 0) {
        std::memcpy(out + 7, data, dataLen);
    }

    outLen = 3 + paramLen;
    return true;
}

bool QemuBt::dispatchCommand(const uint8_t* cmd, uint32_t len, uint8_t* response, uint32_t& responseLen) {
    if (!cmd || len < 4 || cmd[0] != H4_CMD) {
        return false;
    }

    const uint8_t paramLen = cmd[3];
    if (len != 4 + paramLen) {
        return false;
    }

    const uint16_t opcode = uint16_t(cmd[1]) | (uint16_t(cmd[2]) << 8);

    const CommandSpec* spec = nullptr;
    for (size_t i = 0; i < s_commandCount; ++i) {
        if (s_commands[i].opcode == opcode) {
            spec = &s_commands[i];
            break;
        }
    }

    if (!spec) {
        return buildCommandComplete(opcode, HCI_UNKNOWN_HCI_COMMAND, nullptr, 0, response, responseLen);
    }

    if (paramLen != spec->parameterLength) {
        return buildCommandComplete(opcode, HCI_INVALID_HCI_COMMAND_PARAMETERS, nullptr, 0, response, responseLen);
    }

    uint8_t responseData[256];
    bool success = (this->*(spec->handler))(cmd + 4, responseData);

    if (!success) {
        return buildCommandComplete(opcode, HCI_INVALID_HCI_COMMAND_PARAMETERS, nullptr, 0, response, responseLen);
    }

    return buildCommandComplete(opcode, HCI_SUCCESS, responseData, spec->successDataLength, response, responseLen);
}

bool QemuBt::handleReset(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    (void)responseData;
    return true;
}

bool QemuBt::handleReadLocalVersionInfo(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return false;

    responseData[0] = 0x09;
    responseData[1] = 0x00;
    responseData[2] = 0x00;
    responseData[3] = 0x09;
    responseData[4] = 0x00;
    responseData[5] = 0x00;
    responseData[6] = 0x00;
    responseData[7] = 0x00;
    return true;
}

bool QemuBt::handleReadLocalSupportedFeatures(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return false;

    const uint64_t features = 0x0000000060000000ULL;
    writeLe64(features, responseData);
    return true;
}

bool QemuBt::handleSetEventMask(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params) return false;

    uint64_t mask = 0;
    readLe64(params, mask);
    m_state.eventMask = mask;
    return true;
}

bool QemuBt::handleSetEventMaskPage2(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    (void)params;
    return true;
}

bool QemuBt::handleLeSetEventMask(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params) return false;

    uint64_t mask = 0;
    readLe64(params, mask);
    m_state.leEventMask = mask;
    return true;
}

bool QemuBt::handleLeReadBufferSize(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return false;

    writeLe16(27, responseData);
    responseData[2] = 1;
    return true;
}

bool QemuBt::handleLeReadLocalSupportedFeatures(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return false;

    std::memset(responseData, 0, 8);
    return true;
}

bool QemuBt::handleReadBdAddr(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return false;

    for (int i = 0; i < 6; ++i) {
        responseData[i] = m_state.bdAddr[i];
    }
    return true;
}

bool QemuBt::handleSetControllerToHostFlowControl(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params) return false;

    const uint8_t mode = params[0];
    if (mode == 0 || mode == 1) {
        m_state.controllerToHostFlow = (mode == 1);
        return true;
    }
    return false;
}

bool QemuBt::handleHostBufferSize(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params) return false;

    uint16_t aclLen = 0;
    readLe16(params, aclLen);
    const uint8_t scoLen = params[2];
    uint16_t aclCount = 0;
    readLe16(params + 3, aclCount);
    uint16_t scoCount = 0;
    readLe16(params + 5, scoCount);

    if (scoLen != 0 || scoCount != 0 || aclLen == 0 || aclCount == 0) {
        return false;
    }

    m_state.hostAclLength = aclLen;
    m_state.hostAclPackets = aclCount;
    return true;
}

bool QemuBt::handleLeSetAddressResolutionEnable(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    (void)params;
    return true;
}

bool QemuBt::handleLeClearResolvingList(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    (void)params;
    return true;
}

bool QemuBt::handleLeAddDeviceToResolvingList(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    (void)params;
    return true;
}

bool QemuBt::handleLeSetPrivacyMode(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    (void)params;
    return true;
}

const QemuBt::CommandSpec QemuBt::s_commands[] = {
    { HCI_RESET, 0, 0, &QemuBt::handleReset },
    { HCI_READ_LOCAL_VERSION_INFO, 0, 8, &QemuBt::handleReadLocalVersionInfo },
    { HCI_READ_LOCAL_SUPPORTED_FEATURES, 0, 8, &QemuBt::handleReadLocalSupportedFeatures },
    { HCI_SET_EVENT_MASK, 8, 0, &QemuBt::handleSetEventMask },
    { HCI_SET_EVENT_MASK_PAGE_2, 8, 0, &QemuBt::handleSetEventMaskPage2 },
    { HCI_LE_SET_EVENT_MASK, 8, 0, &QemuBt::handleLeSetEventMask },
    { HCI_LE_READ_BUFFER_SIZE, 0, 3, &QemuBt::handleLeReadBufferSize },
    { HCI_LE_READ_LOCAL_SUPPORTED_FEATURES, 0, 8, &QemuBt::handleLeReadLocalSupportedFeatures },
    { HCI_READ_BD_ADDR, 0, 6, &QemuBt::handleReadBdAddr },
    { HCI_SET_CONTROLLER_TO_HOST_FLOW_CONTROL, 1, 0, &QemuBt::handleSetControllerToHostFlowControl },
    { HCI_HOST_BUFFER_SIZE, 7, 0, &QemuBt::handleHostBufferSize },
    { HCI_LE_SET_ADDRESS_RESOLUTION_ENABLE, 1, 0, &QemuBt::handleLeSetAddressResolutionEnable },
    { HCI_LE_CLEAR_RESOLVING_LIST, 0, 0, &QemuBt::handleLeClearResolvingList },
    { HCI_LE_ADD_DEVICE_TO_RESOLVING_LIST, 39, 0, &QemuBt::handleLeAddDeviceToResolvingList },
    { HCI_LE_SET_PRIVACY_MODE, 8, 0, &QemuBt::handleLeSetPrivacyMode },
};

bool QemuBt::pushRxFrame(const uint8_t* data, uint32_t len) {
    if (!data || !len || len > FRAME_MAX) return false;

    volatile qemuWifiRing_t* ring = &m_arena->bt_rx;
    uint32_t tail = ring->tail;
    uint32_t head = ring->head;
    std::atomic_thread_fence(std::memory_order_acquire);
    if (tail >= RING_FRAMES || head >= RING_FRAMES) return false;

    uint32_t next = (tail + 1) % RING_FRAMES;
    if (next == head) return false;

    qemuWifiFrame_t* output = const_cast<qemuWifiFrame_t*>(&ring->frames[tail]);
    output->len = len;
    std::memcpy(output->data, data, len);
    ring->seq++;
    std::atomic_thread_fence(std::memory_order_release);
    ring->tail = next;
    return true;
}

void QemuBt::pumpTx() {
    volatile qemuWifiRing_t* ring = &m_arena->bt_tx;
    uint8_t frame[FRAME_MAX];
    uint8_t response[FRAME_MAX];

    while (true) {
        uint32_t head = ring->head;
        uint32_t tail = ring->tail;
        if (head >= RING_FRAMES || tail >= RING_FRAMES || head == tail) return;

        std::atomic_thread_fence(std::memory_order_acquire);
        const qemuWifiFrame_t* input = const_cast<const qemuWifiFrame_t*>(&ring->frames[head]);
        uint32_t len = input->len;
        bool bounded = len <= FRAME_MAX;
        if (bounded) std::memcpy(frame, input->data, len);

        uint32_t responseLen = 0;
        if (bounded) {
            dispatchCommand(frame, len, response, responseLen);
        }

        if (responseLen && !pushRxFrame(response, responseLen)) return;
        if (responseLen) m_rxCount++;

        std::atomic_thread_fence(std::memory_order_release);
        ring->head = (head + 1) % RING_FRAMES;
        m_txCount++;
    }
}

void QemuBt::runAction() {
    pumpTx();
}

void QemuBt::injectHostFrame(const QByteArray& frame) {
    if (frame.size() <= 0 || frame.size() > FRAME_MAX) return;
    const uint8_t* data = reinterpret_cast<const uint8_t*>(frame.constData());
    uint32_t len = static_cast<uint32_t>(frame.size());
    if (completeControllerFrame(data, len) && pushRxFrame(data, len))
        m_rxCount++;
}

namespace {
bool completeControllerFrame(const uint8_t* frame, uint32_t len) {
    if (!frame || !len || len > FRAME_MAX) return false;

    uint32_t packetLen = 0;
    switch (frame[0]) {
    case 0x02:
        if (len < 5) return false;
        packetLen = 5u + frame[3] + (uint32_t(frame[4]) << 8);
        break;
    case 0x03:
        if (len < 4) return false;
        packetLen = 4u + frame[3];
        break;
    case 0x04:
        if (len < 3) return false;
        packetLen = 3u + frame[2];
        break;
    case 0x05:
        if (len < 5) return false;
        packetLen = 5u + ((frame[3] | (uint32_t(frame[4]) << 8)) & 0x3FFF);
        break;
    default:
        return false;
    }
    return len == packetLen;
}
}