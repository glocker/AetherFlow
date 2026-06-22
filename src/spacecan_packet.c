#include "spacecan.h"

#include <string.h>

#define SPACECAN_PACKET_HEADER_LEN 2u
#define SPACECAN_FRAGMENT_KIND_SHIFT 6u
#define SPACECAN_FRAGMENT_KIND_MASK 0xC0u
#define SPACECAN_FRAGMENT_SEQ_MASK 0x3Fu
#define SPACECAN_SINGLE_PAYLOAD_CAPACITY 7u
#define SPACECAN_FIRST_PAYLOAD_CAPACITY 6u
#define SPACECAN_CONT_PAYLOAD_CAPACITY 7u

static uint8_t fragment_header(spacecan_fragment_kind_t kind, uint8_t sequence)
{
    return (uint8_t)(((uint8_t)kind << SPACECAN_FRAGMENT_KIND_SHIFT) |
                     (sequence & SPACECAN_FRAGMENT_SEQ_MASK));
}

spacecan_status_t spacecan_packet_build(uint8_t service,
                                        uint8_t subtype,
                                        const uint8_t *payload,
                                        size_t payload_len,
                                        uint8_t *out_packet,
                                        size_t out_capacity,
                                        size_t *out_len)
{
    size_t packet_len = SPACECAN_PACKET_HEADER_LEN + payload_len;

    if (out_packet == NULL || out_len == NULL) {
        return SPACECAN_ERR_NULL;
    }
    if (payload_len > 0u && payload == NULL) {
        return SPACECAN_ERR_NULL;
    }
    if (packet_len > SPACECAN_PACKET_MAX_SIZE) {
        return SPACECAN_ERR_RANGE;
    }
    if (out_capacity < packet_len) {
        return SPACECAN_ERR_BUFFER_TOO_SMALL;
    }

    out_packet[0] = service;
    out_packet[1] = subtype;
    if (payload_len > 0u) {
        memcpy(&out_packet[SPACECAN_PACKET_HEADER_LEN], payload, payload_len);
    }
    *out_len = packet_len;
    return SPACECAN_OK;
}

spacecan_status_t spacecan_packet_parse(const uint8_t *packet,
                                        size_t packet_len,
                                        spacecan_packet_view_t *out_view)
{
    if (packet == NULL || out_view == NULL) {
        return SPACECAN_ERR_NULL;
    }
    if (packet_len < SPACECAN_PACKET_HEADER_LEN || packet_len > SPACECAN_PACKET_MAX_SIZE) {
        return SPACECAN_ERR_RANGE;
    }

    out_view->service = packet[0];
    out_view->subtype = packet[1];
    out_view->payload = &packet[SPACECAN_PACKET_HEADER_LEN];
    out_view->payload_len = packet_len - SPACECAN_PACKET_HEADER_LEN;
    return SPACECAN_OK;
}

