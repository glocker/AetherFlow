#ifndef AETHERFLOW_EPS_SIMULATOR_H
#define AETHERFLOW_EPS_SIMULATOR_H

#include "spacecan.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EPS_SIMULATOR_NODE_ID 1u
#define EPS_HOUSEKEEPING_PAYLOAD_LEN 11u
#define EPS_MAX_REPLY_FRAMES 4u

typedef enum {
    EPS_STATE_BOOT = 0,
    EPS_STATE_PRE_OPERATIONAL = 1,
    EPS_STATE_OPERATIONAL = 2,
    EPS_STATE_SAFE = 3,
} eps_state_t;

typedef struct {
    uint16_t sequence;
    uint16_t bus_voltage_mv;
    int16_t bus_current_ma;
    uint8_t battery_percent;
    int16_t temperature_cdeg;
    uint8_t status_flags;
} eps_measurements_t;

typedef struct {
    uint8_t node_id;
    eps_state_t state;
    uint16_t sync_count;
    eps_measurements_t last_measurements;
} eps_simulator_t;

const char *eps_state_name(eps_state_t state);
void eps_simulator_init(eps_simulator_t *eps, uint8_t node_id);

spacecan_status_t eps_build_housekeeping_payload(const eps_measurements_t *measurements,
                                                 eps_state_t state,
                                                 uint8_t *out_payload,
                                                 size_t out_capacity,
                                                 size_t *out_len);

spacecan_status_t eps_simulator_accept_frame(eps_simulator_t *eps,
                                             const can_frame_t *incoming,
                                             can_frame_t *out_frames,
                                             size_t out_frame_capacity,
                                             size_t *out_frame_count);

#ifdef __cplusplus
}
#endif

#endif /* AETHERFLOW_EPS_SIMULATOR_H */
