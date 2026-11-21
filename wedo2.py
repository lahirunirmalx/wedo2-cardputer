#!/usr/bin/env python3
"""LEGO WeDo 2.0 Smart Hub control over Bluetooth LE.

Protocol taken from scratch-vm (src/extensions/scratch3_wedo2/index.js).

  wedo2.py scan
  wedo2.py led ff0000
  wedo2.py tone 440 500
  wedo2.py motor 1 60 2        port 1, power 60, 2 seconds (negative = reverse)
  wedo2.py monitor             print attach events and sensor values

Press the green button on the hub first so it advertises.
"""
import argparse
import asyncio
import sys

from bleak import BleakClient, BleakScanner

BASE = "-1212-efde-1523-785feabcd123"
DEVICE_SERVICE = "00001523" + BASE
ATTACHED_IO = "00001527" + BASE
LOW_VOLTAGE_ALERT = "00001528" + BASE
INPUT_VALUES = "00001560" + BASE
INPUT_COMMAND = "00001563" + BASE
OUTPUT_COMMAND = "00001565" + BASE

TYPE_MOTOR = 1
TYPE_PIEZO = 22
TYPE_LED = 23
TYPE_TILT = 34
TYPE_DISTANCE = 35
TYPE_NAMES = {
    20: "voltage",
    21: "current",
    TYPE_MOTOR: "motor",
    TYPE_PIEZO: "piezo",
    TYPE_LED: "led",
    TYPE_TILT: "tilt",
    TYPE_DISTANCE: "distance",
}

PORT_PIEZO = 5
PORT_LED = 6

CMD_MOTOR_POWER = 1
CMD_PLAY_TONE = 2
CMD_STOP_TONE = 3
CMD_WRITE_RGB = 4

MOTOR_BRAKE = 127
SCAN_SECONDS = 6.0
PORT_REPORT_SECONDS = 1.5
KEEPALIVE_SECONDS = 5.0


def output_command(port, command, values=None):
    data = [port, command]
    if values is not None:
        data.append(len(values))
        data.extend(values)
    return bytes(v & 0xFF for v in data)


def input_command(port, dev_type, mode, delta, unit, notify):
    return bytes([1, 2, port, dev_type, mode, delta, 0, 0, 0, unit, 1 if notify else 0])


def motor_byte(power):
    power = max(-100, min(100, int(power)))
    if power == 0:
        return 0
    scaled = round(30 + 70 * abs(power) / 100)
    return scaled if power > 0 else -scaled


async def find_hub(address):
    if address:
        device = await BleakScanner.find_device_by_address(address, timeout=SCAN_SECONDS)
    else:
        device = await BleakScanner.find_device_by_filter(
            lambda d, adv: DEVICE_SERVICE in [u.lower() for u in adv.service_uuids],
            timeout=SCAN_SECONDS,
        )
    if device is None:
        sys.exit("No WeDo 2.0 hub found. Press the green button on the hub, then retry.")
    return device


class Hub:
    def __init__(self, client, verbose=False):
        self.client = client
        self.verbose = verbose
        self.ports = {}

    async def start(self):
        await self.client.start_notify(ATTACHED_IO, self._on_attached)
        await self.client.start_notify(INPUT_VALUES, self._on_value)
        await self.client.write_gatt_char(
            INPUT_COMMAND, input_command(PORT_LED, TYPE_LED, 1, 0, 0, False)
        )
        await asyncio.sleep(PORT_REPORT_SECONDS)

    def _on_attached(self, _char, data):
        if len(data) < 2:
            return
        port = data[0]
        if data[1] == 0:
            self.ports.pop(port, None)
            if self.verbose:
                print("port %d: detached" % port, flush=True)
            return
        if len(data) < 4:
            return
        dev_type = data[3]
        self.ports[port] = dev_type
        if self.verbose:
            print("port %d: %s" % (port, TYPE_NAMES.get(dev_type, "type %d" % dev_type)), flush=True)
        if dev_type == TYPE_TILT:
            asyncio.ensure_future(self._enable_sensor(port, dev_type, 0, 0))
        elif dev_type == TYPE_DISTANCE:
            asyncio.ensure_future(self._enable_sensor(port, dev_type, 0, 1))

    async def _enable_sensor(self, port, dev_type, mode, unit):
        await self.client.write_gatt_char(
            INPUT_COMMAND, input_command(port, dev_type, mode, 1, unit, True)
        )

    def _on_value(self, _char, data):
        if not self.verbose or len(data) < 3:
            return
        port = data[1]
        dev_type = self.ports.get(port)
        if dev_type == TYPE_DISTANCE:
            print("port %d distance: %d" % (port, data[2]), flush=True)
        elif dev_type == TYPE_TILT and len(data) >= 4:
            x = int.from_bytes(data[2:3], "little", signed=True)
            y = int.from_bytes(data[3:4], "little", signed=True)
            print("port %d tilt: x=%d y=%d" % (port, x, y), flush=True)

    async def led(self, r, g, b):
        await self.client.write_gatt_char(
            OUTPUT_COMMAND, output_command(PORT_LED, CMD_WRITE_RGB, [r, g, b])
        )

    async def tone(self, hz, ms):
        await self.client.write_gatt_char(
            OUTPUT_COMMAND,
            output_command(PORT_PIEZO, CMD_PLAY_TONE, [hz, hz >> 8, ms, ms >> 8]),
        )

    async def motor(self, port, value):
        await self.client.write_gatt_char(
            OUTPUT_COMMAND, output_command(port, CMD_MOTOR_POWER, [value])
        )

    def ports_text(self):
        if not self.ports:
            return "none reported"
        return ", ".join(
            "%d=%s" % (p, TYPE_NAMES.get(t, "type %d" % t)) for p, t in sorted(self.ports.items())
        )


