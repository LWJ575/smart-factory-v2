#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
OTA 固件 HTTP 服务器 (OTA Demo / v2.1 / v2.2 配套, 支持 Range 分块下载)

用法:
    python http_server.py [端口]

默认 8080 端口, 以本脚本所在目录为根目录提供文件服务。
把编译好的 firmware.bin 放到本目录 (tools/) 即可通过
http://<PC IP>:8080/firmware.bin 下载。

v1.2: 支持 HTTP Range 请求 (206 Partial Content) —— bootloader v1.2
按 16KB 分块请求固件, 块间无数据在途, 保证 Flash 写入窗口安全。
Range 末尾超出文件大小时自动钳位 (206 bytes start-(size-1)/size)。

Windows 防火墙首次运行会弹窗, 需允许 (专用网络)。
"""
import http.server
import os
import re
import sys

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
ROOT = os.path.dirname(os.path.abspath(__file__))
os.chdir(ROOT)

RANGE_RE = re.compile(r'bytes=(\d+)-(\d*)$')


class RangeFileHandler(http.server.SimpleHTTPRequestHandler):
    """带 Range 支持的静态文件服务 (单文件请求, 不处理 multipart)"""

    def send_head(self):
        """GET/HEAD 共用入口: 有 Range 头且是文件时走 206 分支"""
        path = self.translate_path(self.path)
        if os.path.isdir(path) or not os.path.isfile(path):
            return super().send_head()          # 目录/404 交给父类

        range_hdr = self.headers.get('Range')
        if not range_hdr:
            return super().send_head()          # 普通 200 全量

        m = RANGE_RE.match(range_hdr.strip())
        if not m:
            return super().send_head()

        f = open(path, 'rb')
        size = os.fstat(f.fileno()).st_size
        start = int(m.group(1))
        end = int(m.group(2)) if m.group(2) else size - 1
        end = min(end, size - 1)                # 末尾越界 → 钳位

        if start > end or start >= size:
            f.close()
            self.send_response(416)
            self.send_header('Content-Range', 'bytes */%d' % size)
            self.end_headers()
            return None

        self._range_len = end - start + 1
        self.send_response(206)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Content-Range',
                         'bytes %d-%d/%d' % (start, end, size))
        self.send_header('Content-Length', str(self._range_len))
        self.send_header('Accept-Ranges', 'bytes')
        self.end_headers()
        f.seek(start)
        return f

    def copyfile(self, src, dst):
        n = getattr(self, '_range_len', None)
        if n is None:
            return super().copyfile(src, dst)
        while n > 0:
            chunk = src.read(min(8192, n))
            if not chunk:
                break
            dst.write(chunk)
            n -= len(chunk)


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
    print(f"[HTTP] http://0.0.0.0:{PORT}/firmware.bin  (Range 支持: 是)")
    print("[HTTP] Ctrl+C 停止\n")

    with ReuseAddrServer(("0.0.0.0", PORT), RangeFileHandler) as httpd:
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\n[HTTP] stopped")


if __name__ == "__main__":
    main()
