/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "qemubt.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr uint32_t RING_FRAMES = 64;
constexpr uint32_t FRAME_MAX = 1536;
constexpr size_t PENDING_EVENT_MAX = 64;
constexpr size_t BEST_EFFORT_EVENT_MAX = 48;
constexpr size_t RELIABLE_DATA_EVENT_MAX = 60;
constexpr size_t DUPLICATE_CACHE_MAX = 128;

constexpr uint8_t HCI_SUCCESS = 0x00;
constexpr uint8_t HCI_UNKNOWN_HCI_COMMAND = 0x01;
constexpr uint8_t HCI_UNKNOWN_CONNECTION_IDENTIFIER = 0x02;
constexpr uint8_t HCI_COMMAND_DISALLOWED = 0x0C;
constexpr uint8_t HCI_INVALID_HCI_COMMAND_PARAMETERS = 0x12;

constexpr uint8_t H4_CMD = 0x01;
constexpr uint8_t H4_ACL = 0x02;
constexpr uint8_t H4_EVT = 0x04;

constexpr uint16_t HCI_RESET = 0x0C03;
constexpr uint16_t HCI_READ_LOCAL_VERSION_INFO = 0x1001;
constexpr uint16_t HCI_READ_LOCAL_SUPPORTED_COMMANDS = 0x1002;
constexpr uint16_t HCI_READ_LOCAL_SUPPORTED_FEATURES = 0x1003;
constexpr uint16_t HCI_SET_EVENT_MASK = 0x0C01;
constexpr uint16_t HCI_SET_EVENT_MASK_PAGE_2 = 0x0C63;
constexpr uint16_t HCI_LE_SET_EVENT_MASK = 0x2001;
constexpr uint16_t HCI_LE_READ_BUFFER_SIZE = 0x2002;
constexpr uint16_t HCI_LE_READ_LOCAL_SUPPORTED_FEATURES = 0x2003;
constexpr uint16_t HCI_READ_BD_ADDR = 0x1009;
constexpr uint16_t HCI_SET_CONTROLLER_TO_HOST_FLOW_CONTROL = 0x0C31;
constexpr uint16_t HCI_HOST_BUFFER_SIZE = 0x0C33;
constexpr uint16_t HCI_HOST_NUMBER_OF_COMPLETED_PACKETS = 0x0C35;
constexpr uint16_t HCI_DISCONNECT = 0x0406;
constexpr uint16_t HCI_READ_REMOTE_VERSION_INFO = 0x041D;
constexpr uint16_t HCI_LE_SET_ADDRESS_RESOLUTION_ENABLE = 0x202D;
constexpr uint16_t HCI_LE_CLEAR_RESOLVING_LIST = 0x2029;
constexpr uint16_t HCI_LE_ADD_DEVICE_TO_RESOLVING_LIST = 0x2027;
constexpr uint16_t HCI_LE_SET_PRIVACY_MODE = 0x204E;
constexpr uint16_t HCI_LE_SET_ADVERTISING_PARAMETERS = 0x2006;
constexpr uint16_t HCI_LE_SET_ADVERTISING_DATA = 0x2008;
constexpr uint16_t HCI_LE_SET_SCAN_RESPONSE_DATA = 0x2009;
constexpr uint16_t HCI_LE_SET_ADVERTISING_ENABLE = 0x200A;
constexpr uint16_t HCI_LE_SET_SCAN_PARAMETERS = 0x200B;
constexpr uint16_t HCI_LE_SET_SCAN_ENABLE = 0x200C;
constexpr uint16_t HCI_LE_CREATE_CONNECTION = 0x200D;
constexpr uint16_t HCI_LE_CREATE_CONNECTION_CANCEL = 0x200E;
constexpr uint16_t HCI_LE_READ_REMOTE_FEATURES = 0x2016;
constexpr uint16_t HCI_LE_RAND = 0x2018;

constexpr uint8_t EVT_CMD_COMPLETE = 0x0E;
constexpr uint8_t EVT_CMD_STATUS = 0x0F;
constexpr uint8_t EVT_DISCONNECTION_COMPLETE = 0x05;
constexpr uint8_t EVT_READ_REMOTE_VERSION_COMPLETE = 0x0C;
constexpr uint8_t EVT_NUMBER_OF_COMPLETED_PACKETS = 0x13;
constexpr uint8_t EVT_LE_META = 0x3E;
constexpr uint8_t EVT_LE_CONNECTION_COMPLETE = 0x01;
constexpr uint8_t EVT_LE_ADVERTISING_REPORT = 0x02;
constexpr uint8_t EVT_LE_READ_REMOTE_FEATURES_COMPLETE = 0x04;
constexpr uint8_t EVT_LE_ENHANCED_CONNECTION_COMPLETE = 0x0A;

constexpr uint64_t EVENT_MASK_LE_META = uint64_t(1) << 61;
constexpr uint64_t EVENT_MASK_DISCONNECTION_COMPLETE = uint64_t(1) << 4;
constexpr uint64_t EVENT_MASK_READ_REMOTE_VERSION_COMPLETE = uint64_t(1) << 11;
constexpr uint64_t LE_EVENT_MASK_ADVERTISING_REPORT = uint64_t(1) << 1;
constexpr uint64_t LE_EVENT_MASK_CONNECTION_COMPLETE = uint64_t(1) << 0;
constexpr uint64_t LE_EVENT_MASK_READ_REMOTE_FEATURES_COMPLETE = uint64_t(1) << 3;
constexpr uint64_t LE_EVENT_MASK_ENHANCED_CONNECTION_COMPLETE = uint64_t(1) << 9;

struct AirAdvertisement {
    QemuBt* owner = nullptr;
    QString source;
    std::array<uint8_t, 6> address{};
    uint8_t addressType = 0;
    uint8_t eventType = 0;
    uint8_t dataLength = 0;
    std::array<uint8_t, 31> data{};
    uint8_t scanResponseLength = 0;
    std::array<uint8_t, 31> scanResponse{};
};

std::vector<AirAdvertisement> airAdvertisements;
std::vector<QemuBt*> airScanners;
std::vector<QemuBt*> airControllers;
uint16_t nextConnectionHandle = 1;

bool completeControllerFrame(const uint8_t* frame, uint32_t len);

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
    initializeController();
}

QemuBt::QemuBt( volatile qemuArena_t& arena, QString name, int n )
    : QemuModule( arena, name, n )
    , m_rxCount( 0 )
    , m_txCount( 0 )
{
    initializeController();
}

