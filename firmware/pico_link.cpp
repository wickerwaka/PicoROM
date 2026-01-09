#include "pico_link.h"

#include "pico/stdlib.h"
#include <string.h>
#include <tusb.h>
#include <unistd.h>


static uint8_t incoming_buffer[sizeof(Packet)];
static uint8_t incoming_count;

static uint8_t activity_count = 0;
static uint8_t activity_report = 0;

void usb_send(const void *data, size_t len)
{
    const uint8_t *ptr = (const uint8_t *)data;
    uint32_t remaining = len;

    while (remaining > 0)
    {
        uint32_t sent = tud_cdc_write(ptr, remaining);
        ptr += sent;
        remaining -= sent;
        tud_task();
    }

    tud_cdc_write_flush();

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

void pl_send_ota_status(const char *s, OTAStatusCode code)
{
    Packet pkt;
    pkt.type = (uint8_t)PacketType::OTAStatus;
    pkt.size = MIN(MAX_PKT_PAYLOAD, strlen(s) + sizeof(code));
    memcpy(pkt.payload, &code, sizeof(code));
    strncpy((char *)pkt.payload + sizeof(code), s, pkt.size - sizeof(code));
    usb_send(&pkt, pkt.size + 2);

    // work and delay to ensure messages get sent
    tud_task(); sleep_ms(1); tud_task();
}

void pl_wait_for_connection()
{
    // Wait for connection
    while (!tud_cdc_connected())
    {
        tud_task();
        sleep_ms(1);
    }

    // Flush input
    tud_cdc_read_flush();
    tud_cdc_write_clear();

    incoming_count = 0;

    activity_count = activity_report = 0;

    // Write preamble
    usb_send("PicoROM Hello", 13);
}

bool pl_is_connected()
{
    return tud_cdc_connected();
}

const Packet *pl_poll()
{
    tud_task();

    uint32_t space = sizeof(incoming_buffer) - incoming_count;
    uint32_t rx_avail = tud_cdc_available();

    uint32_t read_size = MIN(rx_avail, space);
    if (read_size > 0)
    {
        incoming_count += tud_cdc_read(incoming_buffer + incoming_count, read_size);
    }

    if (incoming_count >= 2)
    {
        Packet *pkt = (Packet *)incoming_buffer;
        if ((pkt->size + 2) <= incoming_count)
        {
            activity_count++;
            return pkt;
        }
    }

    return nullptr;
}

void pl_consume_packet(const Packet *pkt)
{
    uint32_t used = pkt->size + 2;
    uint32_t remaining = incoming_count - used;

    if (remaining > 0)
    {
        memmove(incoming_buffer, incoming_buffer + used, remaining);
    }

    incoming_count = remaining;
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
