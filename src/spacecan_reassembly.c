#include "spacecan.h"

#include <string.h>

#define SPACECAN_FRAGMENT_KIND_SHIFT 6u
#define SPACECAN_FRAGMENT_SEQ_MASK 0x3Fu
#define SPACECAN_SINGLE_PAYLOAD_CAPACITY 7u
#define SPACECAN_FIRST_PAYLOAD_CAPACITY 6u
#define SPACECAN_CONT_PAYLOAD_CAPACITY 7u

static spacecan_fragment_kind_t frame_kind(const can_frame_t *frame)
{
    return (spacecan_fragment_kind_t)(frame->data[0] >> SPACECAN_FRAGMENT_KIND_SHIFT);
}

static uint8_t frame_sequence(const can_frame_t *frame)
{
    return (uint8_t)(frame->data[0] & SPACECAN_FRAGMENT_SEQ_MASK);
}

static spacecan_status_t validate_data_frame(const can_frame_t *frame)
{
    spacecan_id_t parsed;

    if (frame == NULL) {
        return SPACECAN_ERR_NULL;
    }
    if (frame->is_extended || frame->is_rtr || frame->is_error || frame->dlc == 0u || frame->dlc > CAN_FRAME_MAX_DATA_LEN) {
        return SPACECAN_ERR_INVALID_FRAME;
    }
    return spacecan_parse_can_id(frame->id, &parsed);
}

void spacecan_reassembly_reset(spacecan_reassembly_t *state)
{
    if (state != NULL) {
        memset(state, 0, sizeof(*state));
    }
}

// Gets frames one by one
spacecan_status_t spacecan_reassembly_accept(spacecan_reassembly_t *state,
                                             const can_frame_t *frame,
                                             uint8_t *out_packet,
                                             size_t out_capacity,
                                             size_t *out_len)
{
    spacecan_status_t status;
    spacecan_fragment_kind_t kind;
    uint8_t sequence;

    if (state == NULL || out_packet == NULL || out_len == NULL) {
        return SPACECAN_ERR_NULL;
    }

    *out_len = 0u;
    status = validate_data_frame(frame);
    if (status != SPACECAN_OK) {
        return status;
    }

    kind = frame_kind(frame);
    sequence = frame_sequence(frame);

    switch (kind) {

    // Returns single ready packet back
    case SPACECAN_FRAGMENT_SINGLE: {
        size_t packet_len;
        if (state->active) {
            return SPACECAN_ERR_UNEXPECTED_FRAGMENT;
        }
        if (sequence != 0u || frame->dlc < 2u || frame->dlc > SPACECAN_SINGLE_PAYLOAD_CAPACITY + 1u) {
            return SPACECAN_ERR_INVALID_FRAME;
        }
        packet_len = frame->dlc - 1u;
        if (out_capacity < packet_len) {
            return SPACECAN_ERR_BUFFER_TOO_SMALL;
        }
        memcpy(out_packet, &frame->data[1], packet_len);
        *out_len = packet_len;
        return SPACECAN_OK;
    }

    // Remembers total_len
    // Copy first bytes
    // Awaits next fragment
    case SPACECAN_FRAGMENT_FIRST: {
        uint8_t total_len;
        if (state->active) {
            return SPACECAN_ERR_UNEXPECTED_FRAGMENT;
        }
        if (sequence != 0u || frame->dlc != CAN_FRAME_MAX_DATA_LEN) {
            return SPACECAN_ERR_INVALID_FRAME;
        }
        total_len = frame->data[1];
        if (total_len <= SPACECAN_SINGLE_PAYLOAD_CAPACITY || total_len > SPACECAN_PACKET_MAX_SIZE) {
            return SPACECAN_ERR_INVALID_FRAME;
        }

        state->active = true;
        state->expected_total_len = total_len;
        state->received_len = SPACECAN_FIRST_PAYLOAD_CAPACITY;
        state->next_sequence = 1u;
        memcpy(state->buffer, &frame->data[2], SPACECAN_FIRST_PAYLOAD_CAPACITY);
        return SPACECAN_ERR_IN_PROGRESS;
    }

    // Check sequence and adds data to buffer
    case SPACECAN_FRAGMENT_CONSECUTIVE:

    // Check sequence and adds rest data
    // Returns ready packet for same length
    case SPACECAN_FRAGMENT_LAST: {
        size_t chunk_len;
        if (!state->active) {
            return SPACECAN_ERR_UNEXPECTED_FRAGMENT;
        }
        if (sequence != state->next_sequence) {
            spacecan_reassembly_reset(state);
            return SPACECAN_ERR_SEQUENCE;
        }
        if (frame->dlc < 2u || frame->dlc > SPACECAN_CONT_PAYLOAD_CAPACITY + 1u) {
            spacecan_reassembly_reset(state);
            return SPACECAN_ERR_INVALID_FRAME;
        }

        chunk_len = frame->dlc - 1u;
        if ((size_t)state->received_len + chunk_len > state->expected_total_len) {
            spacecan_reassembly_reset(state);
            return SPACECAN_ERR_INVALID_FRAME;
        }
        if (kind == SPACECAN_FRAGMENT_CONSECUTIVE && chunk_len != SPACECAN_CONT_PAYLOAD_CAPACITY) {
            spacecan_reassembly_reset(state);
            return SPACECAN_ERR_INVALID_FRAME;
        }

        memcpy(&state->buffer[state->received_len], &frame->data[1], chunk_len);
        state->received_len = (uint8_t)(state->received_len + chunk_len);
        state->next_sequence = (uint8_t)((state->next_sequence + 1u) & SPACECAN_FRAGMENT_SEQ_MASK);

        if (kind == SPACECAN_FRAGMENT_LAST) {
            size_t packet_len;
            if (state->received_len != state->expected_total_len) {
                spacecan_reassembly_reset(state);
                return SPACECAN_ERR_INVALID_FRAME;
            }
            packet_len = state->received_len;
            if (out_capacity < packet_len) {
                spacecan_reassembly_reset(state);
                return SPACECAN_ERR_BUFFER_TOO_SMALL;
            }
            memcpy(out_packet, state->buffer, packet_len);
            *out_len = packet_len;
            spacecan_reassembly_reset(state);
            return SPACECAN_OK;
        }

        return SPACECAN_ERR_IN_PROGRESS;
    }

    default:
        return SPACECAN_ERR_INVALID_FRAME;
    }
}
