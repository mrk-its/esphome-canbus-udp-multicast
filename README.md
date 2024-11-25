# ESPHome Canbus UDP Multicast
Implementation of virtual [udp_multicast](https://python-can.readthedocs.io/en/stable/interfaces/udp_multicast.html) CAN interface on ESPHome device

## Configuration variables:
* multicast_ip (Optional, string): ipv4 destination multicast address, defaults to 232.10.11.12 (default multicast ipv4 address of `udp_multicast` python-can interface)
* multicast_port (Optional, int): ipv4 destination multicast port, defaults to 43113 (default port of `udp_multicast` python-can interface)
* canbus_id (Optional, string): optional id of physical canbus component where packets will be send to / read from.

Example esphome config ([full file](examples/udp-multicast-can-interface.yaml))

```
# ...

external_components:
  - source: github://mrk-its/esphome-canbus-udp-multicast

canbus:
  - platform: esp32_can
    id: can_bus
    rx_pin: GPIO5
    tx_pin: GPIO4
    can_id: 0
    bit_rate: 125kbps

  - platform: canbus_udp_multicast
    id: udp_multicast
    can_id: 0
    canbus_id: can_bus             # refers to above esp32_can component
    multicast_ip: 232.10.11.12

# ...
```

## Example

Build and flash example esphome config:
```
pip install esphome
esphome run examples/udp-multicast-can-interface.yaml
```

Install python-can and run can.viewer session:

```
pip install "python-can[viewer]"
python -m can.viewer -i udp_multicast -c 232.10.11.12

```
