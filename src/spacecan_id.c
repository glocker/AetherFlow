#include "spacecan.h"

#include <string.h>

#define SPACECAN_CAN_ID_SYNC 0x080u
#define SPACECAN_CAN_ID_REPLY_BASE 0x580u
#define SPACECAN_CAN_ID_REQUEST_BASE 0x600u
#define SPACECAN_CAN_ID_HEARTBEAT_BASE 0x700u

bool can_frame_init(can_frame_t *frame,
                    uint32_t id,
                    const uint8_t *data,
                    uint8_t dlc,
                    bool is_extended)
{
    if (frame == NULL) {
        return false;
    }
    if (dlc > CAN_FRAME_MAX_DATA_LEN) {
        return false;
    }
    if (is_extended) {
        if (id > CAN_EXTENDED_ID_MAX) {
            return false;
        }
    } else if (id > CAN_STANDARD_ID_MAX) {
        return false;
    }
    if (dlc > 0u && data == NULL) {
        return false;
    }

    frame->id = id;
    frame->dlc = dlc;
    memset(frame->data, 0, sizeof(frame->data));
    if (dlc > 0u) {
        memcpy(frame->data, data, dlc);
    }
    frame->is_extended = is_extended;
    frame->is_rtr = false;
    frame->is_error = false;
    return true;
}

bool spacecan_node_id_valid(uint8_t node_id)
{
    return node_id <= SPACECAN_NODE_MAX;
}

uint32_t spacecan_make_can_id(spacecan_frame_class_t frame_class, uint8_t node_id)
{
    if (!spacecan_node_id_valid(node_id)) {
        return UINT32_MAX;
    }

    switch (frame_class) {
    case SPACECAN_FRAME_SYNC:
        return SPACECAN_CAN_ID_SYNC;
    case SPACECAN_FRAME_HEARTBEAT:
        return SPACECAN_CAN_ID_HEARTBEAT_BASE + node_id;
    case SPACECAN_FRAME_REQUEST:
        return SPACECAN_CAN_ID_REQUEST_BASE + node_id;
    case SPACECAN_FRAME_REPLY:
        return SPACECAN_CAN_ID_REPLY_BASE + node_id;
    default:
        return UINT32_MAX;
    }
}

spacecan_status_t spacecan_parse_can_id(uint32_t can_id, spacecan_id_t *parsed)
{
    if (parsed == NULL) {
        return SPACECAN_ERR_NULL;
    }

    if (can_id == SPACECAN_CAN_ID_SYNC) {
        parsed->frame_class = SPACECAN_FRAME_SYNC;
        parsed->node_id = SPACECAN_NODE_BROADCAST;
        return SPACECAN_OK;
    }

    if (can_id >= SPACECAN_CAN_ID_REPLY_BASE &&
        can_id <= SPACECAN_CAN_ID_REPLY_BASE + SPACECAN_NODE_MAX) {
        parsed->frame_class = SPACECAN_FRAME_REPLY;
        parsed->node_id = (uint8_t)(can_id - SPACECAN_CAN_ID_REPLY_BASE);
        return SPACECAN_OK;
    }

    if (can_id >= SPACECAN_CAN_ID_REQUEST_BASE &&
        can_id <= SPACECAN_CAN_ID_REQUEST_BASE + SPACECAN_NODE_MAX) {
        parsed->frame_class = SPACECAN_FRAME_REQUEST;
        parsed->node_id = (uint8_t)(can_id - SPACECAN_CAN_ID_REQUEST_BASE);
        return SPACECAN_OK;
    }

    if (can_id >= SPACECAN_CAN_ID_HEARTBEAT_BASE &&
        can_id <= SPACECAN_CAN_ID_HEARTBEAT_BASE + SPACECAN_NODE_MAX) {
        parsed->frame_class = SPACECAN_FRAME_HEARTBEAT;
        parsed->node_id = (uint8_t)(can_id - SPACECAN_CAN_ID_HEARTBEAT_BASE);
        return SPACECAN_OK;
    }

    return SPACECAN_ERR_RANGE;
}

spacecan_status_t spacecan_make_frame(can_frame_t *frame,
                                      spacecan_frame_class_t frame_class,
                                      uint8_t node_id,
                                      const uint8_t *data,
                                      uint8_t dlc)
{
    uint32_t can_id = spacecan_make_can_id(frame_class, node_id);
    if (can_id == UINT32_MAX) {
        return SPACECAN_ERR_RANGE;
    }
    return can_frame_init(frame, can_id, data, dlc, false) ? SPACECAN_OK : SPACECAN_ERR_INVALID_FRAME;
}
