import sys, time
import serial

port = sys.argv[1] if len(sys.argv) > 1 else '/dev/cu.usbmodem31301'
with serial.Serial(port, 115200, timeout=0.2) as connection:
    connection.dtr = len(sys.argv) > 2 and sys.argv[2] == 'dtr-on'
    connection.rts = True
    time.sleep(0.25)
    connection.rts = False
    until = time.monotonic() + 12
    while time.monotonic() < until:
        chunk = connection.read(1024)
        if chunk:
            sys.stdout.write(chunk.decode('utf-8', errors='replace'))
            sys.stdout.flush()