async def cmd_scan(_args):
    found = await BleakScanner.discover(
        timeout=SCAN_SECONDS, service_uuids=[DEVICE_SERVICE], return_adv=True
    )
    hubs = [
        (d, adv) for d, adv in found.values()
        if DEVICE_SERVICE in [u.lower() for u in adv.service_uuids]
    ]
    if not hubs:
        sys.exit("No WeDo 2.0 hub found. Press the green button on the hub, then retry.")
    for device, adv in hubs:
        print("%s  %s  rssi %s" % (device.address, device.name or "(no name)", adv.rssi))


async def with_hub(args, action, verbose=False):
    device = await find_hub(args.addr)
    async with BleakClient(device) as client:
        hub = Hub(client, verbose=verbose)
        await hub.start()
        await action(hub)


async def cmd_led(args):
    color = args.color.lstrip("#")
    if len(color) != 6:
        sys.exit("Color must be 6 hex digits, e.g. ff0000")
    try:
        rgb = bytes.fromhex(color)
    except ValueError:
        sys.exit("Color must be 6 hex digits, e.g. ff0000")

    async def action(hub):
        await hub.led(rgb[0], rgb[1], rgb[2])
        print("LED set to #%s" % color)
        await asyncio.sleep(args.hold)

    await with_hub(args, action)


async def cmd_tone(args):
    if not 20 <= args.hz <= 20000:
        sys.exit("Frequency must be 20 to 20000 Hz")
    if not 1 <= args.ms <= 60000:
        sys.exit("Duration must be 1 to 60000 ms")

    async def action(hub):
        await hub.tone(args.hz, args.ms)
        print("Tone %d Hz for %d ms" % (args.hz, args.ms))
        await asyncio.sleep(args.ms / 1000 + 0.2)

    await with_hub(args, action)


async def cmd_motor(args):
    if args.port not in (1, 2):
        sys.exit("Port must be 1 or 2")
    if not 0 < args.seconds <= 60:
        sys.exit("Seconds must be above 0 and at most 60")

    async def action(hub):
        if hub.ports.get(args.port) != TYPE_MOTOR:
            sys.exit("No motor on port %d. Ports: %s" % (args.port, hub.ports_text()))
        try:
            await hub.motor(args.port, motor_byte(args.power))
            print("Motor on port %d at power %d for %.1f s" % (args.port, args.power, args.seconds))
            await asyncio.sleep(args.seconds)
        finally:
            await hub.motor(args.port, 0)
            print("Motor stopped")

    await with_hub(args, action)


async def cmd_monitor(args):
    async def action(hub):
        print("Ports: %s. Ctrl-C to quit." % hub.ports_text(), flush=True)
        while hub.client.is_connected:
            await asyncio.sleep(KEEPALIVE_SECONDS)
            await hub.client.read_gatt_char(LOW_VOLTAGE_ALERT)
        print("Hub disconnected")

    await with_hub(args, action, verbose=True)


def main():
    parser = argparse.ArgumentParser(description="LEGO WeDo 2.0 Smart Hub control over BLE")
    parser.add_argument("--addr", help="hub Bluetooth address (default: first hub found)")
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("scan", help="list hubs that are advertising").set_defaults(func=cmd_scan)

    p = sub.add_parser("led", help="set hub LED color")
    p.add_argument("color", help="hex color, e.g. ff0000")
    p.add_argument("--hold", type=float, default=3.0, help="seconds to stay connected")
    p.set_defaults(func=cmd_led)

    p = sub.add_parser("tone", help="play a tone on the hub buzzer")
    p.add_argument("hz", type=int)
    p.add_argument("ms", type=int)
    p.set_defaults(func=cmd_tone)

    p = sub.add_parser("motor", help="run a motor, then stop it")
    p.add_argument("port", type=int)
    p.add_argument("power", type=int, help="-100 to 100")
    p.add_argument("seconds", type=float)
    p.set_defaults(func=cmd_motor)

    sub.add_parser("monitor", help="print port and sensor events").set_defaults(func=cmd_monitor)

    args = parser.parse_args()
    try:
        asyncio.run(args.func(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
