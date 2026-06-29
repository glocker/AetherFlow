#include "eps_simulator.h"
#include "transport.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}

static void print_frame(const char *prefix, const can_frame_t *frame)
{
    uint8_t i;

    printf("%s id=0x%03X dlc=%u data=", prefix, (unsigned int)frame->id, (unsigned int)frame->dlc);
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
        return 0u;
    }

    errno = 0;
    value = strtoul(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' || value == 0u) {
        return 0u;
    }
    return value;
}

static int run_synthetic(unsigned long sync_count)
{
    eps_simulator_t eps;
    can_frame_t sync_frame;
    unsigned long i;

    eps_simulator_init(&eps, EPS_SIMULATOR_NODE_ID);
    if (!can_frame_init(&sync_frame, spacecan_make_can_id(SPACECAN_FRAME_SYNC, SPACECAN_NODE_BROADCAST), NULL, 0u, false)) {
        fputs("failed to create SYNC frame\n", stderr);
        return 1;
    }

    printf("EPS simulator synthetic node=%u initial_state=%s\n", (unsigned int)eps.node_id, eps_state_name(eps.state));

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
            print_frame("TX REP", &reply_frames[frame_index]);
        }
    }

    return 0;
}

static int run_udp(void)
{
    eps_simulator_t eps;
    udp_transport_t transport;

    eps_simulator_init(&eps, EPS_SIMULATOR_NODE_ID);
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (udp_transport_open(&transport, AETHERFLOW_UDP_GROUP, AETHERFLOW_UDP_PORT) != TRANSPORT_OK) {
        fprintf(stderr,
                "eps_simulator: failed to open UDP multicast bus %s:%u\n",
                AETHERFLOW_UDP_GROUP,
                (unsigned int)AETHERFLOW_UDP_PORT);
        return 1;
    }

    printf("eps_simulator: node=%u state=%s UDP multicast bus %s:%u\n",
           (unsigned int)eps.node_id,
           eps_state_name(eps.state),
           AETHERFLOW_UDP_GROUP,
           (unsigned int)AETHERFLOW_UDP_PORT);

    while (keep_running) {
        can_frame_t incoming;
        can_frame_t reply_frames[EPS_MAX_REPLY_FRAMES];
        size_t reply_count = 0u;
        size_t i;
        transport_status_t recv_status = udp_transport_recv(&transport, &incoming, 500);

        if (recv_status == TRANSPORT_TIMEOUT) {
            continue;
        }
        if (recv_status != TRANSPORT_OK) {
            fputs("eps_simulator: failed to receive frame\n", stderr);
            udp_transport_close(&transport);
            return 1;
        }

        if (eps_simulator_accept_frame(&eps,
                                       &incoming,
                                       reply_frames,
                                       EPS_MAX_REPLY_FRAMES,
                                       &reply_count) != SPACECAN_OK) {
            continue;
        }

        printf("eps_simulator: RX SYNC state=%s seq=%u reply_frames=%u\n",
               eps_state_name(eps.state),
               (unsigned int)eps.last_measurements.sequence,
               (unsigned int)reply_count);
        for (i = 0u; i < reply_count; ++i) {
            if (udp_transport_send(&transport, &reply_frames[i]) != TRANSPORT_OK) {
                fputs("eps_simulator: failed to send reply frame\n", stderr);
                udp_transport_close(&transport);
                return 1;
            }
            print_frame("eps_simulator: TX REP", &reply_frames[i]);
        }
        fflush(stdout);
    }

    udp_transport_close(&transport);
    puts("eps_simulator: stopped");
    return 0;
}

int main(int argc, char **argv)
{
    unsigned long sync_count = parse_sync_count(argc, argv);

    if (argc >= 2 && sync_count == 0u) {
        fprintf(stderr, "usage: %s [synthetic-sync-count]\n", argv[0]);
        return 2;
    }
    if (sync_count > 0u) {
        return run_synthetic(sync_count);
    }
    return run_udp();
}
