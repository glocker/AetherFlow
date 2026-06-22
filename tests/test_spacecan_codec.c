#include "spacecan.h"
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

// REPLY node 1 → 0x581
// REQUEST node 1 → 0x601
static int test_can_id_helpers(void)
{
    spacecan_id_t parsed;

    ASSERT_EQ_U32(0x080u, spacecan_make_can_id(SPACECAN_FRAME_SYNC, 0u));
    ASSERT_EQ_U32(0x701u, spacecan_make_can_id(SPACECAN_FRAME_HEARTBEAT, 1u));
    ASSERT_EQ_U32(0x601u, spacecan_make_can_id(SPACECAN_FRAME_REQUEST, 1u));
    ASSERT_EQ_U32(0x581u, spacecan_make_can_id(SPACECAN_FRAME_REPLY, 1u));

    ASSERT_EQ_STATUS(SPACECAN_OK, spacecan_parse_can_id(0x581u, &parsed));
    ASSERT_EQ_U32(SPACECAN_FRAME_REPLY, parsed.frame_class);
    ASSERT_EQ_U32(1u, parsed.node_id);

    ASSERT_EQ_STATUS(SPACECAN_ERR_RANGE, spacecan_parse_can_id(0x123u, &parsed));
    return 0;
}

// 0x1234 → 0x12 0x34
static int test_integer_encoding(void)
{
    uint8_t data[4];

    spacecan_put_u16_be(data, 0x1234u);
    ASSERT_EQ_U32(0x12u, data[0]);
    ASSERT_EQ_U32(0x34u, data[1]);
    ASSERT_EQ_U32(0x1234u, spacecan_get_u16_be(data));

    spacecan_put_u32_be(data, 0x89ABCDEFu);
    ASSERT_EQ_U32(0x89u, data[0]);
    ASSERT_EQ_U32(0xABu, data[1]);
    ASSERT_EQ_U32(0xCDu, data[2]);
    ASSERT_EQ_U32(0xEFu, data[3]);
    ASSERT_EQ_U32(0x89ABCDEFu, spacecan_get_u32_be(data));

    spacecan_put_i16_be(data, -2);
    ASSERT_EQ_U32(0xFFFEu, spacecan_get_u16_be(data));
    return 0;
}

// service/subtype/payload
static int test_service_packet_parse(void)
{
    uint8_t payload[] = {0x01u, 0x02u, 0x03u};
    uint8_t packet[16];
    size_t packet_len = 0u;
    spacecan_packet_view_t view;

    ASSERT_EQ_STATUS(SPACECAN_OK,
                     spacecan_packet_build(SPACECAN_SERVICE_HOUSEKEEPING,
                                           SPACECAN_HK_SUBTYPE_REPORT,
                                           payload,
                                           sizeof(payload),
                                           packet,
                                           sizeof(packet),
                                           &packet_len));
    ASSERT_EQ_U32(5u, packet_len);
    ASSERT_EQ_STATUS(SPACECAN_OK, spacecan_packet_parse(packet, packet_len, &view));
    ASSERT_EQ_U32(SPACECAN_SERVICE_HOUSEKEEPING, view.service);
    ASSERT_EQ_U32(SPACECAN_HK_SUBTYPE_REPORT, view.subtype);
    ASSERT_EQ_U32(sizeof(payload), view.payload_len);
    ASSERT_TRUE(memcmp(payload, view.payload, sizeof(payload)) == 0);
    ASSERT_TRUE(strcmp("housekeeping", spacecan_service_name(view.service)) == 0);
    ASSERT_TRUE(strcmp("housekeeping.report", spacecan_subtype_name(view.service, view.subtype)) == 0);
    return 0;
}

// packet → one CAN frame → packet
static int test_single_frame_roundtrip(void)
{
    uint8_t packet[] = {SPACECAN_SERVICE_PARAMETER, SPACECAN_PARAMETER_SUBTYPE_GET, 0x10u};
    can_frame_t frames[1];
    size_t frame_count = 0u;
    spacecan_reassembly_t state;
    uint8_t out[SPACECAN_PACKET_MAX_SIZE];
    size_t out_len = 0u;

    ASSERT_EQ_STATUS(SPACECAN_OK,
                     spacecan_fragment_packet(SPACECAN_FRAME_REQUEST,
                                              1u,
                                              packet,
                                              sizeof(packet),
                                              frames,
                                              1u,
                                              &frame_count));
    ASSERT_EQ_U32(1u, frame_count);
    ASSERT_EQ_U32(0x601u, frames[0].id);
    ASSERT_EQ_U32(4u, frames[0].dlc);

    spacecan_reassembly_reset(&state);
    ASSERT_EQ_STATUS(SPACECAN_OK,
                     spacecan_reassembly_accept(&state,
                                                &frames[0],
                                                out,
                                                sizeof(out),
                                                &out_len));
    ASSERT_EQ_U32(sizeof(packet), out_len);
    ASSERT_TRUE(memcmp(packet, out, sizeof(packet)) == 0);
    return 0;
}

