"""连接 qemu telnet monitor，读指定地址内存 / 寄存器，打印结果。"""
import socket
import sys
import time

PORT = 45454


def connect():
    s = socket.create_connection(("127.0.0.1", PORT), timeout=5)
    s.settimeout(3)
    # 关闭 telnet 协商（发 IAC WONT/IAC DO 等可略; 直接应答 DONT）
    # 收到协商就回 WONT(252)/DONT(254)，避免超时
    return s


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
                if cmd == 0xFA:  # subneg
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
    s = connect()
    cmd = sys.argv[1] if len(sys.argv) > 1 else "info registers"
    # send IAC DONT for all, then command
    s.sendall(os_command(cmd))
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


def os_command(cmd: str) -> bytes:
    return cmd.encode("utf-8") + b"\n"


if __name__ == "__main__":
    main()