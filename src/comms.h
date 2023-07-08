#if !defined(COMMS_H)
#define COMMS_H 1

#include <stdlib.h>
#include <stdint.h>

#include "hardware/sync.h"

template<uint32_t N>
struct FIFO
{
    uint32_t head;
    uint32_t tail;
    uint8_t data[N];

    FIFO()
    {
        head = tail = 0;
    }

    void clear()
    {
        tail = head;
    }

    uint32_t count() const
    {
        return head - tail;
    }

    bool is_full() const
    {
        return count() == N;
    }

    bool is_empty() const
    {
        return count() == 0;
    }

    void push(uint8_t v)
    {
        data[head % N] = v;
        __dmb();
        head++;
    }

    uint8_t pop()
    {
        uint8_t v = data[tail % N];
        __dmb();
        tail++;
        return v;
    }

    uint8_t peek()
    {
        return data[tail % N];
    }
};

void comms_init();
void comms_begin_session(uint32_t addr, uint8_t *rom_base);
void comms_end_session();
bool comms_update(const uint8_t *data, uint32_t len, uint32_t timeout_ms);

#endif // COMMS_H