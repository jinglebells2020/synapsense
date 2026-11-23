"""Connects to the SynapSense watch over Bluetooth, like nRF Connect would.

Prints every characteristic, subscribes to Alert/Status/Live, and sends a
command. Needs: pip install bleak

  python tools/ble_check.py            # listen for 10 s, send "test"
  python tools/ble_check.py --listen 120 --cmd none
"""
import argparse
import asyncio

from bleak import BleakClient, BleakScanner

BASE = "7a3e{:04x}-5c1f-4d2b-9a61-53594e415053"
SVC, ALERT, STATUS, LIVE, CMD = (BASE.format(i) for i in range(1, 6))
NAMES = {ALERT: "ALERT", STATUS: "STATUS", LIVE: "LIVE"}


async def main(args):
    print("scanning for SynapSense...")
    dev = await BleakScanner.find_device_by_name("SynapSense", timeout=15)
    if not dev:
        print("not found")
        return
    print("found", dev.address)
    async with BleakClient(dev) as c:
        print("connected, mtu", c.mtu_size)
        for s in c.services:
            print("service", s.uuid, s.description)
            for ch in s.characteristics:
                line = f"   {ch.uuid} {','.join(ch.properties)}"
                for d in ch.descriptors:
                    if d.uuid.startswith("00002901"):
                        line += f"  '{(await c.read_gatt_descriptor(d.handle)).decode()}'"
                if "read" in ch.properties:
                    v = await c.read_gatt_char(ch)
                    line += f"  = {v.decode(errors='replace') if ch.uuid.startswith('7a3e') else v.hex()}"
                print(line)

        def on_notify(ch, data):
            print(f"<- {NAMES.get(ch.uuid, ch.uuid)}: {data.decode(errors='replace')}")

        for u in (ALERT, STATUS, LIVE):
            await c.start_notify(u, on_notify)
        if args.cmd != "none":
            print(f"-> command '{args.cmd}'")
            await c.write_gatt_char(CMD, args.cmd.encode(), response=True)
        await asyncio.sleep(args.listen)


if __name__ == "__main__":
    p = argparse.ArgumentParser()
    p.add_argument("--listen", type=float, default=10)
    p.add_argument("--cmd", default="test")
    asyncio.run(main(p.parse_args()))
