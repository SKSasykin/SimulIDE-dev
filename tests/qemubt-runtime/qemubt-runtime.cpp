#include "qemubt.h"

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
    } catch (const std::exception& error) {
        std::cerr << "QemuBt runtime test failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "QemuBt runtime tests passed\n";
    return 0;
}