void QemuBt::initializeController() {
    m_type = "bt";
    m_arena->bt_rx.head = m_arena->bt_rx.tail = 0;
    m_arena->bt_tx.head = m_arena->bt_tx.tail = 0;

    const QByteArray identity = m_name.toUtf8();
    uint64_t hash = 1469598103934665603ULL;
    for (char byte : identity) {
        hash ^= static_cast<uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    for (int i = 0; i < 5; ++i)
        m_state.bdAddr[i] = (hash >> (i * 8)) & 0xFF;
    m_state.bdAddr[5] = 0x02; // Locally administered unicast address.
    airControllers.push_back(this);
}

QemuBt::~QemuBt() {
    detachFromMedium(true);
    airControllers.erase(std::remove(airControllers.begin(), airControllers.end(), this),
                         airControllers.end());
}

void QemuBt::reset() {
    QemuModule::reset();
    detachFromMedium(true);
    clearProcedureState();
    m_arena->bt_rx.head = m_arena->bt_rx.tail = 0;
    m_arena->bt_tx.head = m_arena->bt_tx.tail = 0;
    m_rxCount = m_txCount = 0;
}

void QemuBt::ensureMediumRegistration() {
    if (std::find(airControllers.begin(), airControllers.end(), this) ==
        airControllers.end())
        airControllers.push_back(this);
}

void QemuBt::clearProcedureState() {
    withdrawAdvertisement();
    unregisterScanner();
    m_state.eventMask = 0;
    m_state.leEventMask = 0;
    m_state.controllerToHostFlow = false;
    m_state.hostAclLength = 0;
    m_state.hostAclPackets = 0;
    m_state.hostAclCredits = 0;
    m_advState = AdvertisingState();
    m_stagedAdvState = AdvertisingState();
    m_scanState = ScanningState();
    m_stagedScanState = ScanningState();
    m_initiation = InitiationState();
    m_stagedInitiation = InitiationState();
    m_connection = ConnectionState();
    m_pendingCommandResponse.clear();
    m_pendingAcl.clear();
    m_pendingEvents.clear();
    m_seenAdvertisements.clear();
    m_pendingAction = NoAction;
    m_pendingHandle = 0;
    m_pendingReason = 0;
    m_hostAclOutstanding.clear();
}

bool QemuBt::buildCommandComplete(uint16_t opcode, uint8_t status, const uint8_t* data, uint8_t dataLen, uint8_t* out, uint32_t& outLen) {
    if (!out) return false;

    const uint16_t paramLen = 4u + dataLen;
    if (paramLen > 255) return false;

    out[0] = H4_EVT;
    out[1] = EVT_CMD_COMPLETE;
    out[2] = static_cast<uint8_t>(paramLen);
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

bool QemuBt::buildCommandStatus(uint16_t opcode, uint8_t status, uint8_t* out,
                                uint32_t& outLen) {
    if (!out) return false;
    out[0] = H4_EVT;
    out[1] = EVT_CMD_STATUS;
    out[2] = 0x04;
    out[3] = status;
    out[4] = 0x01;
    writeLe16(opcode, out + 5);
    outLen = 7;
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
    const uint8_t* params = cmd + 4;

    if (opcode == HCI_HOST_NUMBER_OF_COMPLETED_PACKETS) {
        handleHostCompletedPackets(params, paramLen);
        responseLen = 0;
        return true;
    }

    if (opcode == HCI_LE_CREATE_CONNECTION) {
        uint8_t status = HCI_INVALID_HCI_COMMAND_PARAMETERS;
        if (paramLen == 25) {
            uint16_t scanInterval = 0, scanWindow = 0;
            uint16_t intervalMin = 0, intervalMax = 0, latency = 0, timeout = 0;
            uint16_t ceMin = 0, ceMax = 0;
            readLe16(params, scanInterval);
            readLe16(params + 2, scanWindow);
            readLe16(params + 13, intervalMin);
            readLe16(params + 15, intervalMax);
            readLe16(params + 17, latency);
            readLe16(params + 19, timeout);
            readLe16(params + 21, ceMin);
            readLe16(params + 23, ceMax);
            const bool valid = scanInterval >= 0x0004 && scanInterval <= 0x4000 &&
                scanWindow >= 0x0004 && scanWindow <= scanInterval &&
                params[4] == 0 && params[5] == 0 && params[12] == 0 &&
                intervalMin >= 0x0006 && intervalMin <= intervalMax &&
                intervalMax <= 0x0C80 && latency <= 0x01F3 &&
                timeout >= 0x000A && timeout <= 0x0C80 &&
                uint32_t(timeout) * 4u > uint32_t(1u + latency) * intervalMax &&
                ceMin <= ceMax;
            if (valid) {
                if (m_connection.peer || m_initiation.active) {
                    status = HCI_COMMAND_DISALLOWED;
                } else {
                    status = HCI_SUCCESS;
                    m_stagedInitiation = InitiationState();
                    m_stagedInitiation.active = true;
                    std::copy(params + 6, params + 12,
                              m_stagedInitiation.peerAddress.begin());
                    m_stagedInitiation.intervalMin = intervalMin;
                    m_stagedInitiation.intervalMax = intervalMax;
                    m_stagedInitiation.latency = latency;
                    m_stagedInitiation.timeout = timeout;
                    m_pendingAction = StartInitiation;
                }
            }
        }
        return buildCommandStatus(opcode, status, response, responseLen);
    }

    if (opcode == HCI_LE_CREATE_CONNECTION_CANCEL) {
        uint8_t status = HCI_INVALID_HCI_COMMAND_PARAMETERS;
        if (paramLen == 0) {
            status = m_initiation.active ? HCI_SUCCESS : HCI_COMMAND_DISALLOWED;
            if (status == HCI_SUCCESS) m_pendingAction = CancelInitiation;
        }
        return buildCommandComplete(opcode, status, nullptr, 0, response, responseLen);
    }

    if (opcode == HCI_DISCONNECT) {
        uint8_t status = HCI_INVALID_HCI_COMMAND_PARAMETERS;
        if (paramLen == 3) {
            uint16_t handle = 0;
            readLe16(params, handle);
            const uint8_t reason = params[2];
            const bool validReason = reason == 0x05 || reason == 0x13 ||
                reason == 0x14 || reason == 0x15 || reason == 0x1A ||
                reason == 0x29 || reason == 0x3B;
            if (validReason) {
                status = (!m_connection.peer || handle != m_connection.localHandle)
                    ? HCI_UNKNOWN_CONNECTION_IDENTIFIER : HCI_SUCCESS;
                if (status == HCI_SUCCESS) {
                    m_pendingHandle = handle;
                    m_pendingReason = reason;
                    m_pendingAction = DisconnectConnection;
                }
            }
        }
        return buildCommandStatus(opcode, status, response, responseLen);
    }

    if (opcode == HCI_LE_READ_REMOTE_FEATURES) {
        uint8_t status = HCI_INVALID_HCI_COMMAND_PARAMETERS;
        if (paramLen == 2) {
            uint16_t handle = 0;
            readLe16(params, handle);
            status = (!m_connection.peer || handle != m_connection.localHandle)
                ? HCI_UNKNOWN_CONNECTION_IDENTIFIER : HCI_SUCCESS;
            if (status == HCI_SUCCESS) {
                m_pendingHandle = handle;
                m_pendingAction = ReadRemoteFeatures;
            }
        }
        return buildCommandStatus(opcode, status, response, responseLen);
    }

    if (opcode == HCI_READ_REMOTE_VERSION_INFO) {
        uint8_t status = HCI_INVALID_HCI_COMMAND_PARAMETERS;
        if (paramLen == 2) {
            uint16_t handle = 0;
            readLe16(params, handle);
            status = (!m_connection.peer || handle != m_connection.localHandle)
                ? HCI_UNKNOWN_CONNECTION_IDENTIFIER : HCI_SUCCESS;
            if (status == HCI_SUCCESS) {
                m_pendingHandle = handle;
                m_pendingAction = ReadRemoteVersion;
            }
        }
        return buildCommandStatus(opcode, status, response, responseLen);
    }

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
    const ControllerState oldControllerState = m_state;
    const AdvertisingState oldAdvState = m_advState;
    const ScanningState oldScanState = m_scanState;
    const uint8_t status = (this->*(spec->handler))(cmd + 4, responseData);
    if (status == HCI_SUCCESS) {
        if (opcode == HCI_RESET) {
            m_pendingAction = ResetController;
        } else {
            m_stagedControllerState = m_state;
            m_stagedAdvState = m_advState;
            m_stagedScanState = m_scanState;
            m_pendingAction = ApplySynchronousState;
        }
    }
    m_state = oldControllerState;
    m_advState = oldAdvState;
    m_scanState = oldScanState;
    const uint8_t dataLength = status == HCI_SUCCESS ? spec->successDataLength : 0;
    return buildCommandComplete(opcode, status, responseData, dataLength,
                                response, responseLen);
}

uint8_t QemuBt::handleReset(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    (void)responseData;
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleReadLocalVersionInfo(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    responseData[0] = 0x09;
    responseData[1] = 0x00;
    responseData[2] = 0x00;
    responseData[3] = 0x09;
    responseData[4] = 0x00;
    responseData[5] = 0x00;
    responseData[6] = 0x00;
    responseData[7] = 0x00;
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleReadLocalSupportedCommands(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    std::memset(responseData, 0, 64);
    responseData[0] = 0x20;
    responseData[16] = 0x05;
    responseData[22] = 0x15;
    responseData[24] = 0x07;
    responseData[25] = 0x01;
    responseData[27] = 0x02;
    responseData[28] = 0x04;
    responseData[56] = 0xA7;
    responseData[57] = 0x3F;
    responseData[58] = 0x20;
    responseData[60] = 0x40;
    responseData[61] = 0x11;
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleReadLocalSupportedFeatures(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    const uint64_t features = 0x0000006000000000ULL;
    writeLe64(features, responseData);
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleSetEventMask(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params) return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    uint64_t mask = 0;
    readLe64(params, mask);
    m_state.eventMask = mask;
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleSetEventMaskPage2(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    (void)params;
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleLeSetEventMask(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params) return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    uint64_t mask = 0;
    readLe64(params, mask);
    m_state.leEventMask = mask;
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleLeReadBufferSize(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    writeLe16(27, responseData);
    responseData[2] = 1;
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleLeReadLocalSupportedFeatures(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    std::memset(responseData, 0, 8);
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleLeRand(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    std::copy(m_state.bdAddr.begin(), m_state.bdAddr.end(), responseData);
    responseData[6] = 0x5A;
    responseData[7] = 0xA5;
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleReadBdAddr(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    for (int i = 0; i < 6; ++i) {
        responseData[i] = m_state.bdAddr[i];
    }
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleSetControllerToHostFlowControl(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params) return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    const uint8_t mode = params[0];
    if (mode == 0 || mode == 1) {
        m_state.controllerToHostFlow = (mode == 1);
        return HCI_SUCCESS;
    }
    return HCI_INVALID_HCI_COMMAND_PARAMETERS;
}

uint8_t QemuBt::handleHostBufferSize(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params) return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    uint16_t aclLen = 0;
    readLe16(params, aclLen);
    const uint8_t scoLen = params[2];
    uint16_t aclCount = 0;
    readLe16(params + 3, aclCount);
    uint16_t scoCount = 0;
    readLe16(params + 5, scoCount);

    if (scoLen != 0 || scoCount != 0 || aclLen < 27 || aclCount == 0) {
        return HCI_INVALID_HCI_COMMAND_PARAMETERS;
    }
    if (!m_hostAclOutstanding.empty() || m_connection.peer)
        return HCI_COMMAND_DISALLOWED;

    m_state.hostAclLength = aclLen;
    m_state.hostAclPackets = aclCount;
    m_state.hostAclCredits = aclCount;
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleLeSetAddressResolutionEnable(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    return params && params[0] <= 1 ? HCI_SUCCESS : HCI_INVALID_HCI_COMMAND_PARAMETERS;
}

uint8_t QemuBt::handleLeClearResolvingList(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    (void)params;
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleLeAddDeviceToResolvingList(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    return params && params[0] <= 1 ? HCI_SUCCESS : HCI_INVALID_HCI_COMMAND_PARAMETERS;
}

uint8_t QemuBt::handleLeSetPrivacyMode(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    return params && params[0] <= 1 && params[7] <= 1
               ? HCI_SUCCESS
               : HCI_INVALID_HCI_COMMAND_PARAMETERS;
}

uint8_t QemuBt::handleLeSetAdvertisingParameters(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params) return HCI_INVALID_HCI_COMMAND_PARAMETERS;
    if (m_advState.enabled) return HCI_COMMAND_DISALLOWED;

    uint16_t minInterval = 0;
    uint16_t maxInterval = 0;
    readLe16(params, minInterval);
    readLe16(params + 2, maxInterval);
    const uint8_t type = params[4];
    const uint8_t ownAddressType = params[5];
    const uint8_t peerAddressType = params[6];
    const uint8_t channelMap = params[13];
    const uint8_t filterPolicy = params[14];

    if (minInterval < 0x0020 || maxInterval > 0x4000 || minInterval > maxInterval ||
        (type != 0 && type != 2 && type != 3) || ownAddressType != 0 ||
        peerAddressType > 1 ||
        !(channelMap & 0x07) || (channelMap & 0xF8) || filterPolicy != 0)
        return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    m_advState.minInterval = minInterval;
    m_advState.maxInterval = maxInterval;
    m_advState.type = type;
    m_advState.ownAddressType = ownAddressType;
    m_advState.peerAddressType = peerAddressType;
    std::copy(params + 7, params + 13, m_advState.peerAddress.begin());
    m_advState.channelMap = channelMap;
    m_advState.filterPolicy = filterPolicy;
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleLeSetAdvertisingData(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params || params[0] > m_advState.data.size())
        return HCI_INVALID_HCI_COMMAND_PARAMETERS;
    if (m_advState.enabled) return HCI_COMMAND_DISALLOWED;

    m_advState.dataLength = params[0];
    std::copy(params + 1, params + 1 + m_advState.dataLength, m_advState.data.begin());
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleLeSetScanResponseData(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params || params[0] > m_advState.scanResponse.size())
        return HCI_INVALID_HCI_COMMAND_PARAMETERS;
    if (m_advState.enabled) return HCI_COMMAND_DISALLOWED;

    m_advState.scanResponseLength = params[0];
    std::copy(params + 1, params + 1 + m_advState.scanResponseLength,
              m_advState.scanResponse.begin());
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleLeSetAdvertisingEnable(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params || params[0] > 1) return HCI_INVALID_HCI_COMMAND_PARAMETERS;
    if (params[0] == 1 && m_advState.enabled) return HCI_COMMAND_DISALLOWED;

    m_advState.enabled = params[0] != 0;
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleLeSetScanParameters(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params) return HCI_INVALID_HCI_COMMAND_PARAMETERS;
    if (m_scanState.enabled) return HCI_COMMAND_DISALLOWED;

    uint16_t interval = 0;
    uint16_t window = 0;
    readLe16(params + 1, interval);
    readLe16(params + 3, window);
    if (params[0] > 1 || interval < 0x0004 || interval > 0x4000 ||
        window < 0x0004 || window > interval || params[5] != 0 || params[6] != 0)
        return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    m_scanState.type = params[0];
    m_scanState.interval = interval;
    m_scanState.window = window;
    m_scanState.ownAddressType = params[5];
    m_scanState.filterPolicy = params[6];
    return HCI_SUCCESS;
}

uint8_t QemuBt::handleLeSetScanEnable(const uint8_t* params, uint8_t* responseData) {
    (void)responseData;
    if (!params || params[0] > 1 || params[1] > 1)
        return HCI_INVALID_HCI_COMMAND_PARAMETERS;
    if (params[0] == 1 && m_scanState.enabled) return HCI_COMMAND_DISALLOWED;

    m_scanState.enabled = params[0] != 0;
    m_scanState.filterDuplicates = params[1] != 0;
    m_seenAdvertisements.clear();
    return HCI_SUCCESS;
}

bool QemuBt::leAdvertisingReportEnabled() const {
    return (m_state.eventMask & EVENT_MASK_LE_META) &&
           (m_state.leEventMask & LE_EVENT_MASK_ADVERTISING_REPORT);
}

void QemuBt::publishAdvertisement() {
    AirAdvertisement advertisement;
    advertisement.owner = this;
    advertisement.source = m_name;
    advertisement.address = m_state.bdAddr;
    advertisement.addressType = m_advState.ownAddressType;
    advertisement.eventType = m_advState.type == 4 ? 1 : m_advState.type;
    advertisement.dataLength = m_advState.dataLength;
    advertisement.data = m_advState.data;
    advertisement.scanResponseLength = m_advState.scanResponseLength;
    advertisement.scanResponse = m_advState.scanResponse;

    auto existing = std::find_if(airAdvertisements.begin(), airAdvertisements.end(),
                                 [this](const AirAdvertisement& item) {
                                     return item.owner == this;
                                 });
    if (existing == airAdvertisements.end())
        airAdvertisements.push_back(advertisement);
    else
        *existing = advertisement;

    for (QemuBt* scanner : airScanners) {
        if (scanner != this) {
            scanner->queueAdvertisingReport(advertisement.source,
                                            advertisement.address,
                                            advertisement.addressType,
                                            advertisement.eventType,
                                            advertisement.data,
                                            advertisement.dataLength);
            if (scanner->m_scanState.type == 1 &&
                (advertisement.eventType == 0 || advertisement.eventType == 2))
                scanner->queueAdvertisingReport(advertisement.source,
                                                advertisement.address,
                                                advertisement.addressType, 4,
                                                advertisement.scanResponse,
                                                advertisement.scanResponseLength);
            scanner->pumpPendingEvents();
        }
    }

    const std::vector<QemuBt*> controllers = airControllers;
    for (QemuBt* controller : controllers) {
        if (controller != this) controller->tryPendingConnection();
    }
}

void QemuBt::withdrawAdvertisement() {
    airAdvertisements.erase(
        std::remove_if(airAdvertisements.begin(), airAdvertisements.end(),
                       [this](const AirAdvertisement& item) {
                           return item.owner == this;
                       }),
        airAdvertisements.end());
}

void QemuBt::registerScanner() {
    if (std::find(airScanners.begin(), airScanners.end(), this) == airScanners.end())
        airScanners.push_back(this);
}

void QemuBt::unregisterScanner() {
    airScanners.erase(std::remove(airScanners.begin(), airScanners.end(), this),
                      airScanners.end());
}

bool QemuBt::queueAdvertisingReport(const QString& source,
                                    const std::array<uint8_t, 6>& address,
                                    uint8_t addressType, uint8_t eventType,
                                    const std::array<uint8_t, 31>& data,
                                    uint8_t dataLength) {
    if (!m_scanState.enabled || !leAdvertisingReportEnabled() ||
        m_pendingEvents.size() >= BEST_EFFORT_EVENT_MAX)
        return false;

    uint64_t signature = 1469598103934665603ULL;
    if (m_scanState.filterDuplicates) {
        const QByteArray identity = source.toUtf8();
        for (char byte : identity) {
            signature ^= static_cast<uint8_t>(byte);
            signature *= 1099511628211ULL;
        }
        signature ^= eventType;
        signature *= 1099511628211ULL;
        signature ^= dataLength;
        signature *= 1099511628211ULL;
        for (uint8_t i = 0; i < dataLength; ++i) {
            signature ^= data[i];
            signature *= 1099511628211ULL;
        }
        if (std::find(m_seenAdvertisements.begin(), m_seenAdvertisements.end(), signature) !=
            m_seenAdvertisements.end())
            return false;
    }

    QByteArray event(15 + dataLength, 0);
    uint8_t* output = reinterpret_cast<uint8_t*>(event.data());
    output[0] = H4_EVT;
    output[1] = EVT_LE_META;
    output[2] = 12 + dataLength;
    output[3] = EVT_LE_ADVERTISING_REPORT;
    output[4] = 1;
    output[5] = eventType;
    output[6] = addressType;
    std::copy(address.begin(), address.end(), output + 7);
    output[13] = dataLength;
    std::copy(data.begin(), data.begin() + dataLength, output + 14);
    output[14 + dataLength] = static_cast<uint8_t>(-42);
    if (!queueFrame(event, false)) return false;
    if (m_scanState.filterDuplicates) {
        if (m_seenAdvertisements.size() >= DUPLICATE_CACHE_MAX)
            m_seenAdvertisements.erase(m_seenAdvertisements.begin());
        m_seenAdvertisements.push_back(signature);
    }
    return true;
}

void QemuBt::queueAdvertisingReports() {
    if (!m_scanState.enabled || !leAdvertisingReportEnabled()) return;

    for (const AirAdvertisement& advertisement : airAdvertisements) {
        if (advertisement.owner == this) continue;
        queueAdvertisingReport(advertisement.source, advertisement.address,
                               advertisement.addressType, advertisement.eventType,
                               advertisement.data, advertisement.dataLength);
        if (m_scanState.type == 1 &&
            (advertisement.eventType == 0 || advertisement.eventType == 2))
            queueAdvertisingReport(advertisement.source, advertisement.address,
                                   advertisement.addressType, 4,
                                   advertisement.scanResponse,
                                   advertisement.scanResponseLength);
    }
}

void QemuBt::pumpPendingEvents() {
    const size_t pendingBefore = m_pendingEvents.size();
    while (!m_pendingEvents.empty()) {
        const QByteArray& event = m_pendingEvents.front().data;
        if (!pushRxFrame(reinterpret_cast<const uint8_t*>(event.constData()),
                         static_cast<uint32_t>(event.size())))
            return;
        m_pendingEvents.pop_front();
        m_rxCount++;
    }
    if (m_pendingEvents.size() < pendingBefore && m_connection.peer &&
        !m_connection.peer->m_pendingAcl.isEmpty() &&
        std::find(airControllers.begin(), airControllers.end(), m_connection.peer) !=
            airControllers.end())
        m_connection.peer->pumpTx();
}

bool QemuBt::queueFrame(const QByteArray& frame, bool reliable) {
    const size_t limit = reliable ? PENDING_EVENT_MAX : BEST_EFFORT_EVENT_MAX;
    if (m_pendingEvents.size() >= limit) return false;
    PendingFrame pending;
    pending.data = frame;
    pending.reliable = reliable;
    m_pendingEvents.push_back(pending);
    return true;
}

void QemuBt::dropBestEffortEvents() {
    m_pendingEvents.erase(
        std::remove_if(m_pendingEvents.begin(), m_pendingEvents.end(),
                       [](const PendingFrame& frame) { return !frame.reliable; }),
        m_pendingEvents.end());
}

QByteArray QemuBt::connectionCompleteEvent(
    uint8_t status, uint16_t handle, uint8_t role,
    const std::array<uint8_t, 6>& peerAddress) const {
    if (!(m_state.eventMask & EVENT_MASK_LE_META)) return QByteArray();

    const bool enhanced = m_state.leEventMask &
                          LE_EVENT_MASK_ENHANCED_CONNECTION_COMPLETE;
    if (!enhanced && !(m_state.leEventMask & LE_EVENT_MASK_CONNECTION_COMPLETE))
        return QByteArray();

    QByteArray event(enhanced ? 34 : 22, 0);
    uint8_t* out = reinterpret_cast<uint8_t*>(event.data());
    out[0] = H4_EVT;
    out[1] = EVT_LE_META;
    out[2] = enhanced ? 31 : 19;
    out[3] = enhanced ? EVT_LE_ENHANCED_CONNECTION_COMPLETE
                      : EVT_LE_CONNECTION_COMPLETE;
    out[4] = status;
    writeLe16(handle, out + 5);
    out[7] = role;
    out[8] = 0; // Public peer address.
    std::copy(peerAddress.begin(), peerAddress.end(), out + 9);
    const int timing = enhanced ? 27 : 15;
    writeLe16(status == HCI_SUCCESS ? m_connection.interval : 0, out + timing);
    writeLe16(status == HCI_SUCCESS ? m_connection.latency : 0, out + timing + 2);
    writeLe16(status == HCI_SUCCESS ? m_connection.timeout : 0, out + timing + 4);
    out[timing + 6] = 0;
    return event;
}

QByteArray QemuBt::disconnectionCompleteEvent(uint16_t handle, uint8_t reason) const {
    if (!(m_state.eventMask & EVENT_MASK_DISCONNECTION_COMPLETE))
        return QByteArray();
    QByteArray event(7, 0);
    uint8_t* out = reinterpret_cast<uint8_t*>(event.data());
    out[0] = H4_EVT;
    out[1] = EVT_DISCONNECTION_COMPLETE;
    out[2] = 4;
    out[3] = HCI_SUCCESS;
    writeLe16(handle, out + 4);
    out[6] = reason;
    return event;
}

QByteArray QemuBt::remoteFeaturesEvent(uint16_t handle) const {
    if (!(m_state.eventMask & EVENT_MASK_LE_META) ||
        !(m_state.leEventMask & LE_EVENT_MASK_READ_REMOTE_FEATURES_COMPLETE))
        return QByteArray();
    QByteArray event(15, 0);
    uint8_t* out = reinterpret_cast<uint8_t*>(event.data());
    out[0] = H4_EVT;
    out[1] = EVT_LE_META;
    out[2] = 12;
    out[3] = EVT_LE_READ_REMOTE_FEATURES_COMPLETE;
    out[4] = HCI_SUCCESS;
    writeLe16(handle, out + 5);
    return event;
}

QByteArray QemuBt::remoteVersionEvent(uint16_t handle) const {
    if (!(m_state.eventMask & EVENT_MASK_READ_REMOTE_VERSION_COMPLETE))
        return QByteArray();
    QByteArray event(11, 0);
    uint8_t* out = reinterpret_cast<uint8_t*>(event.data());
    out[0] = H4_EVT;
    out[1] = EVT_READ_REMOTE_VERSION_COMPLETE;
    out[2] = 8;
    out[3] = HCI_SUCCESS;
    writeLe16(handle, out + 4);
    out[6] = 0x09;
    return event;
}

QByteArray QemuBt::completedPacketsEvent(uint16_t handle) const {
    QByteArray event(8, 0);
    uint8_t* out = reinterpret_cast<uint8_t*>(event.data());
    out[0] = H4_EVT;
    out[1] = EVT_NUMBER_OF_COMPLETED_PACKETS;
    out[2] = 5;
    out[3] = 1;
    writeLe16(handle, out + 4);
    writeLe16(1, out + 6);
    return event;
}

void QemuBt::tryPendingConnection() {
    if (!m_initiation.active || m_connection.peer) return;
    for (const AirAdvertisement& advertisement : airAdvertisements) {
        if (advertisement.owner != this && advertisement.owner &&
            advertisement.eventType == 0 && advertisement.addressType == 0 &&
            advertisement.address == m_initiation.peerAddress &&
            !advertisement.owner->m_connection.peer) {
            completeConnection(advertisement.owner);
            return;
        }
    }
}

void QemuBt::completeConnection(QemuBt* advertiser) {
    if (!advertiser || !m_initiation.active || m_connection.peer ||
        advertiser->m_connection.peer)
        return;

    const auto allocateHandle = []() -> uint16_t {
        for (uint16_t attempt = 0; attempt < 0x0EFF; ++attempt) {
            const uint16_t candidate = nextConnectionHandle;
            nextConnectionHandle = nextConnectionHandle == 0x0EFF
                ? 1 : uint16_t(nextConnectionHandle + 1);
            bool used = false;
            for (QemuBt* controller : airControllers) {
                if (controller->m_connection.localHandle == candidate) {
                    used = true;
                    break;
                }
            }
            if (!used) return candidate;
        }
        return 0;
    };

    const uint16_t centralHandle = allocateHandle();
    const uint16_t peripheralHandle = allocateHandle();
    if (!centralHandle || !peripheralHandle) return;

    m_connection.peer = advertiser;
    m_connection.localHandle = centralHandle;
    m_connection.peerHandle = peripheralHandle;
    m_connection.interval = m_initiation.intervalMin;
    m_connection.latency = m_initiation.latency;
    m_connection.timeout = m_initiation.timeout;
    advertiser->m_connection.peer = this;
    advertiser->m_connection.localHandle = peripheralHandle;
    advertiser->m_connection.peerHandle = centralHandle;
    advertiser->m_connection.interval = m_connection.interval;
    advertiser->m_connection.latency = m_connection.latency;
    advertiser->m_connection.timeout = m_connection.timeout;

    const QByteArray centralEvent = connectionCompleteEvent(
        HCI_SUCCESS, centralHandle, 0, advertiser->m_state.bdAddr);
    const QByteArray peripheralEvent = advertiser->connectionCompleteEvent(
        HCI_SUCCESS, peripheralHandle, 1, m_state.bdAddr);
    if ((!centralEvent.isEmpty() && m_pendingEvents.size() >= PENDING_EVENT_MAX) ||
        (!peripheralEvent.isEmpty() &&
         advertiser->m_pendingEvents.size() >= PENDING_EVENT_MAX)) {
        m_connection = ConnectionState();
        advertiser->m_connection = ConnectionState();
        return;
    }

    m_initiation = InitiationState();
    advertiser->m_advState.enabled = false;
    advertiser->withdrawAdvertisement();
    if (!centralEvent.isEmpty()) queueFrame(centralEvent, true);
    if (!peripheralEvent.isEmpty()) advertiser->queueFrame(peripheralEvent, true);
    pumpPendingEvents();
    advertiser->pumpPendingEvents();
}

void QemuBt::teardownConnection(uint8_t localReason, uint8_t peerReason,
                                bool notifyLocal) {
    QemuBt* peer = m_connection.peer;
    if (!peer) return;
    const uint16_t localHandle = m_connection.localHandle;
    const uint16_t peerHandle = m_connection.peerHandle;
    m_connection = ConnectionState();
    if (peer->m_connection.peer == this) peer->m_connection = ConnectionState();
    const QByteArray localEvent = disconnectionCompleteEvent(localHandle, localReason);
    if (notifyLocal && !localEvent.isEmpty()) queueFrame(localEvent, true);
    if (std::find(airControllers.begin(), airControllers.end(), peer) !=
        airControllers.end()) {
        const QByteArray peerEvent = peer->disconnectionCompleteEvent(peerHandle, peerReason);
        if (!peerEvent.isEmpty()) peer->queueFrame(peerEvent, true);
        peer->pumpPendingEvents();
    }
}

void QemuBt::detachFromMedium(bool notifyPeer) {
    withdrawAdvertisement();
    unregisterScanner();
    m_initiation = InitiationState();
    if (m_connection.peer)
        teardownConnection(0x16, 0x16, !notifyPeer);
    airControllers.erase(std::remove(airControllers.begin(), airControllers.end(), this),
                         airControllers.end());
}

void QemuBt::commitPendingAction(uint16_t opcode) {
    const PendingAction action = m_pendingAction;
    m_pendingAction = NoAction;
    if (action == ApplySynchronousState) {
        if (opcode == HCI_SET_EVENT_MASK) {
            m_state.eventMask = m_stagedControllerState.eventMask;
        } else if (opcode == HCI_LE_SET_EVENT_MASK) {
            m_state.leEventMask = m_stagedControllerState.leEventMask;
        } else if (opcode == HCI_SET_CONTROLLER_TO_HOST_FLOW_CONTROL) {
            m_state.controllerToHostFlow =
                m_stagedControllerState.controllerToHostFlow;
        } else if (opcode == HCI_HOST_BUFFER_SIZE) {
            m_state.hostAclLength = m_stagedControllerState.hostAclLength;
            m_state.hostAclPackets = m_stagedControllerState.hostAclPackets;
            m_state.hostAclCredits = m_stagedControllerState.hostAclCredits;
        } else if (opcode == HCI_LE_SET_ADVERTISING_PARAMETERS ||
                   opcode == HCI_LE_SET_ADVERTISING_DATA ||
                   opcode == HCI_LE_SET_SCAN_RESPONSE_DATA ||
                   opcode == HCI_LE_SET_ADVERTISING_ENABLE) {
            m_advState = m_stagedAdvState;
        } else if (opcode == HCI_LE_SET_SCAN_PARAMETERS ||
                   opcode == HCI_LE_SET_SCAN_ENABLE) {
            m_scanState = m_stagedScanState;
        }
    } else if (action == ResetController) {
        detachFromMedium(true);
        clearProcedureState();
        ensureMediumRegistration();
    } else if (action == StartInitiation) {
        m_initiation = m_stagedInitiation;
        m_stagedInitiation = InitiationState();
        tryPendingConnection();
    } else if (action == CancelInitiation) {
        const std::array<uint8_t, 6> address = m_initiation.peerAddress;
        m_initiation = InitiationState();
        const QByteArray event = connectionCompleteEvent(0x02, 0, 0, address);
        if (!event.isEmpty()) queueFrame(event, true);
    } else if (action == DisconnectConnection) {
        teardownConnection(0x16, m_pendingReason, true);
    } else if (action == ReadRemoteFeatures) {
        const QByteArray event = remoteFeaturesEvent(m_pendingHandle);
        if (!event.isEmpty()) queueFrame(event, true);
    } else if (action == ReadRemoteVersion) {
        const QByteArray event = remoteVersionEvent(m_pendingHandle);
        if (!event.isEmpty()) queueFrame(event, true);
    }
    m_pendingHandle = 0;
    m_pendingReason = 0;
}

bool QemuBt::handleHostCompletedPackets(const uint8_t* params, uint8_t len) {
    if (!params || len < 1 || params[0] == 0 ||
        uint32_t(len) != 1u + 4u * params[0])
        return false;
    std::map<uint16_t, uint32_t> completed;
    for (uint8_t i = 0; i < params[0]; ++i) {
        uint16_t handle = 0, count = 0;
        readLe16(params + 1 + 4 * i, handle);
        readLe16(params + 3 + 4 * i, count);
        auto outstanding = m_hostAclOutstanding.find(handle);
        if (outstanding == m_hostAclOutstanding.end()) continue;
        completed[handle] += count;
    }
    uint32_t replenish = 0;
    for (const auto& item : completed) {
        auto outstanding = m_hostAclOutstanding.find(item.first);
        if (outstanding == m_hostAclOutstanding.end() ||
            item.second > outstanding->second)
            return false;
        replenish += item.second;
    }
    const uint16_t outstanding = m_state.hostAclPackets >= m_state.hostAclCredits
        ? uint16_t(m_state.hostAclPackets - m_state.hostAclCredits) : 0;
    if (replenish > outstanding) return false;
    for (const auto& item : completed) {
        auto outstanding = m_hostAclOutstanding.find(item.first);
        outstanding->second = uint16_t(outstanding->second - item.second);
        if (outstanding->second == 0) m_hostAclOutstanding.erase(outstanding);
    }
    m_state.hostAclCredits = uint16_t(m_state.hostAclCredits + replenish);
    if (replenish && m_connection.peer) m_deferredWake = m_connection.peer;
    return true;
}

bool QemuBt::handleAclFrame(const uint8_t* frame, uint32_t len) {
    if (!frame || len < 5 || frame[0] != H4_ACL) return true;
    uint16_t flags = 0, payloadLen = 0;
    readLe16(frame + 1, flags);
    readLe16(frame + 3, payloadLen);
    const uint16_t handle = flags & 0x0FFF;
    const uint8_t pb = (flags >> 12) & 0x03;
    const uint8_t bc = (flags >> 14) & 0x03;
    if (len != 5u + payloadLen || payloadLen > 27 || bc != 0 || pb > 1)
        return true;
    if (!m_connection.peer || handle != m_connection.localHandle) return true;

    QemuBt* peer = m_connection.peer;
    if ((peer->m_state.controllerToHostFlow &&
         (peer->m_state.hostAclCredits == 0 ||
          payloadLen > peer->m_state.hostAclLength)) ||
        peer->m_pendingEvents.size() >= RELIABLE_DATA_EVENT_MAX ||
        m_pendingEvents.size() >= RELIABLE_DATA_EVENT_MAX) {
        if (m_pendingAcl.isEmpty()) {
            m_pendingAcl = QByteArray(reinterpret_cast<const char*>(frame), len);
            return true;
        }
        return false;
    }

    QByteArray routed(reinterpret_cast<const char*>(frame), len);
    uint8_t* out = reinterpret_cast<uint8_t*>(routed.data());
    const uint16_t peerFlags = m_connection.peerHandle |
        (uint16_t(pb == 0 ? 2 : 1) << 12);
    writeLe16(peerFlags, out + 1);
    if (peer->m_state.controllerToHostFlow) {
        peer->m_state.hostAclCredits--;
        peer->m_hostAclOutstanding[peer->m_connection.localHandle]++;
    }
    peer->queueFrame(routed, true);
    queueFrame(completedPacketsEvent(m_connection.localHandle), true);
    peer->pumpPendingEvents();
    return true;
}

void QemuBt::tryPendingAcl() {
    if (m_pendingAcl.isEmpty()) return;
    const QByteArray pending = m_pendingAcl;
    m_pendingAcl.clear();
    handleAclFrame(reinterpret_cast<const uint8_t*>(pending.constData()),
                   static_cast<uint32_t>(pending.size()));
}

const QemuBt::CommandSpec QemuBt::s_commands[] = {
    { HCI_RESET, 0, 0, &QemuBt::handleReset },
    { HCI_READ_LOCAL_VERSION_INFO, 0, 8, &QemuBt::handleReadLocalVersionInfo },
    { HCI_READ_LOCAL_SUPPORTED_COMMANDS, 0, 64, &QemuBt::handleReadLocalSupportedCommands },
    { HCI_READ_LOCAL_SUPPORTED_FEATURES, 0, 8, &QemuBt::handleReadLocalSupportedFeatures },
    { HCI_SET_EVENT_MASK, 8, 0, &QemuBt::handleSetEventMask },
    { HCI_SET_EVENT_MASK_PAGE_2, 8, 0, &QemuBt::handleSetEventMaskPage2 },
    { HCI_LE_SET_EVENT_MASK, 8, 0, &QemuBt::handleLeSetEventMask },
    { HCI_LE_READ_BUFFER_SIZE, 0, 3, &QemuBt::handleLeReadBufferSize },
    { HCI_LE_READ_LOCAL_SUPPORTED_FEATURES, 0, 8, &QemuBt::handleLeReadLocalSupportedFeatures },
    { HCI_LE_RAND, 0, 8, &QemuBt::handleLeRand },
    { HCI_READ_BD_ADDR, 0, 6, &QemuBt::handleReadBdAddr },
    { HCI_SET_CONTROLLER_TO_HOST_FLOW_CONTROL, 1, 0, &QemuBt::handleSetControllerToHostFlowControl },
    { HCI_HOST_BUFFER_SIZE, 7, 0, &QemuBt::handleHostBufferSize },
    { HCI_LE_SET_ADDRESS_RESOLUTION_ENABLE, 1, 0, &QemuBt::handleLeSetAddressResolutionEnable },
    { HCI_LE_CLEAR_RESOLVING_LIST, 0, 0, &QemuBt::handleLeClearResolvingList },
    { HCI_LE_ADD_DEVICE_TO_RESOLVING_LIST, 39, 0, &QemuBt::handleLeAddDeviceToResolvingList },
    { HCI_LE_SET_PRIVACY_MODE, 8, 0, &QemuBt::handleLeSetPrivacyMode },
    { HCI_LE_SET_ADVERTISING_PARAMETERS, 15, 0, &QemuBt::handleLeSetAdvertisingParameters },
    { HCI_LE_SET_ADVERTISING_DATA, 32, 0, &QemuBt::handleLeSetAdvertisingData },
    { HCI_LE_SET_SCAN_RESPONSE_DATA, 32, 0, &QemuBt::handleLeSetScanResponseData },
    { HCI_LE_SET_ADVERTISING_ENABLE, 1, 0, &QemuBt::handleLeSetAdvertisingEnable },
    { HCI_LE_SET_SCAN_PARAMETERS, 7, 0, &QemuBt::handleLeSetScanParameters },
    { HCI_LE_SET_SCAN_ENABLE, 2, 0, &QemuBt::handleLeSetScanEnable },
};

const size_t QemuBt::s_commandCount = sizeof(QemuBt::s_commands) / sizeof(QemuBt::s_commands[0]);

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
    if (m_pumping) {
        m_pumpRequested = true;
        return;
    }
    m_pumping = true;
    volatile qemuWifiRing_t* ring = &m_arena->bt_tx;
    uint8_t frame[FRAME_MAX];
    uint8_t response[FRAME_MAX];

    tryPendingAcl();

    if (m_pendingCommandResponse.isEmpty()) {
        pumpPendingEvents();
        if (!m_pendingEvents.empty()) goto done;
    }

    while (true) {
        uint32_t head = ring->head;
        uint32_t tail = ring->tail;
        if (head >= RING_FRAMES || tail >= RING_FRAMES || head == tail) break;

        std::atomic_thread_fence(std::memory_order_acquire);
        const qemuWifiFrame_t* input = const_cast<const qemuWifiFrame_t*>(&ring->frames[head]);
        uint32_t len = input->len;
        bool bounded = len <= FRAME_MAX;
        if (bounded) std::memcpy(frame, input->data, len);

        if (m_pendingCommandResponse.isEmpty()) {
            if (bounded && len && frame[0] == H4_ACL) {
                if (!handleAclFrame(frame, len)) goto done;
                std::atomic_thread_fence(std::memory_order_release);
                ring->head = (head + 1) % RING_FRAMES;
                m_txCount++;
                pumpPendingEvents();
                if (!m_pendingEvents.empty()) goto done;
                continue;
            }
            uint32_t responseLen = 0;
            if (bounded) dispatchCommand(frame, len, response, responseLen);
            if (responseLen) {
                m_pendingCommandResponse = QByteArray(
                    reinterpret_cast<const char*>(response), responseLen);
            } else {
                std::atomic_thread_fence(std::memory_order_release);
                ring->head = (head + 1) % RING_FRAMES;
                m_txCount++;
                continue;
            }
        }

        const uint8_t* completion = reinterpret_cast<const uint8_t*>(
            m_pendingCommandResponse.constData());
        const uint32_t completionLen = static_cast<uint32_t>(
            m_pendingCommandResponse.size());
        if (!pushRxFrame(completion, completionLen)) goto done;
        m_rxCount++;

        const bool commandStatus = completion[1] == EVT_CMD_STATUS;
        const uint16_t opcode = commandStatus
            ? uint16_t(completion[5]) | (uint16_t(completion[6]) << 8)
            : uint16_t(completion[4]) | (uint16_t(completion[5]) << 8);
        const uint8_t status = commandStatus ? completion[3] : completion[6];
        m_pendingCommandResponse.clear();

        if (status == HCI_SUCCESS) commitPendingAction(opcode);
        else m_pendingAction = NoAction;

        std::atomic_thread_fence(std::memory_order_release);
        ring->head = (head + 1) % RING_FRAMES;
        m_txCount++;

        if (status == HCI_SUCCESS) {
            if (opcode == HCI_LE_SET_ADVERTISING_ENABLE) {
                if (m_advState.enabled)
                    publishAdvertisement();
                else
                    withdrawAdvertisement();
            } else if (opcode == HCI_LE_SET_SCAN_ENABLE) {
                if (m_scanState.enabled) {
                    registerScanner();
                    queueAdvertisingReports();
                } else {
                    unregisterScanner();
                    dropBestEffortEvents();
                }
            }
        }
        pumpPendingEvents();
        if (!m_pendingEvents.empty()) goto done;
    }

done:
    m_pumping = false;
    if (m_deferredWake) {
        QemuBt* peer = m_deferredWake;
        m_deferredWake = nullptr;
        if (std::find(airControllers.begin(), airControllers.end(), peer) !=
            airControllers.end())
            peer->pumpTx();
    }
    if (m_pumpRequested) {
        m_pumpRequested = false;
        pumpTx();
    }
}

void QemuBt::runAction() {
    ensureMediumRegistration();
    pumpTx();
}

void QemuBt::injectHostFrame(const QByteArray& frame) {
    if (frame.size() <= 0 || static_cast<uint32_t>(frame.size()) > FRAME_MAX) return;
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
