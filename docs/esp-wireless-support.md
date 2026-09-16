# ESP virtual WiFi and Bluetooth support

This document describes the wireless networking facilities exposed by the
ESP devices in SimulIDE. Virtual WiFi is a packet-level integration between
guest firmware, the QEMU fork and libslirp. It is not an RF or 802.11 MAC/PHY
simulation. A development BLE HCI transport, NimBLE startup command profile,
legacy undirected advertising/scanning, deterministic connections and opaque
ACL forwarding are implemented, but
Bluetooth is not currently a supported user-facing feature.

## Support matrix

| Device | Virtual WiFi backend | Bundled HTTP demo | Silicon Bluetooth | Bluetooth in SimulIDE |
| --- | --- | --- | --- | --- |
| ESP32 | SLC DMA NIC with libslirp DHCP/NAT | Yes | Classic + BLE | Development HCI transport, deterministic LE links and opaque ACL forwarding |
| ESP32-S3 | SLC DMA NIC with libslirp DHCP/NAT | Yes | BLE | Development HCI transport, deterministic LE links and opaque ACL forwarding |
| ESP32-C3 | SLC DMA NIC with libslirp DHCP/NAT | Yes | BLE | Development HCI transport, deterministic LE links and opaque ACL forwarding |
| ESP8266EX | Virtual SLC NIC available | No guest demo yet | None | Not applicable |

## Virtual WiFi architecture

The working packet path is:

```text
ESP application
  -> custom ESP-NETIF driver
  -> ESP SLC DMA descriptors
  -> QEMU esp32.slc virtual NIC
  -> QEMU libslirp backend
  -> host network
```

The reverse path follows the same layers in the opposite direction. The guest
receives ordinary Ethernet frames and uses its normal lwIP stack for ARP, DHCP,
IP, TCP and UDP. libslirp provides a private user-mode network, DHCP and NAT;
it does not require a TAP interface or administrator privileges.

The QEMU SLC model copies TX and RX frames through the guest DMA descriptor
rings. Backend RX is queued and delivered asynchronously on a host-clock timer
so responses cannot race guest ESP-NETIF link initialization. The SimulIDE
shared-memory arena also contains WiFi packet rings for future simulator-to-
simulator links, but the supported Internet backend is currently libslirp.

## Firmware requirements

The bundled demos use a custom ESP-NETIF transport that presents the emulated
SLC controller as an Ethernet-like interface. Firmware must include this
transport to use virtual WiFi.

The implementation does not emulate an 802.11 radio, access points, channels,
association, WPA, or Espressif's closed WiFi hardware controller. Consequently,
an arbitrary stock binary using `WiFi.begin()` or the native Espressif WiFi
driver will not automatically gain networking merely because the QEMU NIC is
present.

Bundled end-to-end examples:

- `resources/data/examples/esp32/esp32 WiFi HTTP Hello World.sim2`
- `resources/data/examples/esp32-s3/esp32-s3 WiFi HTTP Hello World.sim2`
- `resources/data/examples/esp32-c3/esp32-c3 WiFi HTTP Hello World.sim2`

Their merged firmware images live under the corresponding device directory in
`resources/data/bin/`.

## Host forwarding

`HostForwardPort` controls access from the host to an HTTP server in the guest:

- `0` disables forwarding and is the default.
- A positive value forwards that TCP port on all host IPv4 interfaces to guest
  TCP port 80. It is therefore also reachable through `127.0.0.1`.
- The bundled HTTP examples set `HostForwardPort="8080"` and are available at
  `http://127.0.0.1:8080/` after the guest obtains its DHCP address.
- The selected host port must be free. Multiple simulations must use different
  forwarding ports.

Because the current forwarding rule binds to all host IPv4 interfaces, do not
expose untrusted guest services while connected to an untrusted network. A
future host-address property may allow restricting the listener explicitly to
loopback.

Normal ESP examples leave forwarding disabled, do not reserve port 8080 and
can run in parallel. Outbound guest connections use libslirp NAT independently
of `HostForwardPort`.

