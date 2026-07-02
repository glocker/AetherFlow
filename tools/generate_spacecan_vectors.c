#include "eps_simulator.h"
#include "spacecan.h"
#include "spacecan_services.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *description;
    spacecan_frame_class_t frame_class;
    uint8_t node_id;
    uint8_t service;
    uint8_t subtype;
    const uint8_t *payload;
    size_t payload_len;
} vector_case_t;

static void require_status(spacecan_status_t status, const char *operation)
{
    if (status != SPACECAN_OK) {
        fprintf(stderr, "%s failed with status %d\n", operation, (int)status);
        exit(1);
    }
}

static const char *frame_class_name(spacecan_frame_class_t frame_class)
{
    switch (frame_class) {
    case SPACECAN_FRAME_SYNC:
        return "sync";
    case SPACECAN_FRAME_HEARTBEAT:
        return "heartbeat";
    case SPACECAN_FRAME_REQUEST:
        return "request";
    case SPACECAN_FRAME_REPLY:
        return "reply";
    default:
        return "unknown";
    }
}

static void print_hex_bytes(const uint8_t *data, size_t len)
{
    size_t i;

    for (i = 0u; i < len; ++i) {
        printf("%02x", data[i]);
    }
}

static void print_json_string_hex_field(const char *name, const uint8_t *data, size_t len, bool comma)
{
    printf("      \"%s\": \"", name);
    print_hex_bytes(data, len);
    printf("\"%s\n", comma ? "," : "");
}

static void print_vector(const vector_case_t *test_case, bool comma)
{
    uint8_t packet[SPACECAN_PACKET_MAX_SIZE];
    can_frame_t frames[16];
    size_t packet_len = 0u;
    size_t frame_count = 0u;
    size_t i;

    require_status(spacecan_packet_build(test_case->service,
                                         test_case->subtype,
                                         test_case->payload,
                                         test_case->payload_len,
                                         packet,
                                         sizeof(packet),
                                         &packet_len),
                   "spacecan_packet_build");
    require_status(spacecan_fragment_packet(test_case->frame_class,
                                            test_case->node_id,
                                            packet,
                                            packet_len,
                                            frames,
                                            sizeof(frames) / sizeof(frames[0]),
                                            &frame_count),
                   "spacecan_fragment_packet");

    printf("    {\n");
    printf("      \"name\": \"%s\",\n", test_case->name);
    printf("      \"description\": \"%s\",\n", test_case->description);
    printf("      \"frame_class\": \"%s\",\n", frame_class_name(test_case->frame_class));
    printf("      \"node_id\": %u,\n", (unsigned)test_case->node_id);
    printf("      \"can_id\": \"0x%03x\",\n", (unsigned)spacecan_make_can_id(test_case->frame_class, test_case->node_id));
    printf("      \"service\": %u,\n", (unsigned)test_case->service);
    printf("      \"subtype\": %u,\n", (unsigned)test_case->subtype);
    print_json_string_hex_field("payload_hex", test_case->payload, test_case->payload_len, true);
    print_json_string_hex_field("packet_hex", packet, packet_len, true);
    printf("      \"frames\": [\n");
    for (i = 0u; i < frame_count; ++i) {
        printf("        {\n");
        printf("          \"id\": \"0x%03x\",\n", (unsigned)frames[i].id);
        printf("          \"dlc\": %u,\n", (unsigned)frames[i].dlc);
        printf("          \"data_hex\": \"");
        print_hex_bytes(frames[i].data, frames[i].dlc);
        printf("\"\n");
        printf("        }%s\n", (i + 1u < frame_count) ? "," : "");
    }
    printf("      ]\n");
    printf("    }%s\n", comma ? "," : "");
}

int main(void)
{
    static const uint8_t parameter_get_payload[] = {0x10u};
    static const uint8_t long_payload[] = {
        0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u, 0x09u, 0x0au,
        0x0bu, 0x0cu, 0x0du, 0x0eu, 0x0fu, 0x10u, 0x11u, 0x12u, 0x13u, 0x14u
    };
    uint8_t eps_payload[EPS_HOUSEKEEPING_PAYLOAD_LEN];
    size_t eps_payload_len = 0u;
    const eps_measurements_t measurements = {
        2u,
        5006u,
        430,
        84u,
        2416,
        0u,
    };
    vector_case_t cases[3];
    size_t i;

    require_status(eps_build_housekeeping_payload(&measurements,
                                                  EPS_STATE_OPERATIONAL,
                                                  eps_payload,
                                                  sizeof(eps_payload),
                                                  &eps_payload_len),
                   "eps_build_housekeeping_payload");

    cases[0].name = "parameter_get_request_single_frame";
    cases[0].description = "Small service 4/subtype 1 request from node 1; packet fits in one CAN frame.";
    cases[0].frame_class = SPACECAN_FRAME_REQUEST;
    cases[0].node_id = 1u;
    cases[0].service = SPACECAN_SERVICE_PARAMETER;
    cases[0].subtype = SPACECAN_PARAMETER_SUBTYPE_GET;
    cases[0].payload = parameter_get_payload;
    cases[0].payload_len = sizeof(parameter_get_payload);

    cases[1].name = "eps_housekeeping_reply_node_1";
    cases[1].description = "Deterministic EPS housekeeping report matching the Stage 3 simulator layout.";
    cases[1].frame_class = SPACECAN_FRAME_REPLY;
    cases[1].node_id = 1u;
    cases[1].service = SPACECAN_SERVICE_HOUSEKEEPING;
    cases[1].subtype = SPACECAN_HK_SUBTYPE_REPORT;
    cases[1].payload = eps_payload;
    cases[1].payload_len = eps_payload_len;

    cases[2].name = "housekeeping_long_payload_fragmentation";
    cases[2].description = "20-byte payload used to lock multi-frame fragmentation and reassembly semantics.";
    cases[2].frame_class = SPACECAN_FRAME_REPLY;
    cases[2].node_id = 1u;
    cases[2].service = SPACECAN_SERVICE_HOUSEKEEPING;
    cases[2].subtype = SPACECAN_HK_SUBTYPE_REPORT;
    cases[2].payload = long_payload;
    cases[2].payload_len = sizeof(long_payload);

    printf("{\n");
    printf("  \"schema\": \"aetherflow.spacecan.vectors.v1\",\n");
    printf("  \"dialect\": \"AetherFlow SpaceCAN dialect v1\",\n");
    printf("  \"producer\": \"tools/generate_spacecan_vectors.c\",\n");
    printf("  \"vectors\": [\n");
    for (i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        print_vector(&cases[i], i + 1u < sizeof(cases) / sizeof(cases[0]));
    }
    printf("  ]\n");
    printf("}\n");
    return 0;
}
