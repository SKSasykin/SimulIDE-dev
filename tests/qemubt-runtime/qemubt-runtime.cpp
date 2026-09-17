#include "blechatclient.h"
#include "blechatformat.h"
#include "qemubt.h"

#include <QObject>

#include <algorithm>
#include <array>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using Bytes = std::vector<uint8_t>;

struct Endpoint {
    qemuArena_t arena{};
    QemuBt controller;

    explicit Endpoint(const char* name)
        : controller(arena, QString::fromLatin1(name)) {}
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Bytes command(uint16_t opcode, std::initializer_list<uint8_t> params = {}) {
    Bytes frame{0x01, uint8_t(opcode), uint8_t(opcode >> 8), uint8_t(params.size())};
    frame.insert(frame.end(), params.begin(), params.end());
    return frame;
}

void sendFrame(Endpoint& endpoint, const Bytes& frame) {
    volatile qemuWifiRing_t& ring = endpoint.arena.bt_tx;
    const uint32_t tail = ring.tail;
    const uint32_t next = (tail + 1) % QEMU_WIFI_RING_FRAMES;
    require(next != ring.head, "test TX ring is full");
    require(frame.size() <= QEMU_WIFI_FRAME_MAX, "test frame is oversized");
    qemuWifiFrame_t& slot = const_cast<qemuWifiFrame_t&>(ring.frames[tail]);
    slot.len = uint32_t(frame.size());
    std::copy(frame.begin(), frame.end(), slot.data);
    ring.tail = next;
    endpoint.controller.runAction();
}

bool popFrame(Endpoint& endpoint, Bytes& frame) {
    volatile qemuWifiRing_t& ring = endpoint.arena.bt_rx;
    if (ring.head == ring.tail) return false;
    const uint32_t head = ring.head;
    const qemuWifiFrame_t& slot = const_cast<const qemuWifiFrame_t&>(ring.frames[head]);
    frame.assign(slot.data, slot.data + slot.len);
    ring.head = (head + 1) % QEMU_WIFI_RING_FRAMES;
    endpoint.controller.runAction();
    return true;
}

std::vector<Bytes> drain(Endpoint& endpoint) {
    std::vector<Bytes> frames;
    Bytes frame;
    while (popFrame(endpoint, frame)) frames.push_back(frame);
    return frames;
}

Bytes transact(Endpoint& endpoint, const Bytes& request) {
    sendFrame(endpoint, request);
    Bytes response;
    require(popFrame(endpoint, response), "missing command response");
    return response;
}

void requireSuccessComplete(const Bytes& frame, uint16_t opcode) {
    require(frame.size() >= 7, "short Command Complete");
    require(frame[0] == 0x04 && frame[1] == 0x0e, "expected Command Complete");
    require(frame[4] == uint8_t(opcode) && frame[5] == uint8_t(opcode >> 8),
            "wrong Command Complete opcode");
    require(frame[6] == 0, "command failed");
}

void configureEvents(Endpoint& endpoint) {
    requireSuccessComplete(transact(endpoint, command(0x0c01,
        {0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20})), 0x0c01);
    requireSuccessComplete(transact(endpoint, command(0x2001,
        {0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00})), 0x2001);
}

std::array<uint8_t, 6> readAddress(Endpoint& endpoint) {
    const Bytes response = transact(endpoint, command(0x1009));
    requireSuccessComplete(response, 0x1009);
    require(response.size() == 13, "wrong Read BD_ADDR response length");
    std::array<uint8_t, 6> address{};
    std::copy(response.begin() + 7, response.end(), address.begin());
    return address;
}

void enableAdvertising(Endpoint& endpoint) {
    requireSuccessComplete(transact(endpoint, command(0x2006,
        {0x20, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00,
         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x00})), 0x2006);
    requireSuccessComplete(transact(endpoint, command(0x200a, {0x01})), 0x200a);
}

void configureOneHostAclCredit(Endpoint& endpoint) {
    requireSuccessComplete(transact(endpoint, command(0x0c31, {0x01})), 0x0c31);
    requireSuccessComplete(transact(endpoint, command(0x0c33,
        {0x1b, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00})), 0x0c33);
}

struct Link {
    uint16_t centralHandle;
    uint16_t peripheralHandle;
};

Link connect(Endpoint& central, Endpoint& peripheral, bool hostFlow) {
    configureEvents(central);
    configureEvents(peripheral);
    if (hostFlow) configureOneHostAclCredit(peripheral);
    const std::array<uint8_t, 6> address = readAddress(peripheral);
    enableAdvertising(peripheral);

    Bytes params{0x10, 0x00, 0x10, 0x00, 0x00, 0x00};
    params.insert(params.end(), address.begin(), address.end());
    const uint8_t tail[] = {
        0x00, 0x18, 0x00, 0x28, 0x00, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x00
    };
    params.insert(params.end(), std::begin(tail), std::end(tail));
    require(params.size() == 25, "bad Create Connection test vector");
    Bytes create{0x01, 0x0d, 0x20, 0x19};
    create.insert(create.end(), params.begin(), params.end());
    sendFrame(central, create);

    const std::vector<Bytes> centralFrames = drain(central);
    const std::vector<Bytes> peripheralFrames = drain(peripheral);
    require(centralFrames.size() == 2, "central did not receive status and connection event");
    require(peripheralFrames.size() == 1, "peripheral did not receive connection event");
    require(centralFrames[0] == Bytes({0x04, 0x0f, 0x04, 0x00, 0x01, 0x0d, 0x20}),
            "wrong Create Connection status");
    require(centralFrames[1].size() == 22 && centralFrames[1][3] == 0x01 &&
            centralFrames[1][4] == 0x00 && centralFrames[1][7] == 0x00,
            "wrong central Connection Complete");
    require(peripheralFrames[0].size() == 22 && peripheralFrames[0][3] == 0x01 &&
            peripheralFrames[0][4] == 0x00 && peripheralFrames[0][7] == 0x01,
            "wrong peripheral Connection Complete");
    const uint16_t centralHandle = centralFrames[1][5] | (uint16_t(centralFrames[1][6]) << 8);
    const uint16_t peripheralHandle = peripheralFrames[0][5] |
                                      (uint16_t(peripheralFrames[0][6]) << 8);
    require(centralHandle && peripheralHandle && centralHandle != peripheralHandle,
            "invalid local connection handles");
    return {centralHandle, peripheralHandle};
}

Bytes acl(uint16_t handle, uint8_t value) {
    return {0x02, uint8_t(handle), uint8_t(handle >> 8), 0x05, 0x00,
            0x01, 0x00, 0x04, 0x00, value};
}

void testConnectionAndAclCredits() {
    Endpoint central("runtime-central");
    Endpoint peripheral("runtime-peripheral");
    const Link link = connect(central, peripheral, true);

    sendFrame(central, acl(link.centralHandle, 0x11));
    std::vector<Bytes> peerFrames = drain(peripheral);
    std::vector<Bytes> centralFrames = drain(central);
    require(peerFrames.size() == 1 && peerFrames[0].size() == 10,
            "first ACL packet was not routed");
    const uint16_t routedFlags = peerFrames[0][1] | (uint16_t(peerFrames[0][2]) << 8);
    require((routedFlags & 0x0fff) == link.peripheralHandle &&
            ((routedFlags >> 12) & 3) == 2 && peerFrames[0][9] == 0x11,
            "ACL handle or PB translation is wrong");
    require(centralFrames.size() == 1 && centralFrames[0] == Bytes(
        {0x04, 0x13, 0x05, 0x01, uint8_t(link.centralHandle),
         uint8_t(link.centralHandle >> 8), 0x01, 0x00}),
        "missing Number Of Completed Packets");

    sendFrame(central, acl(link.centralHandle, 0x22));
    require(drain(peripheral).empty(), "ACL ignored exhausted host credits");
    require(drain(central).empty(), "sender credit returned before routing");

    sendFrame(peripheral, command(0x0c35,
        {0x01, uint8_t(link.peripheralHandle), uint8_t(link.peripheralHandle >> 8),
         0x01, 0x00}));
    peerFrames = drain(peripheral);
    centralFrames = drain(central);
    require(peerFrames.size() == 1 && peerFrames[0] == Bytes(
        {0x02, uint8_t(link.peripheralHandle),
         uint8_t((link.peripheralHandle >> 8) | 0x20), 0x05, 0x00,
         0x01, 0x00, 0x04, 0x00, 0x22}),
            "Host Number Of Completed Packets did not resume pending ACL");
    require(centralFrames.size() == 1 && centralFrames[0] == Bytes(
        {0x04, 0x13, 0x05, 0x01, uint8_t(link.centralHandle),
         uint8_t(link.centralHandle >> 8), 0x01, 0x00}),
            "resumed ACL did not return sender credit");
}

void testRxBackpressure() {
    Endpoint endpoint("runtime-backpressure");
    volatile qemuWifiRing_t& rx = endpoint.arena.bt_rx;
    for (uint32_t i = 0; i < QEMU_WIFI_RING_FRAMES - 1; ++i) {
        qemuWifiFrame_t& slot = const_cast<qemuWifiFrame_t&>(rx.frames[rx.tail]);
        slot.len = 3;
        slot.data[0] = 0x04;
        slot.data[1] = 0xff;
        slot.data[2] = 0x00;
        rx.tail = (rx.tail + 1) % QEMU_WIFI_RING_FRAMES;
    }
    sendFrame(endpoint, command(0x0c03));
    require(endpoint.arena.bt_tx.head != endpoint.arena.bt_tx.tail,
            "RX-full command was consumed before its response fit");

    Bytes discarded;
    require(popFrame(endpoint, discarded), "failed to free RX slot");
    require(endpoint.arena.bt_tx.head == endpoint.arena.bt_tx.tail,
            "command did not resume after RX space was freed");
    const std::vector<Bytes> frames = drain(endpoint);
    require(std::find(frames.begin(), frames.end(),
        Bytes({0x04, 0x0e, 0x04, 0x01, 0x03, 0x0c, 0x00})) != frames.end(),
        "Reset completion was lost under RX backpressure");
}

void testChatFormat() {
    QByteArray hello("Hello");
    require(bleChatFormatString(hello) == "Hello", "string readable failed");
    require(bleChatFormatHex(hello) == "48 65 6C 6C 6F", "hex format failed");
    QByteArray mixed;
    mixed.append(char(0x48));
    mixed.append(char(0x00));
    mixed.append(char(0xFF));
    mixed.append(char(0x5C));
    require(bleChatFormatString(mixed) == "H\\x00\\xFF\\\\", "string escape failed");
    require(bleChatFormatHex(mixed) == "48 00 FF 5C", "hex escape failed");
    QByteArray parsed;
    QString error;
    require(bleChatParseString("H\\x00\\xFF\\\\", parsed, error), "string parse failed");
    require(parsed == mixed, "string roundtrip failed");
    require(!bleChatParseString("bad\\x1", parsed, error), "bad string escape accepted");
    require(!bleChatParseString("trailing\\", parsed, error), "trailing backslash accepted");
    require(bleChatParseHex("48 65 6c 6c 6F", parsed, error), "hex parse failed");
    require(parsed == hello, "hex roundtrip failed");
    require(bleChatParseHex("48656C6C6F", parsed, error), "compact hex parse failed");
    require(parsed == hello, "compact hex roundtrip failed");
    require(!bleChatParseHex("0G", parsed, error), "bad hex accepted");
    require(!bleChatParseHex("ABC", parsed, error), "odd hex accepted");
    QByteArray empty;
    require(bleChatFormatString(empty).isEmpty(), "empty string format failed");
    require(bleChatFormatHex(empty).isEmpty(), "empty hex format failed");
    require(bleChatParseHex("", parsed, error) && parsed.isEmpty(), "empty hex parse failed");
}

const Bytes CHAT_SVC_UUID = {0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
                             0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};
const Bytes CHAT_CHR_UUID = {0x11, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
                             0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE};

struct ChatAttServer {
    uint16_t handle = 0;
    Bytes value{0x01};
    bool subscribed = false;
    Bytes reassembly;
    int expected = 0;
};

Bytes chatL2cap(const Bytes& att) {
    Bytes packet{uint8_t(att.size()), uint8_t(att.size() >> 8), 0x04, 0x00};
    packet.insert(packet.end(), att.begin(), att.end());
    return packet;
}

void peerSendAcl(Endpoint& peripheral, uint16_t handle, const Bytes& l2cap) {
    size_t offset = 0;
    bool first = true;
    while (offset < l2cap.size()) {
        const size_t chunk = std::min<size_t>(27, l2cap.size() - offset);
        const uint16_t flags = (handle & 0x0FFF) | (uint16_t(first ? 0 : 1) << 12);
        Bytes frame{0x02, uint8_t(flags), uint8_t(flags >> 8),
                    uint8_t(chunk), uint8_t(chunk >> 8)};
        frame.insert(frame.end(), l2cap.begin() + offset,
                     l2cap.begin() + offset + chunk);
        sendFrame(peripheral, frame);
        offset += chunk;
        first = false;
    }
}

uint16_t chatU16(const Bytes& data, size_t pos) {
    return uint16_t(data[pos]) | (uint16_t(data[pos + 1]) << 8);
}

void peerAnswerAtt(Endpoint& peripheral, ChatAttServer& server, const Bytes& att) {
    if (att.empty()) return;
    static const Bytes gapName = {'S', 'i', 'm', 'u', 'l', 'I', 'D', 'E'};
    const uint8_t opcode = att[0];
    Bytes response;
    uint16_t notifyHandle = 0;
    if (opcode == 0x10 && att.size() == 7 && chatU16(att, 5) == 0x2800) {
        const uint16_t start = chatU16(att, 1);
        const uint16_t end = chatU16(att, 3);
        const bool gapIn = start <= 1 && 1 <= end;
        const bool demoIn = start <= 4 && 4 <= end;
        if (gapIn) {
            response = {0x11, 6, 0x01, 0x00, 0x03, 0x00, 0x00, 0x18};
        } else if (demoIn) {
            response = {0x11, 20, 0x04, 0x00, 0x07, 0x00};
            response.insert(response.end(), CHAT_SVC_UUID.begin(), CHAT_SVC_UUID.end());
        } else {
            response = {0x01, 0x10, uint8_t(start), uint8_t(start >> 8), 0x0A};
        }
    } else if (opcode == 0x08 && att.size() == 7 && chatU16(att, 5) == 0x2803) {
        const uint16_t start = chatU16(att, 1);
        const uint16_t end = chatU16(att, 3);
        if (start <= 5 && 5 <= end) {
            response = {0x09, 21, 0x05, 0x00, 0x1A, 0x06, 0x00};
            response.insert(response.end(), CHAT_CHR_UUID.begin(), CHAT_CHR_UUID.end());
        } else if (start <= 2 && 2 <= end) {
            response = {0x09, 7, 0x02, 0x00, 0x02, 0x03, 0x00, 0x00, 0x2A};
        } else {
            response = {0x01, 0x08, uint8_t(start), uint8_t(start >> 8), 0x0A};
        }
    } else if (opcode == 0x04 && att.size() == 5) {
        const uint16_t start = chatU16(att, 1);
        const uint16_t end = chatU16(att, 3);
        if (start <= 7 && 7 <= end) {
            response = {0x05, 0x01, 0x07, 0x00, 0x02, 0x29};
        } else {
            response = {0x01, 0x04, uint8_t(start), uint8_t(start >> 8), 0x0A};
        }
    } else if (opcode == 0x0A && att.size() == 3) {
        const uint16_t handle = chatU16(att, 1);
        if (handle == 6) {
            response = {0x0B};
            response.insert(response.end(), server.value.begin(), server.value.end());
        } else if (handle == 7) {
            response = {0x0B, uint8_t(server.subscribed ? 0x01 : 0x00), 0x00};
        } else if (handle == 3) {
            response = {0x0B};
            response.insert(response.end(), gapName.begin(), gapName.end());
        } else {
            response = {0x01, 0x0A, uint8_t(handle), uint8_t(handle >> 8), 0x0A};
        }
    } else if (opcode == 0x12 && att.size() >= 3) {
        const uint16_t handle = chatU16(att, 1);
        const Bytes data(att.begin() + 3, att.end());
        if (handle == 6 && !data.empty() && data.size() <= 20) {
            server.value = data;
            response = {0x13};
            notifyHandle = 6;
        } else if (handle == 7 && data.size() == 2) {
            server.subscribed = (data[0] == 0x01 && data[1] == 0x00);
            response = {0x13};
        } else if (handle == 3) {
            response = {0x01, 0x12, uint8_t(handle), uint8_t(handle >> 8), 0x03};
        } else {
            response = {0x01, 0x12, uint8_t(handle), uint8_t(handle >> 8), 0x0A};
        }
    } else {
        return;
    }
    peerSendAcl(peripheral, server.handle, chatL2cap(response));
    if (notifyHandle != 0 && server.subscribed) {
        const Bytes notify{0x1B, uint8_t(notifyHandle), uint8_t(notifyHandle >> 8)};
        Bytes payload = notify;
        payload.insert(payload.end(), server.value.begin(), server.value.end());
        peerSendAcl(peripheral, server.handle, chatL2cap(payload));
    }
}

void peripheralChatPump(Endpoint& peripheral, ChatAttServer& server) {
    Bytes frame;
    while (popFrame(peripheral, frame)) {
        if (frame.size() >= 7 && frame[0] == 0x04 && frame[1] == 0x3E &&
            (frame[3] == 0x01 || frame[3] == 0x0A) && frame[4] == 0x00) {
            server.handle = chatU16(frame, 5);
            continue;
        }
        if (frame.size() < 5 || frame[0] != 0x02) continue;
        const uint16_t flags = chatU16(frame, 1);
        const uint16_t payloadLen = chatU16(frame, 3);
        if (frame.size() != size_t(5 + payloadLen)) continue;
        const uint8_t pb = (flags >> 12) & 0x03;
        const Bytes payload(frame.begin() + 5, frame.end());
        if (pb == 2 || pb == 0) {
            if (payload.size() < 4) continue;
            const uint16_t l2len = chatU16(payload, 0);
            if (chatU16(payload, 2) != 0x0004) continue;
            server.expected = 4 + l2len;
            server.reassembly = payload;
        } else if (pb == 1) {
            if (server.expected <= 0) continue;
            server.reassembly.insert(server.reassembly.end(),
                                     payload.begin(), payload.end());
        } else {
            continue;
        }
        if (server.expected > 0 &&
            int(server.reassembly.size()) >= server.expected) {
            const uint16_t l2len = chatU16(server.reassembly, 0);
            const Bytes att(server.reassembly.begin() + 4,
                            server.reassembly.begin() + 4 + l2len);
            server.reassembly.clear();
            server.expected = 0;
            if (server.handle != 0) peerAnswerAtt(peripheral, server, att);
        }
    }
}

void testChatClientRoundTrip() {
    Endpoint peripheral("test-chat-peripheral");
    configureEvents(peripheral);
    enableAdvertising(peripheral);

    BleChatClient client;
    QStringList chatLog;
    QObject::connect(&client, &BleChatClient::statusMessage,
                     [&](QString text) { chatLog.append("STATUS: " + text); });
    QObject::connect(&client, &BleChatClient::errorMessage,
                     [&](QString text) { chatLog.append("ERROR: " + text); });
    bool connected = false;
    bool ready = false;
    Bytes readValue;
    bool gotRead = false;
    Bytes notified;
    bool gotNotify = false;
    bool writeOk = false;
    bool writeFailed = false;
    QObject::connect(&client, &BleChatClient::connectionChanged,
                     [&]() { connected = client.connected(); });
    QObject::connect(&client, &BleChatClient::discoveryChanged,
                     [&]() { ready = client.ready(); });
    QObject::connect(&client, &BleChatClient::valueRead,
                     [&](QByteArray data) {
                         gotRead = true;
                         readValue.assign(data.begin(), data.end());
                     });
    QObject::connect(&client, &BleChatClient::notification,
                     [&](QByteArray data) {
                         gotNotify = true;
                         notified.assign(data.begin(), data.end());
                     });
    QObject::connect(&client, &BleChatClient::writeDone,
                     [&](bool ok, QString) { writeOk = ok; if (!ok) writeFailed = true; });

    client.start();
    client.startScan();
    for (int i = 0; i < 200 && client.devices().isEmpty(); ++i) client.poll();
    if (client.devices().isEmpty()) {
        for (const QString& line : chatLog) std::fprintf(stderr, "DEBUG %s\n", line.toUtf8().constData());
    }
    require(!client.devices().isEmpty(), "chat scan found no peripheral");
    client.connectToDevice(0);

    ChatAttServer server;
    int i = 0;
    for (; i < 2000 && !client.ready(); ++i) {
        client.poll();
        peripheralChatPump(peripheral, server);
        if (!client.connecting() && !client.connected()) break;
    }
    require(client.connected(), "chat client did not connect");
    require(client.ready(), "chat client did not finish discovery");

    QByteArray out;
    out.append(char(0x19));
    client.writeValue(out);
    for (i = 0; i < 500 && !(writeOk && gotNotify); ++i) {
        client.poll();
        peripheralChatPump(peripheral, server);
    }
    require(writeOk, "chat write was not acknowledged");
    require(gotNotify && notified == Bytes{0x19}, "chat notification mismatch");

    client.readValue();
    for (i = 0; i < 500 && !gotRead; ++i) {
        client.poll();
        peripheralChatPump(peripheral, server);
    }
    require(gotRead && readValue == Bytes{0x19}, "chat readback mismatch");

    require(client.serviceCount() == 2, "chat did not enumerate both services");
    client.selectTarget(0, 0);
    for (i = 0; i < 500 && client.selectedService() != 0; ++i) {
        client.poll();
        peripheralChatPump(peripheral, server);
    }
    require(client.selectedService() == 0, "chat did not switch service");
    writeFailed = false;
    out.clear();
    out.append(char(0x55));
    client.writeValue(out);
    for (i = 0; i < 500 && !writeFailed; ++i) {
        client.poll();
        peripheralChatPump(peripheral, server);
    }
    require(writeFailed, "read-only characteristic accepted a write");

    client.selectTarget(1, 0);
    for (i = 0; i < 500 && (client.selectedService() != 1 || !client.notificationsOn()); ++i) {
        client.poll();
        peripheralChatPump(peripheral, server);
    }
    require(client.selectedService() == 1 && client.notificationsOn(),
            "chat did not switch back");
    writeOk = false;
    gotNotify = false;
    gotRead = false;
    out.clear();
    out.append(char(0x2A));
    client.writeValue(out);
    for (i = 0; i < 500 && !(writeOk && gotNotify); ++i) {
        client.poll();
        peripheralChatPump(peripheral, server);
    }
    if (!(writeOk && notified == Bytes{0x2A})) {
        for (const QString& line : chatLog) std::fprintf(stderr, "DEBUG %s\n", line.toUtf8().constData());
        std::fprintf(stderr, "DEBUG writeOk=%d gotNotify=%d notified=%zu ready=%d sel=%d attr=%s\n",
                     int(writeOk), int(gotNotify), notified.size(), int(client.ready()),
                     client.selectedService(),
                     client.attributeText().toUtf8().constData());
    }
    require(writeOk && notified == Bytes{0x2A}, "chat notification mismatch after switch");
    client.readValue();
    for (i = 0; i < 500 && !gotRead; ++i) {
        client.poll();
        peripheralChatPump(peripheral, server);
    }
    require(gotRead && readValue == Bytes{0x2A}, "chat readback mismatch after switch");

    client.disconnect();
    for (i = 0; i < 200 && client.connected(); ++i) {
        client.poll();
        peripheralChatPump(peripheral, server);
    }
    if (client.connected()) {
        for (const QString& line : chatLog) std::fprintf(stderr, "DEBUG %s\n", line.toUtf8().constData());
        std::fprintf(stderr, "DEBUG conn=%d connecting=%d ready=%d attr=%s\n",
                     int(client.connected()), int(client.connecting()), int(client.ready()),
                     client.attributeText().toUtf8().constData());
    }
    require(!client.connected(), "chat client did not disconnect");
    client.stop();
}

void testPeerDestruction() {
    Endpoint central("runtime-lifetime-central");
    uint16_t handle = 0;
    {
        std::unique_ptr<Endpoint> peripheral(new Endpoint("runtime-lifetime-peripheral"));
        handle = connect(central, *peripheral, false).centralHandle;
    }
    const std::vector<Bytes> frames = drain(central);
    require(frames.size() == 1 && frames[0].size() == 7 &&
            frames[0][0] == 0x04 && frames[0][1] == 0x05 && frames[0][3] == 0x00 &&
            (frames[0][4] | (uint16_t(frames[0][5]) << 8)) == handle &&
            frames[0][6] == 0x16,
            "peer destruction did not produce Disconnection Complete");
}

} // namespace

int main() {
    try {
        testConnectionAndAclCredits();
        testRxBackpressure();
        testPeerDestruction();
        testChatFormat();
        testChatClientRoundTrip();
    } catch (const std::exception& error) {
        std::cerr << "QemuBt runtime test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "QemuBt runtime tests passed\n";
    return 0;
}
