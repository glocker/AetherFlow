#include "eps_simulator.h"

#include "spacecan_services.h"

#include <string.h>

#define EPS_FLAG_SAFE_MODE 0x01u
#define EPS_FLAG_LOW_BATTERY 0x02u
#define EPS_FLAG_OVERTEMP 0x04u

static eps_state_t next_state(eps_state_t state)
{
    switch (state) {
    case EPS_STATE_BOOT:
        return EPS_STATE_PRE_OPERATIONAL;
    case EPS_STATE_PRE_OPERATIONAL:
        return EPS_STATE_OPERATIONAL;
    case EPS_STATE_OPERATIONAL:
    case EPS_STATE_SAFE:
    default:
        return state;
    }
}

static eps_measurements_t generate_measurements(uint16_t sequence, eps_state_t state)
{
    eps_measurements_t measurements;
    uint16_t tick = (uint16_t)(sequence % 32u);

    measurements.sequence = sequence;
    measurements.bus_voltage_mv = (uint16_t)(state == EPS_STATE_SAFE ? 4850u : 5000u + tick * 3u);
    measurements.bus_current_ma = (int16_t)(state == EPS_STATE_SAFE ? 180 : 420 + (int16_t)(tick * 5u));
    measurements.battery_percent = (uint8_t)(state == EPS_STATE_SAFE ? 42u : 86u - (uint8_t)(sequence % 5u));
    measurements.temperature_cdeg = (int16_t)(state == EPS_STATE_SAFE ? 3150 : 2400 + (int16_t)(tick * 8u));
    measurements.status_flags = 0u;

    if (state == EPS_STATE_SAFE) {
        measurements.status_flags |= EPS_FLAG_SAFE_MODE;
    }
    if (measurements.battery_percent < 20u) {
        measurements.status_flags |= EPS_FLAG_LOW_BATTERY;
    }
    if (measurements.temperature_cdeg > 6000) {
        measurements.status_flags |= EPS_FLAG_OVERTEMP;
    }

    return measurements;
}

static bool is_sync_frame(const can_frame_t *frame)
{
    spacecan_id_t parsed;

    if (frame == NULL || frame->is_extended || frame->is_rtr || frame->is_error || frame->dlc != 0u) {
        return false;
    }
    if (spacecan_parse_can_id(frame->id, &parsed) != SPACECAN_OK) {
        return false;
    }
    return parsed.frame_class == SPACECAN_FRAME_SYNC;
}

const char *eps_state_name(eps_state_t state)
{
    switch (state) {
    case EPS_STATE_BOOT:
        return "BOOT";
    case EPS_STATE_PRE_OPERATIONAL:
        return "PRE_OPERATIONAL";
    case EPS_STATE_OPERATIONAL:
        return "OPERATIONAL";
    case EPS_STATE_SAFE:
        return "SAFE";
    default:
        return "UNKNOWN";
    }
}

void eps_simulator_init(eps_simulator_t *eps, uint8_t node_id)
{
    if (eps == NULL) {
        return;
    }

    memset(eps, 0, sizeof(*eps));
    eps->node_id = spacecan_node_id_valid(node_id) ? node_id : EPS_SIMULATOR_NODE_ID;
    eps->state = EPS_STATE_BOOT;
}

spacecan_status_t eps_build_housekeeping_payload(const eps_measurements_t *measurements,
                                                 eps_state_t state,
                                                 uint8_t *out_payload,
                                                 size_t out_capacity,
                                                 size_t *out_len)
{
    if (measurements == NULL || out_payload == NULL || out_len == NULL) {
        return SPACECAN_ERR_NULL;
    }
    if (out_capacity < EPS_HOUSEKEEPING_PAYLOAD_LEN) {
        return SPACECAN_ERR_BUFFER_TOO_SMALL;
    }

    spacecan_put_u16_be(&out_payload[0], measurements->sequence);
    out_payload[2] = (uint8_t)state;
    spacecan_put_u16_be(&out_payload[3], measurements->bus_voltage_mv);
    spacecan_put_i16_be(&out_payload[5], measurements->bus_current_ma);
    out_payload[7] = measurements->battery_percent;
    spacecan_put_i16_be(&out_payload[8], measurements->temperature_cdeg);
    out_payload[10] = measurements->status_flags;
    *out_len = EPS_HOUSEKEEPING_PAYLOAD_LEN;
    return SPACECAN_OK;
}

spacecan_status_t eps_simulator_accept_frame(eps_simulator_t *eps,
                                             const can_frame_t *incoming,
                                             can_frame_t *out_frames,
                                             size_t out_frame_capacity,
                                             size_t *out_frame_count)
{
    uint8_t payload[EPS_HOUSEKEEPING_PAYLOAD_LEN];
    uint8_t packet[SPACECAN_PACKET_MAX_SIZE];
    size_t payload_len = 0u;
    size_t packet_len = 0u;
    spacecan_status_t status;

    if (eps == NULL || incoming == NULL || out_frames == NULL || out_frame_count == NULL) {
        return SPACECAN_ERR_NULL;
    }

    *out_frame_count = 0u;
    if (!is_sync_frame(incoming)) {
        return SPACECAN_ERR_INVALID_FRAME;
    }

    eps->sync_count = (uint16_t)(eps->sync_count + 1u);
    eps->state = next_state(eps->state);

    if (eps->state != EPS_STATE_OPERATIONAL && eps->state != EPS_STATE_SAFE) {
        return SPACECAN_OK;
    }

    eps->last_measurements = generate_measurements(eps->sync_count, eps->state);

    status = eps_build_housekeeping_payload(&eps->last_measurements,
                                            eps->state,
                                            payload,
                                            sizeof(payload),
                                            &payload_len);
    if (status != SPACECAN_OK) {
        return status;
    }

    status = spacecan_packet_build(SPACECAN_SERVICE_HOUSEKEEPING,
                                   SPACECAN_HK_SUBTYPE_REPORT,
                                   payload,
                                   payload_len,
                                   packet,
                                   sizeof(packet),
                                   &packet_len);
    if (status != SPACECAN_OK) {
        return status;
    }

    return spacecan_fragment_packet(SPACECAN_FRAME_REPLY,
                                    eps->node_id,
                                    packet,
                                    packet_len,
                                    out_frames,
                                    out_frame_capacity,
                                    out_frame_count);
}