// long packet → several CAN frames → original packet
static int test_multi_frame_roundtrip(void)
{
    uint8_t payload[20];
    uint8_t packet[32];
    can_frame_t frames[8];
    uint8_t out[SPACECAN_PACKET_MAX_SIZE];
    size_t packet_len = 0u;
    size_t frame_count = 0u;
    size_t out_len = 0u;
    spacecan_reassembly_t state;
    size_t i;

    for (i = 0u; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(i + 1u);
    }

    ASSERT_EQ_STATUS(SPACECAN_OK,
                     spacecan_packet_build(SPACECAN_SERVICE_HOUSEKEEPING,
                                           SPACECAN_HK_SUBTYPE_REPORT,
                                           payload,
                                           sizeof(payload),
                                           packet,
                                           sizeof(packet),
                                           &packet_len));
    ASSERT_EQ_U32(22u, packet_len);
    ASSERT_EQ_STATUS(SPACECAN_OK,
                     spacecan_fragment_packet(SPACECAN_FRAME_REPLY,
                                              1u,
                                              packet,
                                              packet_len,
                                              frames,
                                              8u,
                                              &frame_count));
    ASSERT_EQ_U32(4u, frame_count);
    ASSERT_EQ_U32(0x581u, frames[0].id);
    ASSERT_EQ_U32(8u, frames[0].dlc);
    ASSERT_EQ_U32(0x40u, frames[0].data[0]);
    ASSERT_EQ_U32(packet_len, frames[0].data[1]);
    ASSERT_EQ_U32(0x81u, frames[1].data[0]);
    ASSERT_EQ_U32(0x82u, frames[2].data[0]);
    ASSERT_EQ_U32(0xC3u, frames[3].data[0]);

    spacecan_reassembly_reset(&state);
    for (i = 0u; i < frame_count; ++i) {
        spacecan_status_t expected = (i + 1u == frame_count) ? SPACECAN_OK : SPACECAN_ERR_IN_PROGRESS;
        ASSERT_EQ_STATUS(expected,
                         spacecan_reassembly_accept(&state,
                                                    &frames[i],
                                                    out,
                                                    sizeof(out),
                                                    &out_len));
    }
    ASSERT_EQ_U32(packet_len, out_len);
    ASSERT_TRUE(memcmp(packet, out, packet_len) == 0);
    return 0;
}

// Long packet → several CAN frames → original packet
static int test_sequence_error_resets_reassembly(void)
{
    uint8_t packet[16] = {SPACECAN_SERVICE_HOUSEKEEPING, SPACECAN_HK_SUBTYPE_REPORT,
                          1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u};
    can_frame_t frames[4];
    uint8_t out[SPACECAN_PACKET_MAX_SIZE];
    size_t frame_count = 0u;
    size_t out_len = 0u;
    spacecan_reassembly_t state;

    ASSERT_EQ_STATUS(SPACECAN_OK,
                     spacecan_fragment_packet(SPACECAN_FRAME_REPLY,
                                              1u,
                                              packet,
                                              12u,
                                              frames,
                                              4u,
                                              &frame_count));
    ASSERT_EQ_U32(2u, frame_count);

    frames[1].data[0] = 0xC2u;
    spacecan_reassembly_reset(&state);
    ASSERT_EQ_STATUS(SPACECAN_ERR_IN_PROGRESS,
                     spacecan_reassembly_accept(&state, &frames[0], out, sizeof(out), &out_len));
    ASSERT_EQ_STATUS(SPACECAN_ERR_SEQUENCE,
                     spacecan_reassembly_accept(&state, &frames[1], out, sizeof(out), &out_len));
    ASSERT_TRUE(!state.active);
    return 0;
}

int main(void)
{
    int failures = 0;

    failures += test_can_id_helpers();
    failures += test_integer_encoding();
    failures += test_service_packet_parse();
    failures += test_single_frame_roundtrip();
    failures += test_multi_frame_roundtrip();
    failures += test_sequence_error_resets_reassembly();

    if (failures == 0) {
        puts("spacecan codec tests passed");
    }
    return failures == 0 ? 0 : 1;
}
