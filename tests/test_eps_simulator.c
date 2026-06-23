#include "eps_simulator.h"
#include "spacecan_services.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "ASSERT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_U32(expected, actual) do { \
    uint32_t exp_ = (uint32_t)(expected); \
    uint32_t act_ = (uint32_t)(actual); \
    if (exp_ != act_) { \
        fprintf(stderr, "ASSERT_EQ_U32 failed at %s:%d: expected 0x%08X got 0x%08X\n", \
                __FILE__, __LINE__, exp_, act_); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ_STATUS(expected, actual) ASSERT_EQ_U32((uint32_t)(expected), (uint32_t)(actual))

static int make_sync(can_frame_t *frame)
{
    ASSERT_TRUE(can_frame_init(frame,
                               spacecan_make_can_id(SPACECAN_FRAME_SYNC, SPACECAN_NODE_BROADCAST),
                               NULL,
                               0u,
                               false));
    return 0;
}

static int test_state_machine_waits_until_operational(void)
{
    eps_simulator_t eps;
    can_frame_t sync;
    can_frame_t frames[EPS_MAX_REPLY_FRAMES];
    size_t frame_count = 99u;

    ASSERT_EQ_U32(0u, make_sync(&sync));
    eps_simulator_init(&eps, EPS_SIMULATOR_NODE_ID);
    ASSERT_EQ_U32(EPS_STATE_BOOT, eps.state);

    ASSERT_EQ_STATUS(SPACECAN_OK,
                     eps_simulator_accept_frame(&eps, &sync, frames, EPS_MAX_REPLY_FRAMES, &frame_count));
    ASSERT_EQ_U32(EPS_STATE_PRE_OPERATIONAL, eps.state);
    ASSERT_EQ_U32(0u, frame_count);

    ASSERT_EQ_STATUS(SPACECAN_OK,
                     eps_simulator_accept_frame(&eps, &sync, frames, EPS_MAX_REPLY_FRAMES, &frame_count));
    ASSERT_EQ_U32(EPS_STATE_OPERATIONAL, eps.state);
    ASSERT_TRUE(frame_count > 0u);
    return 0;
}

static int test_housekeeping_report_from_node_1(void)
{
    eps_simulator_t eps;
    can_frame_t sync;
    can_frame_t frames[EPS_MAX_REPLY_FRAMES];
    spacecan_reassembly_t reassembly;
    uint8_t packet[SPACECAN_PACKET_MAX_SIZE];
    size_t frame_count = 0u;
    size_t packet_len = 0u;
    size_t i;
    spacecan_packet_view_t view;

    ASSERT_EQ_U32(0u, make_sync(&sync));
    eps_simulator_init(&eps, EPS_SIMULATOR_NODE_ID);

    ASSERT_EQ_STATUS(SPACECAN_OK,
                     eps_simulator_accept_frame(&eps, &sync, frames, EPS_MAX_REPLY_FRAMES, &frame_count));
    ASSERT_EQ_U32(0u, frame_count);
    ASSERT_EQ_STATUS(SPACECAN_OK,
                     eps_simulator_accept_frame(&eps, &sync, frames, EPS_MAX_REPLY_FRAMES, &frame_count));

    ASSERT_EQ_U32(2u, frame_count);
    for (i = 0u; i < frame_count; ++i) {
        ASSERT_EQ_U32(0x581u, frames[i].id);
        ASSERT_TRUE(!frames[i].is_extended);
    }

    spacecan_reassembly_reset(&reassembly);
    for (i = 0u; i < frame_count; ++i) {
        spacecan_status_t expected = (i + 1u == frame_count) ? SPACECAN_OK : SPACECAN_ERR_IN_PROGRESS;
        ASSERT_EQ_STATUS(expected,
                         spacecan_reassembly_accept(&reassembly,
                                                    &frames[i],
                                                    packet,
                                                    sizeof(packet),
                                                    &packet_len));
    }

    ASSERT_EQ_STATUS(SPACECAN_OK, spacecan_packet_parse(packet, packet_len, &view));
    ASSERT_EQ_U32(SPACECAN_SERVICE_HOUSEKEEPING, view.service);
    ASSERT_EQ_U32(SPACECAN_HK_SUBTYPE_REPORT, view.subtype);
    ASSERT_EQ_U32(EPS_HOUSEKEEPING_PAYLOAD_LEN, view.payload_len);
    ASSERT_EQ_U32(2u, spacecan_get_u16_be(&view.payload[0]));
    ASSERT_EQ_U32(EPS_STATE_OPERATIONAL, view.payload[2]);
    ASSERT_EQ_U32(eps.last_measurements.bus_voltage_mv, spacecan_get_u16_be(&view.payload[3]));
    ASSERT_EQ_U32(eps.last_measurements.battery_percent, view.payload[7]);
    return 0;
}

static int test_rejects_non_sync(void)
{
    eps_simulator_t eps;
    can_frame_t frame;
    can_frame_t out[EPS_MAX_REPLY_FRAMES];
    size_t out_count = 0u;
    uint8_t data[] = {0u};

    eps_simulator_init(&eps, EPS_SIMULATOR_NODE_ID);
    ASSERT_TRUE(can_frame_init(&frame, spacecan_make_can_id(SPACECAN_FRAME_REQUEST, 1u), data, sizeof(data), false));
    ASSERT_EQ_STATUS(SPACECAN_ERR_INVALID_FRAME,
                     eps_simulator_accept_frame(&eps, &frame, out, EPS_MAX_REPLY_FRAMES, &out_count));
    ASSERT_EQ_U32(0u, out_count);
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_state_machine_waits_until_operational();
    failures += test_housekeeping_report_from_node_1();
    failures += test_rejects_non_sync();

    if (failures == 0) {
        puts("eps simulator tests passed");
    }
    return failures == 0 ? 0 : 1;
}
