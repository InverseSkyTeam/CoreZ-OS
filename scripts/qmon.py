"""Connect to qemu telnet monitor and run one command. usage: qmon.py <port> "<cmd>" """
import socket
import sys
import time

def strip_iac(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        b = data[i]
        if b == 0xFF:
            if i + 2 < n:
                cmd = data[i + 1]
                opt = data[i + 2]
                i += 3
                if cmd == 0xFA:
                    try:
                        end = data.index(b"\xFF\xF0", i)
                        i = end + 2
                    except ValueError:
                        break
            else:
                break
        else:
            out.append(b)
            i += 1
    return bytes(out)

def main():
    port = int(sys.argv[1])
    cmd = sys.argv[2] if len(sys.argv) > 2 else "info registers"
    s = socket.create_connection(("127.0.0.1", port), timeout=5)
    s.settimeout(3)
    s.sendall(cmd.encode("utf-8") + b"\n")
    time.sleep(0.6)
    data = b""
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
    except socket.timeout:
        pass
    s.close()
    print(strip_iac(data).decode("utf-8", "replace"))

if __name__ == "__main__":
    main()