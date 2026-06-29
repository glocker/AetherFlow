#ifndef AETHERFLOW_CAN_FRAME_WIRE_H
#define AETHERFLOW_CAN_FRAME_WIRE_H

#include "can_frame.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAN_FRAME_WIRE_SIZE 19u

bool can_frame_wire_encode(const can_frame_t *frame,
                           uint8_t *out,
                           size_t out_capacity,
                           size_t *out_len);

bool can_frame_wire_decode(const uint8_t *data,
                           size_t data_len,
                           can_frame_t *out_frame);

#ifdef __cplusplus
}
#endif

#endif /* AETHERFLOW_CAN_FRAME_WIRE_H */
