#if !defined(PICO_LINK_H)
#define PICO_LINK_H 1

#include <stdint.h>
#include <stdlib.h>

enum class PacketType : uint8_t
{
    IdentReq = 0,
    IdentResp = 1,
    IdentSet = 2,

    SetPointer = 3,
    GetPointer = 4,
    CurPointer = 5,
    Write = 6,
    Read = 7,
    ReadData = 8,

    CommitFlash = 12,
    CommitDone = 13,

    CommsStart = 80,
    CommsEnd = 81,

    Error = 0xfe,
    Debug = 0xff
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

void pl_wait_for_connection();
bool pl_is_connected();
const Packet *pl_poll();
void pl_consume_packet(const Packet *pkt);


#endif // PICO_LINK_H