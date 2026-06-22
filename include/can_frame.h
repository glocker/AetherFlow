#ifndef AETHERFLOW_CAN_FRAME_H
#define AETHERFLOW_CAN_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAN_FRAME_MAX_DATA_LEN 8u
#define CAN_STANDARD_ID_MAX 0x7FFu
#define CAN_EXTENDED_ID_MAX 0x1FFFFFFFu

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[CAN_FRAME_MAX_DATA_LEN];
    bool is_extended;
    bool is_rtr;
    bool is_error;
} can_frame_t;

bool can_frame_init(can_frame_t *frame,
                    uint32_t id,
                    const uint8_t *data,
                    uint8_t dlc,
                    bool is_extended);

#endif /* AETHERFLOW_CAN_FRAME_H */
