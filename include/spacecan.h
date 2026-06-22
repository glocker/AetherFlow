#ifndef AETHERFLOW_SPACECAN_H
#define AETHERFLOW_SPACECAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPACECAN_NODE_BROADCAST 0u
#define SPACECAN_NODE_MAX 127u
#define SPACECAN_PACKET_MAX_SIZE 255u
#define SPACECAN_FRAGMENT_SEQUENCE_MAX 63u

/*
 * Stage-1 SpaceCAN codec.
 *
 * Transport adapters must stay outside this module.  The CAN identifiers below
 * intentionally use the common LibreCube/CANopen-style standard arbitration ID
 * ranges so the codec can be exercised in-memory now and wired to SocketCAN only
 * in a later stage.
 */
typedef enum {
    SPACECAN_FRAME_SYNC = 0,
    SPACECAN_FRAME_HEARTBEAT = 1,
    SPACECAN_FRAME_REQUEST = 2,
    SPACECAN_FRAME_REPLY = 3,
} spacecan_frame_class_t;

typedef struct {
    spacecan_frame_class_t frame_class;
    uint8_t node_id;
} spacecan_id_t;

typedef enum {
    SPACECAN_OK = 0,
    SPACECAN_ERR_NULL = -1,
    SPACECAN_ERR_RANGE = -2,
    SPACECAN_ERR_BUFFER_TOO_SMALL = -3,
    SPACECAN_ERR_INVALID_FRAME = -4,
    SPACECAN_ERR_UNEXPECTED_FRAGMENT = -5,
    SPACECAN_ERR_SEQUENCE = -6,
    SPACECAN_ERR_IN_PROGRESS = -7,
} spacecan_status_t;

typedef enum {
    SPACECAN_FRAGMENT_SINGLE = 0,
    SPACECAN_FRAGMENT_FIRST = 1,
    SPACECAN_FRAGMENT_CONSECUTIVE = 2,
    SPACECAN_FRAGMENT_LAST = 3,
} spacecan_fragment_kind_t;

typedef struct {
    uint8_t service;
    uint8_t subtype;
    const uint8_t *payload;
    size_t payload_len;
} spacecan_packet_view_t;

typedef struct {
    bool active;
    uint8_t expected_total_len;
    uint8_t received_len;
    uint8_t next_sequence;
    uint8_t buffer[SPACECAN_PACKET_MAX_SIZE];
} spacecan_reassembly_t;

bool spacecan_node_id_valid(uint8_t node_id);
uint32_t spacecan_make_can_id(spacecan_frame_class_t frame_class, uint8_t node_id);
spacecan_status_t spacecan_parse_can_id(uint32_t can_id, spacecan_id_t *parsed);

spacecan_status_t spacecan_make_frame(can_frame_t *frame,
                                      spacecan_frame_class_t frame_class,
                                      uint8_t node_id,
                                      const uint8_t *data,
                                      uint8_t dlc);

spacecan_status_t spacecan_packet_build(uint8_t service,
                                        uint8_t subtype,
                                        const uint8_t *payload,
                                        size_t payload_len,
                                        uint8_t *out_packet,
                                        size_t out_capacity,
                                        size_t *out_len);

spacecan_status_t spacecan_packet_parse(const uint8_t *packet,
                                        size_t packet_len,
                                        spacecan_packet_view_t *out_view);

spacecan_status_t spacecan_fragment_packet(spacecan_frame_class_t frame_class,
                                           uint8_t node_id,
                                           const uint8_t *packet,
                                           size_t packet_len,
                                           can_frame_t *out_frames,
                                           size_t out_frame_capacity,
                                           size_t *out_frame_count);

void spacecan_reassembly_reset(spacecan_reassembly_t *state);
spacecan_status_t spacecan_reassembly_accept(spacecan_reassembly_t *state,
                                             const can_frame_t *frame,
                                             uint8_t *out_packet,
                                             size_t out_capacity,
                                             size_t *out_len);

uint8_t spacecan_u8(const uint8_t *data);
uint16_t spacecan_get_u16_be(const uint8_t *data);
uint32_t spacecan_get_u32_be(const uint8_t *data);
int16_t spacecan_get_i16_be(const uint8_t *data);
int32_t spacecan_get_i32_be(const uint8_t *data);
void spacecan_put_u16_be(uint8_t *data, uint16_t value);
void spacecan_put_u32_be(uint8_t *data, uint32_t value);
void spacecan_put_i16_be(uint8_t *data, int16_t value);
void spacecan_put_i32_be(uint8_t *data, int32_t value);

#ifdef __cplusplus
}
#endif

#endif /* AETHERFLOW_SPACECAN_H */