## Build requirements

Virtual WiFi requires libslirp 4.0 or newer when building the QEMU fork. The
SimulIDE host integration also requires Qt5Network. `scripts/build_qemu.sh`
checks for libslirp and configures QEMU with SLIRP enabled.

## Bluetooth status

Bluetooth Classic and BLE are not implemented end-to-end. The current tree has:

- `bt_tx` and `bt_rx` rings in the SimulIDE/QEMU shared-memory arena;
- a descriptor-based H4 transport at `0x3ff52000` on ESP32 and `0x60012000`
  on ESP32-S3/C3, with level interrupts and asynchronous RX delivery;
- a `QemuBt` controller that implements the full HCI command set required for
  ESP-IDF 4.4.7 NimBLE host synchronization: Reset, Read Local Version Info,
  Read Local Supported Features, Set Event Mask, Set Event Mask Page 2,
  LE Set Event Mask, LE Read Buffer Size, LE Read Local Supported Features,
  Read BD_ADDR, Host Buffer Size, Set Controller To Host Flow Control
  (ESP32), LE Set Address Resolution Enable, LE Clear Resolving List,
  LE Add Device To Resolving List, LE Set Privacy Mode (ESP32-S3/C3);
- legacy undirected LE Set Advertising Parameters/Data/Scan Response/Enable
  and LE Set Scan Parameters/Enable commands, with strict parameter validation;
- an in-process deterministic medium shared by `QemuBt` instances. An enabled
  advertiser publishes a snapshot; enabled passive scanners receive one LE
  Advertising Report, while active scanners also receive a Scan Response.
  Duplicate filtering and RX-ring backpressure are preserved;
- one deterministic LE connection per `QemuBt`, with globally unique local
  handles, legacy/enhanced connection-complete events, cancellation,
  disconnection and remote-feature completion;
- opaque H4 ACL forwarding between connected controllers. ACL fragments are
  not interpreted; PB flags and peer-local handles are translated, bounded
  queues apply backpressure, and Number Of Completed Packets plus configured
  controller-to-host credits provide flow control;
- source for a Reset transport smoke-test firmware under
  `resources/data/bin/esp/examples/ble-hci-reset/`.

The controller object is compiled directly and its HCI framing is covered by
exact byte-vector and source-contract tests. There is not yet a standalone C++
runtime harness for multi-controller scenarios because `QemuBt` is coupled to
the full `QemuDevice` shared-memory lifecycle.

The current tree does not have:

- ISO data, controller-side ATT/GATT, SMP or encryption. Applications may run
  host-side L2CAP/ATT over forwarded ACL data, but the development controller
  does not inspect or implement those protocols;
- interval scheduling, channels, propagation, interference or collisions. The
  current medium is activation-driven rather than a timed RF simulation;
- GATT inspection or interaction in the SimulIDE UI;
- Bluetooth adapter passthrough through CoreBluetooth, BlueZ or WinRT.

For this reason, the former experimental `WiFiLinkPort` and `BtLinkPort`
settings are not exposed in the Properties panel. They must not be interpreted
as working Bluetooth support.

GATT inspection in the SimulIDE UI, security procedures, timed RF behavior and
host-adapter passthrough remain later stages.

## Relevant implementation files

- `src/microsim/cores/qemu/qemudevice.{h,cpp}`: shared arena and QEMU launch
  options.
- `src/microsim/cores/qemu/qemuwifi.{h,cpp}`: SimulIDE WiFi ring module.
- `src/microsim/cores/qemu/qemubt.{h,cpp}`: minimal HCI controller.
- `third_party/qemu-simulide/hw/misc/esp32_ble_hci.c`: guest DMA/H4 transport.
- `third_party/qemu-simulide/hw/dma/esp32_slc.c`: SLC DMA virtual NIC.
- `third_party/qemu-simulide/hw/misc/esp32-simulide-bridge.c`: shared-memory
  bridge maps.
- `third_party/qemu-simulide/net/slirp.c`: user-mode networking and host
  forwarding.
