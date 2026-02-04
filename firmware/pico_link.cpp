#include "pico_link.h"

#include <string.h>
#include <tusb.h>

static PacketHandler packet_handler = nullptr;
static uint8_t activity_count = 0;
static uint8_t activity_report = 0;

void pl_init(PacketHandler handler)
{
    packet_handler = handler;
}

// USB RX callback - called by TinyUSB when data arrives
extern "C" void tud_vendor_rx_cb(uint8_t itf, uint8_t const *buffer, uint16_t bufsize)
{
    (void)itf;

    // Entire packet arrives at once (30-byte max payload + 2-byte header fits in 64-byte endpoint)
    if (bufsize >= 2 && packet_handler)
    {
        const Packet *pkt = (const Packet *)buffer;
        if ((pkt->size + 2) <= bufsize)
        {
            activity_count++;
            packet_handler(pkt);
        }
    }

    // Arm next transfer
    tud_vendor_read_flush();
}

static void usb_send(const void *data, size_t len)
{
    const uint8_t *ptr = (const uint8_t *)data;
    uint32_t remaining = len;

    while (remaining > 0)
    {
        uint32_t sent = tud_vendor_write(ptr, remaining);
        ptr += sent;
        remaining -= sent;
        tud_vendor_write_flush();
    }

    activity_count++;
}

void pl_send_packet(const Packet *pkt)
{
    usb_send(pkt, pkt->size + 2);
}

void pl_send_null(PacketType type)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)type;
    buf[1] = 0;
    usb_send(buf, 2);
}

void pl_send_string(PacketType type, const char *s)
{
    Packet pkt;
    pkt.type = (uint8_t)type;
    pkt.size = MIN(MAX_PKT_PAYLOAD, strlen(s));
    strncpy((char *)pkt.payload, s, pkt.size);
    usb_send(&pkt, pkt.size + 2);
}

void pl_send_payload(PacketType type, const void *data, size_t len)
{
    Packet pkt;
    pkt.type = (uint8_t)type;
    pkt.size = MIN(len, MAX_PKT_PAYLOAD);
    memcpy(pkt.payload, data, len);
    usb_send(&pkt, pkt.size + 2);
}

void pl_send_payload_offset(PacketType type, uint32_t offset, const void *data, size_t len)
{
    Packet pkt;
    pkt.type = (uint8_t)type;
    size_t data_len = MIN(len, MAX_PKT_PAYLOAD - 4);
    pkt.size = 4 + data_len;
    memcpy(pkt.payload, &offset, sizeof(uint32_t));
    memcpy(pkt.payload + 4, data, data_len);
    usb_send(&pkt, pkt.size + 2);
}

void pl_send_debug(const char *s, uint32_t v0, uint32_t v1)
{
    Packet pkt;
    pkt.type = (uint8_t)PacketType::Debug;
    pkt.size = MIN(MAX_PKT_PAYLOAD, strlen(s) + 8);
    memcpy(pkt.payload, &v0, sizeof(v0));
    memcpy(pkt.payload + 4, &v1, sizeof(v1));
    strncpy((char *)pkt.payload + 8, s, pkt.size - 8);
    usb_send(&pkt, pkt.size + 2);
}

void pl_send_error(const char *s, uint32_t v0, uint32_t v1)
{
    Packet pkt;
    pkt.type = (uint8_t)PacketType::Error;
    pkt.size = MIN(MAX_PKT_PAYLOAD, strlen(s) + 8);
    memcpy(pkt.payload, &v0, sizeof(v0));
    memcpy(pkt.payload + 4, &v1, sizeof(v1));
    strncpy((char *)pkt.payload + 8, s, pkt.size - 8);
    usb_send(&pkt, pkt.size + 2);
}

bool pl_check_activity()
{
    if (activity_count != activity_report)
    {
        activity_report = activity_count;
        return true;
    }

    return false;
}
