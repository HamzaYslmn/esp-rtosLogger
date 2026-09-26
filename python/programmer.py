# /// script
# dependencies = ["pyserial", "bleak"]
# ///
"""Flash an example and watch it, or watch one over the cable or BLE: `uv run python/programmer.py`."""
import asyncio
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path

import serial
from bleak import BleakClient, BleakScanner

EXAMPLES = Path(__file__).parent.parent / "examples"
BAUD = 115200
SERVICE = "9b39be61-f76f-4549-8a18-3ff0c3a21191"  # SerialAndBle's
OUT = "2693aea1-fbe5-4526-9016-fdb6ff47899c"  # its notify
BOARDS = {  # the ones the examples were run on; "other" takes any fqbn
    "ESP32-S3, USB port, hardware CDC": "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc",
    # TinyUSB ignores esptool's reset lines: UploadMode=cdc reboots it with a 1200 baud touch
    "ESP32-S3, USB port, TinyUSB": "esp32:esp32:esp32s3:USBMode=default,CDCOnBoot=cdc,UploadMode=cdc",
    "ESP32, UART port": "esp32:esp32:esp32",
    "nRF52840 ProMicro": "Seeeduino:nrf52:promicronRF52840:softdevice=s140v6",
}


def pick(kind, items):
    print(f"\n{kind}")
    for i, item in enumerate(items, 1):
        print(f"  {i:>2}  {item}")
    if len(items) == 1:
        return items[0]
    while True:
        answer = input("> ").strip()
        if answer.isdigit() and 0 < int(answer) <= len(items):
            return items[int(answer) - 1]


def ports(fqbn=""):
    """Ports of the fqbn's vendor:arch, or every port when none match."""
    out = subprocess.run(["arduino-cli", "board", "list", "--format", "json"],
                         capture_output=True, text=True, check=True).stdout
    found = [(p["port"]["address"], ((p.get("matching_boards") or [{}])[0]).get("fqbn", ""))
             for p in json.loads(out).get("detected_ports", [])]
    arch = ":".join(fqbn.split(":")[:2])
    return [a for a, f in found if arch and f.startswith(arch)] or [a for a, _ in found]


def settle(was, fqbn, seconds=10):
    """An S3 comes back from a flash as another USB device, on another COM."""
    end = time.time() + seconds
    while time.time() < end:
        found = ports(fqbn)
        if found:
            return was if was in found else found[0]
        time.sleep(0.5)
    return was


def monitor(port):
    for _ in range(25):  # Windows holds a CDC port for a moment after a flash
        try:
            line = serial.Serial(port, BAUD, timeout=0.1)
            break
        except serial.SerialException:
            time.sleep(0.2)
    else:
        sys.exit(f"cannot open {port}")
    print(f"\n{port} at {BAUD}, ctrl+c to leave\n")

    def pump():
        while line.is_open:
            try:
                data = line.read(line.in_waiting or 1)
            except Exception:
                return  # the board rebooted or we are leaving
            sys.stdout.write(data.decode(errors="replace"))
            sys.stdout.flush()

    threading.Thread(target=pump, daemon=True).start()
    try:
        for typed in sys.stdin:
            line.write(typed.encode())
    finally:
        line.close()


def show(data):
    """A packet of lines prints as is; one of structs, its first byte never '[', as hex."""
    text = data.decode(errors="replace") if data[:1] == b"[" else f"struct packet: {data.hex(' ')}\n"
    sys.stdout.write(text)
    sys.stdout.flush()


async def ble():
    print("\nscanning...")
    found = await BleakScanner.discover(5, service_uuids=[SERVICE])
    if not found:
        sys.exit("no board advertising the SerialAndBle service")
    names = [f"{d.name}  {d.address}" for d in found]
    board = found[names.index(pick("board", names))]
    async with BleakClient(board) as link:
        print(f"\n{board.name}, mtu {link.mtu_size}, ctrl+c to leave\n")
        await link.start_notify(OUT, lambda _, data: show(bytes(data)))
        while link.is_connected:
            await asyncio.sleep(0.5)


def main():
    os.system("")  # Windows: lets the board's escapes through
    todo = pick("do", ["flash an example", "serial monitor", "ble monitor"])
    if todo == "serial monitor":
        return monitor(pick("port", ports()))
    if todo == "ble monitor":
        return asyncio.run(ble())
    sketch = EXAMPLES / pick("example", sorted(p.name for p in EXAMPLES.iterdir() if p.is_dir()))
    board = pick("board", [*BOARDS, "other"])
    fqbn = BOARDS.get(board) or input("fqbn> ").strip()
    port = pick("port", ports(fqbn))
    print(f"\n{sketch.name} -> {port}\n")
    if subprocess.run(["arduino-cli", "compile", "-u", "-b", fqbn, "-p", port, str(sketch)]).returncode:
        sys.exit(1)  # arduino-cli said why
    monitor(settle(port, fqbn))


if __name__ == "__main__":
    try:
        main()
    except (KeyboardInterrupt, EOFError):
        pass
