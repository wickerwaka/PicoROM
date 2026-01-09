#if !defined(PICO_LINK_H)
#define PICO_LINK_H 1

#include <stdint.h>
#include <stdlib.h>

enum class PacketType : uint8_t
{
    SetPointer = 3,
    GetPointer = 4,
    CurPointer = 5,
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
    
    OTACommit = 30,
    OTAStatus = 31,

    CommsStart = 80,
    CommsEnd = 81,
    CommsData = 82,

    Identify = 0xf8,
    Bootsel = 0xf9,
    Error = 0xfe,
    Debug = 0xff
};

enum class OTAStatusCode : uint8_t
{
    InProgress,
    Complete,
    Error
};

static constexpr size_t MAX_PKT_PAYLOAD = 30;

struct Packet
{
    uint8_t type;
    uint8_t size;

    uint8_t payload[MAX_PKT_PAYLOAD];
};

void pl_send_null(PacketType type);
void pl_send_string(PacketType type, const char *s);
void pl_send_payload(PacketType type, const void *data, size_t len);
void pl_send_debug(const char *s, uint32_t v0, uint32_t v1);
void pl_send_error(const char *s, uint32_t v0, uint32_t v1);
void pl_send_packet(const Packet *pkt);
void pl_send_ota_status(const char *s, OTAStatusCode code);

void pl_wait_for_connection();
bool pl_is_connected();
const Packet *pl_poll();
void pl_consume_packet(const Packet *pkt);

bool pl_check_activity();


#endif // PICO_LINK_H
