#ifndef AETHERFLOW_CAN_FRAME_H
#define AETHERFLOW_CAN_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAN_FRAME_MAX_DATA_LEN 8u
#define CAN_STANDARD_ID_MAX 0x7FFu
#define CAN_EXTENDED_ID_MAX 0x1FFFFFFFu

typedef struct {
    uint32_t id; // CAN arbitration ID
    uint8_t dlc; // How many bytes of data are in frame, max 8
    uint8_t data[CAN_FRAME_MAX_DATA_LEN]; // CAN frame payload
    bool is_extended; // Whether the frame is extended (29-bit ID) or standard (11-bit ID)
    bool is_rtr; // Remote transmission request
    bool is_error; // Error frame
} can_frame_t;

bool can_frame_init(can_frame_t *frame,
                    uint32_t id,
                    const uint8_t *data,
                    uint8_t dlc,
                    bool is_extended);

#endif /* AETHERFLOW_CAN_FRAME_H */
