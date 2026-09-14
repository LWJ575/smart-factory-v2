#!/usr/bin/env python3
"""
OTA HTTP File Server — 在 PC 或 i.MX6ULL 上运行
提供固件文件下载 (firmware.bin)

用法:
    python3 ota_file_server.py [端口] [固件文件路径]

默认:
    端口: 8080
    固件: ./firmware.bin

工作流程:
    1. 把编译好的 .bin 固件重命名为 firmware.bin
    2. 运行此脚本
    3. STM32 Bootloader 会通过 HTTP GET 下载这个文件
"""

import sys
import os

try:
    from http.server import HTTPServer, SimpleHTTPRequestHandler
except ImportError:
    from BaseHTTPServer import HTTPServer
    from SimpleHTTPServer import SimpleHTTPRequestHandler

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
FIRMWARE = sys.argv[2] if len(sys.argv) > 2 else "firmware.bin"

class FirmwareHandler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/" or self.path == "/firmware.bin":
            if not os.path.exists(FIRMWARE):
                self.send_error(404, "Firmware not found: {}".format(FIRMWARE))
                return
            with open(FIRMWARE, "rb") as f:
                data = f.read()
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(data)
            print("[OTA] Served firmware: {} ({} bytes)".format(FIRMWARE, len(data)))
        else:
            self.send_error(404, "Not found")

    def log_message(self, format, *args):
        print("[HTTP] " + (format % args))

def main():
    if not os.path.exists(FIRMWARE):
        print("[WARN] Firmware file not found: {}".format(FIRMWARE))
        print("[WARN] Place your .bin file as: {}".format(FIRMWARE))
    else:
        size = os.path.getsize(FIRMWARE)
        print("[OTA] Firmware: {} ({} bytes, {:.1f} KB)".format(
            FIRMWARE, size, size / 1024.0))

    print("[OTA] HTTP File Server on port {}".format(PORT))
    print("[OTA] STM32 will download from: http://<this-ip>{}:{}/firmware.bin".format(
        PORT, PORT))
    print("[OTA] Press Ctrl+C to stop")
    print("-" * 50)

    server = HTTPServer(("0.0.0.0", PORT), FirmwareHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[OTA] Server stopped")

if __name__ == "__main__":
    main()
