/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef QEMUBT_H
#define QEMUBT_H

#include "qemumodule.h"
#include <QByteArray>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <vector>

class QemuBt : public QemuModule {
public:
    QemuBt( QemuDevice* mcu, QString name, int n,
            uint64_t memStart, uint64_t memEnd );
    ~QemuBt();

    void reset() override;
    void runAction() override;

    void injectHostFrame( const QByteArray& frame );

    uint64_t rxCount() const { return m_rxCount; }
    uint64_t txCount() const { return m_txCount; }

private:
    struct ControllerState {
        uint64_t eventMask = 0;
        uint64_t leEventMask = 0;
        bool controllerToHostFlow = false;
        uint16_t hostAclLength = 0;
        uint16_t hostAclPackets = 0;
        uint16_t hostAclCredits = 0;
        std::array<uint8_t, 6> bdAddr{};
    };

    struct AdvertisingState {
        bool enabled = false;
        uint16_t minInterval = 0x0800;
        uint16_t maxInterval = 0x0800;
        uint8_t type = 0;
        uint8_t ownAddressType = 0;
        uint8_t peerAddressType = 0;
        std::array<uint8_t, 6> peerAddress{};
        uint8_t channelMap = 7;
        uint8_t filterPolicy = 0;
        uint8_t dataLength = 0;
        std::array<uint8_t, 31> data{};
        uint8_t scanResponseLength = 0;
        std::array<uint8_t, 31> scanResponse{};
    };

    struct ScanningState {
        bool enabled = false;
        uint8_t type = 0;
        uint16_t interval = 0x0010;
        uint16_t window = 0x0010;
        uint8_t ownAddressType = 0;
        uint8_t filterPolicy = 0;
        bool filterDuplicates = false;
    };

    struct CommandSpec {
        uint16_t opcode;
        uint8_t parameterLength;
        uint8_t successDataLength;
        using Handler = uint8_t (QemuBt::*)(const uint8_t* params, uint8_t* responseData);
        Handler handler;
    };

    struct PendingFrame {
        QByteArray data;
        bool reliable = false;
    };

    struct InitiationState {
        bool active = false;
        std::array<uint8_t, 6> peerAddress{};
        uint16_t intervalMin = 0;
        uint16_t intervalMax = 0;
        uint16_t latency = 0;
        uint16_t timeout = 0;
    };

    struct ConnectionState {
        QemuBt* peer = nullptr;
        uint16_t localHandle = 0;
        uint16_t peerHandle = 0;
        uint16_t interval = 0;
        uint16_t latency = 0;
        uint16_t timeout = 0;
    };

    enum PendingAction {
        NoAction,
        ApplySynchronousState,
        ResetController,
        StartInitiation,
        CancelInitiation,
        DisconnectConnection,
        ReadRemoteFeatures
    };

    bool buildCommandComplete(uint16_t opcode, uint8_t status, const uint8_t* data, uint8_t dataLen, uint8_t* out, uint32_t& outLen);
    bool buildCommandStatus(uint16_t opcode, uint8_t status, uint8_t* out, uint32_t& outLen);
    bool dispatchCommand(const uint8_t* cmd, uint32_t len, uint8_t* response, uint32_t& responseLen);
    bool handleAclFrame(const uint8_t* frame, uint32_t len);
    void tryPendingAcl();
    bool handleHostCompletedPackets(const uint8_t* params, uint8_t len);
    bool pushRxFrame(const uint8_t* data, uint32_t len);
    bool leAdvertisingReportEnabled() const;
    void ensureMediumRegistration();
    void clearProcedureState();
    void publishAdvertisement();
    void withdrawAdvertisement();
    void registerScanner();
    void unregisterScanner();
    bool queueFrame(const QByteArray& frame, bool reliable);
    bool queueAdvertisingReport(const QString& source,
                                const std::array<uint8_t, 6>& address,
                                uint8_t addressType, uint8_t eventType,
                                const std::array<uint8_t, 31>& data, uint8_t dataLength);
    void queueAdvertisingReports();
    void dropBestEffortEvents();
    void commitPendingAction(uint16_t opcode);
    void tryPendingConnection();
    void completeConnection(QemuBt* advertiser);
    void teardownConnection(uint8_t localReason, uint8_t peerReason, bool notifyLocal);
    void detachFromMedium(bool notifyPeer);
    QByteArray connectionCompleteEvent(uint8_t status, uint16_t handle, uint8_t role,
                                       const std::array<uint8_t, 6>& peerAddress) const;
    QByteArray disconnectionCompleteEvent(uint16_t handle, uint8_t reason) const;
    QByteArray remoteFeaturesEvent(uint16_t handle) const;
    QByteArray completedPacketsEvent(uint16_t handle) const;
    void pumpPendingEvents();
    void pumpTx();

