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
constexpr size_t DUPLICATE_CACHE_MAX = 128;

constexpr uint8_t HCI_SUCCESS = 0x00;
constexpr uint8_t HCI_UNKNOWN_HCI_COMMAND = 0x01;
constexpr uint8_t HCI_COMMAND_DISALLOWED = 0x0C;
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
constexpr uint16_t HCI_LE_SET_ADVERTISING_PARAMETERS = 0x2006;
constexpr uint16_t HCI_LE_SET_ADVERTISING_DATA = 0x2008;
constexpr uint16_t HCI_LE_SET_SCAN_RESPONSE_DATA = 0x2009;
constexpr uint16_t HCI_LE_SET_ADVERTISING_ENABLE = 0x200A;
constexpr uint16_t HCI_LE_SET_SCAN_PARAMETERS = 0x200B;
constexpr uint16_t HCI_LE_SET_SCAN_ENABLE = 0x200C;

constexpr uint8_t EVT_CMD_COMPLETE = 0x0E;
constexpr uint8_t EVT_LE_META = 0x3E;
constexpr uint8_t EVT_LE_ADVERTISING_REPORT = 0x02;

constexpr uint64_t EVENT_MASK_LE_META = uint64_t(1) << 61;
constexpr uint64_t LE_EVENT_MASK_ADVERTISING_REPORT = uint64_t(1) << 1;

struct AirAdvertisement {
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
    m_type = "bt";
    m_arena->bt_rx.head = m_arena->bt_rx.tail = 0;
    m_arena->bt_tx.head = m_arena->bt_tx.tail = 0;

    const QByteArray identity = name.toUtf8();
    uint64_t hash = 1469598103934665603ULL;
    for (char byte : identity) {
        hash ^= static_cast<uint8_t>(byte);
        hash *= 1099511628211ULL;
    }
    for (int i = 0; i < 5; ++i)
        m_state.bdAddr[i] = (hash >> (i * 8)) & 0xFF;
    m_state.bdAddr[5] = 0x02; // Locally administered unicast address.
}

QemuBt::~QemuBt() {
    withdrawAdvertisement();
    unregisterScanner();
}

void QemuBt::reset() {
    QemuModule::reset();
    clearProcedureState();
    m_arena->bt_rx.head = m_arena->bt_rx.tail = 0;
    m_arena->bt_tx.head = m_arena->bt_tx.tail = 0;
    m_rxCount = m_txCount = 0;
}

void QemuBt::clearProcedureState() {
    withdrawAdvertisement();
    unregisterScanner();
    m_state.eventMask = 0;
    m_state.leEventMask = 0;
    m_state.controllerToHostFlow = false;
    m_state.hostAclLength = 0;
    m_state.hostAclPackets = 0;
    m_advState = AdvertisingState();
    m_scanState = ScanningState();
    m_pendingCommandResponse.clear();
    m_pendingEvents.clear();
    m_seenAdvertisements.clear();
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
    const uint8_t status = (this->*(spec->handler))(cmd + 4, responseData);
    const uint8_t dataLength = status == HCI_SUCCESS ? spec->successDataLength : 0;
    return buildCommandComplete(opcode, status, responseData, dataLength,
                                response, responseLen);
}

uint8_t QemuBt::handleReset(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    (void)responseData;
    clearProcedureState();
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

uint8_t QemuBt::handleReadLocalSupportedFeatures(const uint8_t* params, uint8_t* responseData) {
    (void)params;
    if (!responseData) return HCI_INVALID_HCI_COMMAND_PARAMETERS;

    const uint64_t features = 0x0000000060000000ULL;
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

    if (scoLen != 0 || scoCount != 0 || aclLen == 0 || aclCount == 0) {
        return HCI_INVALID_HCI_COMMAND_PARAMETERS;
    }

    m_state.hostAclLength = aclLen;
    m_state.hostAclPackets = aclCount;
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
    if (!m_scanState.enabled) m_pendingEvents.clear();
    return HCI_SUCCESS;
}

bool QemuBt::leAdvertisingReportEnabled() const {
    return (m_state.eventMask & EVENT_MASK_LE_META) &&
           (m_state.leEventMask & LE_EVENT_MASK_ADVERTISING_REPORT);
}

void QemuBt::publishAdvertisement() {
    AirAdvertisement advertisement;
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
                                     return item.source == m_name;
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
}

void QemuBt::withdrawAdvertisement() {
    airAdvertisements.erase(
        std::remove_if(airAdvertisements.begin(), airAdvertisements.end(),
                       [this](const AirAdvertisement& item) {
                           return item.source == m_name;
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
        m_pendingEvents.size() >= PENDING_EVENT_MAX)
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
    m_pendingEvents.push_back(event);
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
        if (advertisement.source == m_name) continue;
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
    while (!m_pendingEvents.empty()) {
        const QByteArray& event = m_pendingEvents.front();
        if (!pushRxFrame(reinterpret_cast<const uint8_t*>(event.constData()),
                         static_cast<uint32_t>(event.size())))
            return;
        m_pendingEvents.pop_front();
        m_rxCount++;
    }
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
    volatile qemuWifiRing_t* ring = &m_arena->bt_tx;
    uint8_t frame[FRAME_MAX];
    uint8_t response[FRAME_MAX];

    if (m_pendingCommandResponse.isEmpty()) {
        pumpPendingEvents();
        if (!m_pendingEvents.empty()) return;
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
        if (!pushRxFrame(completion, completionLen)) return;
        m_rxCount++;

        const uint16_t opcode = uint16_t(completion[4]) | (uint16_t(completion[5]) << 8);
        const uint8_t status = completion[6];
        m_pendingCommandResponse.clear();

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
                }
            }
        }
        pumpPendingEvents();
        if (!m_pendingEvents.empty()) return;
    }
}

void QemuBt::runAction() {
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
