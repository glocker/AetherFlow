#include "can_frame_wire.h"

#include <string.h>

#define WIRE_MAGIC_0 'A'
#define WIRE_MAGIC_1 'F'
#define WIRE_MAGIC_2 'C'
#define WIRE_MAGIC_3 '1'
#define WIRE_VERSION 1u
#define WIRE_FLAG_EXTENDED 0x01u
#define WIRE_FLAG_RTR 0x02u
#define WIRE_FLAG_ERROR 0x04u

static void put_u32_be(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24u);
    out[1] = (uint8_t)(value >> 16u);
    out[2] = (uint8_t)(value >> 8u);
    out[3] = (uint8_t)value;
}

static uint32_t get_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24u) |
           ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) |
           (uint32_t)data[3];
}

bool can_frame_wire_encode(const can_frame_t *frame,
                           uint8_t *out,
                           size_t out_capacity,
                           size_t *out_len)
{
    uint8_t flags = 0u;

    if (frame == NULL || out == NULL || out_len == NULL || out_capacity < CAN_FRAME_WIRE_SIZE) {
        return false;
    }
    if (frame->dlc > CAN_FRAME_MAX_DATA_LEN) {
        return false;
    }

    out[0] = (uint8_t)WIRE_MAGIC_0;
    out[1] = (uint8_t)WIRE_MAGIC_1;
    out[2] = (uint8_t)WIRE_MAGIC_2;
    out[3] = (uint8_t)WIRE_MAGIC_3;
    out[4] = WIRE_VERSION;
    if (frame->is_extended) {
        flags |= WIRE_FLAG_EXTENDED;
    }
    if (frame->is_rtr) {
        flags |= WIRE_FLAG_RTR;
    }
    if (frame->is_error) {
        flags |= WIRE_FLAG_ERROR;
    }
    out[5] = flags;
    put_u32_be(&out[6], frame->id);
    out[10] = frame->dlc;
    memset(&out[11], 0, CAN_FRAME_MAX_DATA_LEN);
    if (frame->dlc > 0u) {
        memcpy(&out[11], frame->data, frame->dlc);
    }
    *out_len = CAN_FRAME_WIRE_SIZE;
    return true;
}

bool can_frame_wire_decode(const uint8_t *data,
                           size_t data_len,
                           can_frame_t *out_frame)
{
    uint8_t flags;
    uint8_t dlc;
    uint32_t id;
    bool is_extended;

    if (data == NULL || out_frame == NULL || data_len != CAN_FRAME_WIRE_SIZE) {
        return false;
    }
    if (data[0] != (uint8_t)WIRE_MAGIC_0 || data[1] != (uint8_t)WIRE_MAGIC_1 ||
        data[2] != (uint8_t)WIRE_MAGIC_2 || data[3] != (uint8_t)WIRE_MAGIC_3 ||
        data[4] != WIRE_VERSION) {
        return false;
    }

    flags = data[5];
    dlc = data[10];
    id = get_u32_be(&data[6]);
    is_extended = (flags & WIRE_FLAG_EXTENDED) != 0u;
    if (!can_frame_init(out_frame, id, dlc > 0u ? &data[11] : NULL, dlc, is_extended)) {
        return false;
    }
    out_frame->is_rtr = (flags & WIRE_FLAG_RTR) != 0u;
    out_frame->is_error = (flags & WIRE_FLAG_ERROR) != 0u;
    return true;
}
