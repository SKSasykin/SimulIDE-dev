# ESP virtual WiFi and Bluetooth support

This document describes the wireless networking facilities exposed by the
ESP devices in SimulIDE. Virtual WiFi is a packet-level integration between
guest firmware, the QEMU fork and libslirp. It is not an RF or 802.11 MAC/PHY
simulation. Bluetooth transport scaffolding exists internally, but Bluetooth
is not currently a supported user-facing feature.

## Support matrix

| Device | Virtual WiFi backend | Bundled HTTP demo | Silicon Bluetooth | Bluetooth in SimulIDE |
| --- | --- | --- | --- | --- |
| ESP32 | SLC DMA NIC with libslirp DHCP/NAT | Yes | Classic + BLE | Not supported; transport scaffolding only |
| ESP32-S3 | SLC DMA NIC with libslirp DHCP/NAT | Yes | BLE | Not supported; transport scaffolding only |
| ESP32-C3 | SLC DMA NIC with libslirp DHCP/NAT | Yes | BLE | Not supported |
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
- a `QemuBt` host module that can move opaque packets through an experimental
  UDP backend;
- ESP32 and ESP32-S3 bridge ranges reserved for future controller transport.

The current tree does not have:

- an emulated ESP Bluetooth controller;
- a defined and tested HCI transport used by guest firmware;
- advertising, scanning, connections, ACL/ISO scheduling or a virtual radio;
- GATT inspection or interaction in the SimulIDE UI;
- Bluetooth adapter passthrough through CoreBluetooth, BlueZ or WinRT.

For this reason, the former experimental `WiFiLinkPort` and `BtLinkPort`
settings are not exposed in the Properties panel. They must not be interpreted
as working Bluetooth support.

A practical future implementation should start with BLE and NimBLE over a
defined HCI transport. A SimulIDE Bluetooth monitor could then act as a virtual
central, display advertisements and GATT services, and perform characteristic
read, write and notification operations. A deterministic virtual radio shared
by simulated ESP devices should precede optional host-adapter passthrough.

## Relevant implementation files

- `src/microsim/cores/qemu/qemudevice.{h,cpp}`: shared arena and QEMU launch
  options.
- `src/microsim/cores/qemu/qemuwifi.{h,cpp}`: SimulIDE WiFi ring module.
- `src/microsim/cores/qemu/qemubt.{h,cpp}`: experimental Bluetooth transport
  scaffolding.
- `third_party/qemu-simulide/hw/dma/esp32_slc.c`: SLC DMA virtual NIC.
- `third_party/qemu-simulide/hw/misc/esp32-simulide-bridge.c`: shared-memory
  bridge maps.
- `third_party/qemu-simulide/net/slirp.c`: user-mode networking and host
  forwarding.