    uint8_t handleReset(const uint8_t* params, uint8_t* responseData);
    uint8_t handleReadLocalVersionInfo(const uint8_t* params, uint8_t* responseData);
    uint8_t handleReadLocalSupportedFeatures(const uint8_t* params, uint8_t* responseData);
    uint8_t handleSetEventMask(const uint8_t* params, uint8_t* responseData);
    uint8_t handleSetEventMaskPage2(const uint8_t* params, uint8_t* responseData);
    uint8_t handleLeSetEventMask(const uint8_t* params, uint8_t* responseData);
    uint8_t handleLeReadBufferSize(const uint8_t* params, uint8_t* responseData);
    uint8_t handleLeReadLocalSupportedFeatures(const uint8_t* params, uint8_t* responseData);
    uint8_t handleReadBdAddr(const uint8_t* params, uint8_t* responseData);
    uint8_t handleSetControllerToHostFlowControl(const uint8_t* params, uint8_t* responseData);
    uint8_t handleHostBufferSize(const uint8_t* params, uint8_t* responseData);
    uint8_t handleLeSetAddressResolutionEnable(const uint8_t* params, uint8_t* responseData);
    uint8_t handleLeClearResolvingList(const uint8_t* params, uint8_t* responseData);
    uint8_t handleLeAddDeviceToResolvingList(const uint8_t* params, uint8_t* responseData);
    uint8_t handleLeSetPrivacyMode(const uint8_t* params, uint8_t* responseData);

    uint8_t handleLeSetAdvertisingParameters(const uint8_t* params, uint8_t* responseData);
    uint8_t handleLeSetAdvertisingData(const uint8_t* params, uint8_t* responseData);
    uint8_t handleLeSetScanResponseData(const uint8_t* params, uint8_t* responseData);
    uint8_t handleLeSetAdvertisingEnable(const uint8_t* params, uint8_t* responseData);
    uint8_t handleLeSetScanParameters(const uint8_t* params, uint8_t* responseData);
    uint8_t handleLeSetScanEnable(const uint8_t* params, uint8_t* responseData);

    AdvertisingState m_advState;
    AdvertisingState m_stagedAdvState;
    ScanningState m_scanState;
    ScanningState m_stagedScanState;
    InitiationState m_initiation;
    InitiationState m_stagedInitiation;
    ConnectionState m_connection;
    QByteArray m_pendingCommandResponse;
    QByteArray m_pendingAcl;
    std::deque<PendingFrame> m_pendingEvents;
    std::vector<uint64_t> m_seenAdvertisements;
    PendingAction m_pendingAction = NoAction;
    uint16_t m_pendingHandle = 0;
    uint8_t m_pendingReason = 0;
    bool m_pumping = false;
    bool m_pumpRequested = false;
    QemuBt* m_deferredWake = nullptr;

    ControllerState m_state;
    ControllerState m_stagedControllerState;
    std::map<uint16_t, uint16_t> m_hostAclOutstanding;
    uint64_t m_rxCount;
    uint64_t m_txCount;

    static const CommandSpec s_commands[];
    static const size_t s_commandCount;
};

#endif
