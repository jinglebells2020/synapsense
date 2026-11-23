"""USB serial monitor / recorder for SynapSense.

  python tools/monitor.py                       # live features + events
  python tools/monitor.py --raw --seconds 60 --out recordings/walking.csv
      records raw 52 Hz IMU data (ms,ax,ay,az,gx,gy,gz) for tuning

Reconnects by itself if USB drops (the XIAO's connector is easy to jiggle).
Needs: pip install pyserial
"""
import argparse
import glob
import sys
import time

import serial


def open_port(port_arg, mode):
    while True:
        ports = [port_arg] if port_arg else sorted(glob.glob("/dev/cu.usbmodem*"))
        for p in ports:
            try:
                s = serial.Serial(p, 115200, timeout=0.5)
                time.sleep(0.3)
                s.reset_input_buffer()
                s.write(f"{mode} on\n".encode())
                return s
            except (serial.SerialException, OSError):
                pass
        time.sleep(0.5)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--port", default=None)
    p.add_argument("--seconds", type=float, default=0, help="0 = until Ctrl-C")
    p.add_argument("--raw", action="store_true", help="stream raw samples instead of features")
    p.add_argument("--out", default=None, help="also write lines to this file")
    a = p.parse_args()

    if not a.port and not glob.glob("/dev/cu.usbmodem*"):
        sys.exit("no XIAO found (/dev/cu.usbmodem*)")
    mode = "stream" if a.raw else "feat"
    out = open(a.out, "w") if a.out else None
    t0 = time.time()
    s = open_port(a.port, mode)
    last_rx = time.time()
    try:
        while not a.seconds or time.time() - t0 < a.seconds:
            try:
                line = s.readline().decode(errors="replace").rstrip()
            except (serial.SerialException, OSError):
                line, last_rx = "", 0
            if line:
                last_rx = time.time()
                print(f"{time.time() - t0:7.1f}s {line}", flush=True)
                if out and (not a.raw or line[:1].isdigit()):
                    out.write(line + "\n")
                    out.flush()
            elif time.time() - last_rx > 3:
                print(f"{time.time() - t0:7.1f}s # no data for 3 s, reconnecting", flush=True)
                try:
                    s.close()
                except Exception:
                    pass
                s = open_port(a.port, mode)
                last_rx = time.time()
    except KeyboardInterrupt:
        pass
    finally:
        try:
            s.write(f"{mode} off\n".encode())
        except Exception:
            pass
        if out:
            out.close()


if __name__ == "__main__":
    main()
