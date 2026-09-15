/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                              *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef QEMUBT_H
#define QEMUBT_H

#include "qemumodule.h"
#include <QByteArray>
#include <cstdint>
#include <array>

class QemuBt : public QemuModule {
public:
    QemuBt( QemuDevice* mcu, QString name, int n,
            uint64_t memStart, uint64_t memEnd );

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
        std::array<uint8_t, 6> bdAddr{};
    };

    struct CommandSpec {
        uint16_t opcode;
        uint8_t parameterLength;
        uint8_t successDataLength;
        using Handler = bool (QemuBt::*)(const uint8_t* params, uint8_t* responseData);
        Handler handler;
    };

    static constexpr uint16_t HCI_RESET = 0x0C03;
    static constexpr uint16_t HCI_READ_LOCAL_VERSION_INFO = 0x1001;
    static constexpr uint16_t HCI_READ_LOCAL_SUPPORTED_FEATURES = 0x1003;
    static constexpr uint16_t HCI_SET_EVENT_MASK = 0x0C01;
    static constexpr uint16_t HCI_SET_EVENT_MASK_PAGE_2 = 0x0C63;
    static constexpr uint16_t HCI_LE_SET_EVENT_MASK = 0x2001;
    static constexpr uint16_t HCI_LE_READ_BUFFER_SIZE = 0x2002;
    static constexpr uint16_t HCI_LE_READ_LOCAL_SUPPORTED_FEATURES = 0x2003;
    static constexpr uint16_t HCI_READ_BD_ADDR = 0x1009;
    static constexpr uint16_t HCI_SET_CONTROLLER_TO_HOST_FLOW_CONTROL = 0x0C31;
    static constexpr uint16_t HCI_HOST_BUFFER_SIZE = 0x0C33;
    static constexpr uint16_t HCI_LE_SET_ADDRESS_RESOLUTION_ENABLE = 0x202D;
    static constexpr uint16_t HCI_LE_CLEAR_RESOLVING_LIST = 0x2029;
    static constexpr uint16_t HCI_LE_ADD_DEVICE_TO_RESOLVING_LIST = 0x2027;
    static constexpr uint16_t HCI_LE_SET_PRIVACY_MODE = 0x204E;

    static constexpr uint8_t HCI_SUCCESS = 0x00;
    static constexpr uint8_t HCI_UNKNOWN_HCI_COMMAND = 0x01;
    static constexpr uint8_t HCI_INVALID_HCI_COMMAND_PARAMETERS = 0x12;

    static constexpr uint8_t H4_CMD = 0x01;
    static constexpr uint8_t H4_EVT = 0x04;

    bool buildCommandComplete(uint16_t opcode, uint8_t status, const uint8_t* data, uint8_t dataLen, uint8_t* out, uint32_t& outLen);
    bool dispatchCommand(const uint8_t* cmd, uint32_t len, uint8_t* response, uint32_t& responseLen);
    bool pushRxFrame(const uint8_t* data, uint32_t len);
    void pumpTx();

    ControllerState m_state;
    uint64_t m_rxCount;
    uint64_t m_txCount;

    static const CommandSpec s_commands[];
    static constexpr size_t s_commandCount = sizeof(s_commands) / sizeof(CommandSpec);
};

#endif