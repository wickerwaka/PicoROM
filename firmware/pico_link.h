#if !defined(PICO_LINK_H)
#define PICO_LINK_H 1

#include <stdint.h>
#include <stdlib.h>

enum class PacketType : uint8_t
{
    Write = 6,
    Read = 7,
    ReadData = 8,

    CommitFlash = 12,
    CommitDone = 13,

    SetParameter = 20,
    GetParameter = 21,
    Parameter = 22,
    ParameterError = 23,
    QueryParameter = 24,

    CommsStart = 80,
    CommsEnd = 81,
    CommsData = 82,

    Identify = 0xf8,
    Bootsel = 0xf9,
    Error = 0xfe,
    Debug = 0xff
};

static constexpr size_t MAX_PKT_PAYLOAD = 36;

struct Packet
{
    uint8_t type;
    uint8_t size;

    uint8_t payload[MAX_PKT_PAYLOAD];
};

// Packet handler callback type
typedef void (*PacketHandler)(const Packet *pkt);

// Initialize pico_link with a packet handler callback
void pl_init(PacketHandler handler);

// Send functions
void pl_send_null(PacketType type);
void pl_send_string(PacketType type, const char *s);
void pl_send_payload(PacketType type, const void *data, size_t len);
void pl_send_payload_offset(PacketType type, uint32_t offset, const void *data, size_t len);
void pl_send_debug(const char *s, uint32_t v0, uint32_t v1);
void pl_send_error(const char *s, uint32_t v0, uint32_t v1);
void pl_send_packet(const Packet *pkt);

// Activity tracking
bool pl_check_activity();

#endif // PICO_LINK_H
