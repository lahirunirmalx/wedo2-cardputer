# Grab serial output from a port that keeps re-enumerating. DTR and RTS are
# left asserted so that opening the port does not itself reset the chip.
import sys, time, serial
port, secs = sys.argv[1], float(sys.argv[2])
end = time.time() + secs
buf = b""
opens = 0
while time.time() < end:
    try:
        s = serial.Serial()
        s.port = port; s.baudrate = 115200; s.timeout = 0.05
        s.dtr = True; s.rts = True
        s.open(); opens += 1
        while time.time() < end:
            buf += s.read(4096)
    except Exception:
        time.sleep(0.02)
    finally:
        try: s.close()
        except Exception: pass
sys.stdout.write("opens=%d bytes=%d\n" % (opens, len(buf)))
sys.stdout.write(buf.decode("utf-8", "replace"))
