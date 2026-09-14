#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA 固件 HTTP 服务器 (OTA Demo 配套)

用法:
    python http_server.py [端口]

默认 8080 端口, 以本脚本所在目录为根目录提供文件服务。
把编译好的 firmware.bin 放到本目录 (tools/) 即可通过
http://<PC IP>:8080/firmware.bin 下载。

Windows 防火墙首次运行会弹窗, 需允许 (专用网络)。
"""
import http.server
import os
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)


class ReuseAddrServer(http.server.ThreadingHTTPServer):
    allow_reuse_address = True


def main():
    firmware = os.path.join(ROOT, "firmware.bin")
    if os.path.exists(firmware):
        size = os.path.getsize(firmware)
        print(f"[OK] firmware.bin 就绪: {size} bytes")
    else:
        print("[!!] 当前目录没有 firmware.bin, 下载会返回 404")
        print(f"     请把 Keil 生成的 app bin 放到: {firmware}")

    print(f"\n[HTTP] Serving {ROOT}")
    print(f"[HTTP] http://0.0.0.0:{PORT}/firmware.bin")
    print("[HTTP] Ctrl+C 停止\n")

    handler = http.server.SimpleHTTPRequestHandler
    with ReuseAddrServer(("0.0.0.0", PORT), handler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[HTTP] stopped")


if __name__ == "__main__":
    main()
