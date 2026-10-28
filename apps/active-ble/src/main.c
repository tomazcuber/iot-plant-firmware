#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(active_ble, LOG_LEVEL_INF);

/* Streaming service UUID: 2a5b4897-2a20-4096-83c6-a15ff4d4c22a */
#define BT_UUID_STREAMING_SERVICE \
    BT_UUID_128_ENCODE(0x2a5b4897, 0x2a20, 0x4096, 0x83c6, 0xa15ff4d4c22aULL)