#include "eps_simulator.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

static void print_frame(const can_frame_t *frame)
{
    uint8_t i;

    printf("TX REP id=0x%03X dlc=%u data=", (unsigned int)frame->id, (unsigned int)frame->dlc);
    for (i = 0u; i < frame->dlc; ++i) {
        printf("%02X", frame->data[i]);
        if ((uint8_t)(i + 1u) < frame->dlc) {
            putchar(' ');
        }
    }
    putchar('\n');
}

static unsigned long parse_sync_count(int argc, char **argv)
{
    char *end = NULL;
    unsigned long value;

    if (argc < 2) {
        return 5u;
    }

    errno = 0;
    value = strtoul(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' || value == 0u) {
        fprintf(stderr, "usage: %s [sync-count]\n", argv[0]);
        return 0u;
    }
    return value;
}

int main(int argc, char **argv)
{
    eps_simulator_t eps;
    can_frame_t sync_frame;
    unsigned long sync_count = parse_sync_count(argc, argv);
    unsigned long i;

    if (sync_count == 0u) {
        return 2;
    }

    eps_simulator_init(&eps, EPS_SIMULATOR_NODE_ID);

    // Stage 2 has no real transport yet, so this CLI creates synthetic SYNC frames
    if (!can_frame_init(&sync_frame, spacecan_make_can_id(SPACECAN_FRAME_SYNC, SPACECAN_NODE_BROADCAST), NULL, 0u, false)) {
        fputs("failed to create SYNC frame\n", stderr);
        return 1;
    }

    printf("EPS simulator node=%u initial_state=%s\n", (unsigned int)eps.node_id, eps_state_name(eps.state));

    for (i = 0u; i < sync_count; ++i) {
        can_frame_t reply_frames[EPS_MAX_REPLY_FRAMES];
        size_t reply_count = 0u;
        spacecan_status_t status;
        size_t frame_index;

        printf("RX SYNC #%lu\n", i + 1u);
        status = eps_simulator_accept_frame(&eps,
                                            &sync_frame,
                                            reply_frames,
                                            EPS_MAX_REPLY_FRAMES,
                                            &reply_count);
        if (status != SPACECAN_OK) {
            fprintf(stderr, "EPS error: %d\n", (int)status);
            return 1;
        }

        printf("state=%s measurements_seq=%u reply_frames=%u\n",
               eps_state_name(eps.state),
               (unsigned int)eps.last_measurements.sequence,
               (unsigned int)reply_count);
        for (frame_index = 0u; frame_index < reply_count; ++frame_index) {
            print_frame(&reply_frames[frame_index]);
        }
    }

    return 0;
}
