#!/usr/bin/env python3
"""
OTA Trigger Script — 通过 MQTT 发送 OTA 升级指令给 STM32
在 PC 或 i.MX6ULL 上运行

用法:
    python3 ota_trigger.py <device_id> [broker_ip] [broker_port]

默认:
    broker: 127.0.0.1:1883 (本机 mosquitto)

依赖:
    pip install paho-mqtt

工作流程:
    1. 确认 ota_file_server.py 在运行 (提供 firmware.bin)
    2. 运行此脚本发送 OTA 指令
    3. STM32 收到指令后显示 OTA 界面, 设置 BKP 标志, 重启
    4. Bootloader 下载固件, 写入 Flash, 跳转到新 App
"""

import sys
import time
import json

try:
    import paho.mqtt.client as mqtt
    HAS_MQTT = True
except ImportError:
    HAS_MQTT = False

def main():
    if not HAS_MQTT:
        print("[ERROR] paho-mqtt not installed")
        print("  Install: pip install paho-mqtt")
        # Fall back to mosquitto_pub command
        device_id = sys.argv[1] if len(sys.argv) > 1 else "dev01"
        broker = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
        port = int(sys.argv[3]) if len(sys.argv) > 3 else 1883
        topic = "factory/ota/{}".format(device_id)
        payload = json.dumps({"cmd": "start", "version": "2.0.1"})
        os.system('mosquitto_pub -h {} -p {} -t "{}" -m \'{}\' -q 1'.format(
            broker, port, topic, payload))
        print("[OTA] Sent via mosquitto_pub: {} -> {}".format(topic, payload))
        return

    device_id = sys.argv[1] if len(sys.argv) > 1 else "dev01"
    broker = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 1883

    topic = "factory/ota/{}".format(device_id)
    payload = json.dumps({"cmd": "start", "version": "2.0.1"})

    print("[OTA] Sending OTA trigger...")
    print("[OTA] Broker: {}:{}".format(broker, port))
    print("[OTA] Topic:  {}".format(topic))
    print("[OTA] Payload: {}".format(payload))
    print("-" * 50)

    client = mqtt.Client(client_id="ota_trigger_{}".format(device_id))
    client.connect(broker, port, 30)
    client.loop_start()

    result = client.publish(topic, payload, qos=1)
    result.wait_for_publish()

    print("[OTA] Command sent! STM32 should reboot into bootloader.")
    print("[OTA] Watch the STM32 LCD for progress.")
    print("[OTA] Serial debug output (USART1, 115200) shows details.")

    # Listen for status
    ack_topic = "factory/ota_ack/{}".format(device_id)
    client.subscribe(ack_topic)

    print("[OTA] Waiting for ACK on {}...".format(ack_topic))
    print("[OTA] Press Ctrl+C to stop listening")

    try:
        while True:
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass

    client.disconnect()
    print("\n[OTA] Done")

if __name__ == "__main__":
    main()