spacecan_status_t spacecan_fragment_packet(spacecan_frame_class_t frame_class,
                                           uint8_t node_id,
                                           const uint8_t *packet,
                                           size_t packet_len,
                                           can_frame_t *out_frames,
                                           size_t out_frame_capacity,
                                           size_t *out_frame_count)
{
    size_t required_frames;
    size_t offset = 0u;
    size_t frame_index = 0u;
    uint8_t sequence = 0u;

    if (packet == NULL || out_frames == NULL || out_frame_count == NULL) {
        return SPACECAN_ERR_NULL;
    }
    if (packet_len == 0u || packet_len > SPACECAN_PACKET_MAX_SIZE) {
        return SPACECAN_ERR_RANGE;
    }

    if (packet_len <= SPACECAN_SINGLE_PAYLOAD_CAPACITY) {
        uint8_t data[CAN_FRAME_MAX_DATA_LEN] = {0};
        data[0] = fragment_header(SPACECAN_FRAGMENT_SINGLE, 0u);
        memcpy(&data[1], packet, packet_len);
        if (out_frame_capacity < 1u) {
            return SPACECAN_ERR_BUFFER_TOO_SMALL;
        }
        *out_frame_count = 1u;
        return spacecan_make_frame(&out_frames[0], frame_class, node_id, data, (uint8_t)(packet_len + 1u));
    }

    required_frames = 1u;
    if (packet_len > SPACECAN_FIRST_PAYLOAD_CAPACITY) {
        size_t remaining = packet_len - SPACECAN_FIRST_PAYLOAD_CAPACITY;
        required_frames += (remaining + SPACECAN_CONT_PAYLOAD_CAPACITY - 1u) / SPACECAN_CONT_PAYLOAD_CAPACITY;
    }
    if (required_frames > out_frame_capacity) {
        return SPACECAN_ERR_BUFFER_TOO_SMALL;
    }
    if (required_frames > (size_t)SPACECAN_FRAGMENT_SEQUENCE_MAX + 1u) {
        return SPACECAN_ERR_RANGE;
    }

    {
        uint8_t data[CAN_FRAME_MAX_DATA_LEN] = {0};
        data[0] = fragment_header(SPACECAN_FRAGMENT_FIRST, sequence++);
        data[1] = (uint8_t)packet_len;
        memcpy(&data[2], packet, SPACECAN_FIRST_PAYLOAD_CAPACITY);
        spacecan_status_t status = spacecan_make_frame(&out_frames[frame_index++], frame_class, node_id, data, CAN_FRAME_MAX_DATA_LEN);
        if (status != SPACECAN_OK) {
            return status;
        }
        offset = SPACECAN_FIRST_PAYLOAD_CAPACITY;
    }

    while (offset < packet_len) {
        uint8_t data[CAN_FRAME_MAX_DATA_LEN] = {0};
        size_t remaining = packet_len - offset;
        size_t chunk_len = remaining < SPACECAN_CONT_PAYLOAD_CAPACITY ? remaining : SPACECAN_CONT_PAYLOAD_CAPACITY;
        spacecan_fragment_kind_t kind = (offset + chunk_len == packet_len) ?
            SPACECAN_FRAGMENT_LAST : SPACECAN_FRAGMENT_CONSECUTIVE;
        data[0] = fragment_header(kind, sequence++);
        memcpy(&data[1], &packet[offset], chunk_len);

        spacecan_status_t status = spacecan_make_frame(&out_frames[frame_index++],
                                                       frame_class,
                                                       node_id,
                                                       data,
                                                       (uint8_t)(chunk_len + 1u));
        if (status != SPACECAN_OK) {
            return status;
        }
        offset += chunk_len;
    }

    *out_frame_count = frame_index;
    return SPACECAN_OK;
}

uint8_t spacecan_u8(const uint8_t *data)
{
    return data[0];
}

uint16_t spacecan_get_u16_be(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8u) | (uint16_t)data[1]);
}

uint32_t spacecan_get_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24u) |
           ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) |
           (uint32_t)data[3];
}

int16_t spacecan_get_i16_be(const uint8_t *data)
{
    return (int16_t)spacecan_get_u16_be(data);
}

int32_t spacecan_get_i32_be(const uint8_t *data)
{
    return (int32_t)spacecan_get_u32_be(data);
}

void spacecan_put_u16_be(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8u);
    data[1] = (uint8_t)value;
}

void spacecan_put_u32_be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24u);
    data[1] = (uint8_t)(value >> 16u);
    data[2] = (uint8_t)(value >> 8u);
    data[3] = (uint8_t)value;
}

void spacecan_put_i16_be(uint8_t *data, int16_t value)
{
    spacecan_put_u16_be(data, (uint16_t)value);
}

void spacecan_put_i32_be(uint8_t *data, int32_t value)
{
    spacecan_put_u32_be(data, (uint32_t)value);
}
