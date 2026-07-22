// We need define source to use nanosleep because of strict -std=c11
#define _POSIX_C_SOURCE 199309L

#include "spacecan.h"
#include "transport.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    keep_running = 0;
}

static unsigned long parse_rate_hz(int argc, char **argv)
{
    char *end = NULL;
    unsigned long value;

    if (argc < 2) {
        return 1u;
    }

    errno = 0;
    value = strtoul(argv[1], &end, 10);
    if (errno != 0 || end == argv[1] || *end != '\0' || value == 0u || value > 100u) {
        fprintf(stderr, "usage: %s [rate-hz]\n", argv[0]);
        return 0u;
    }
    return value;
}

static void sleep_for_rate(unsigned long rate_hz)
{
    struct timespec delay;
    delay.tv_sec = (time_t)(1u / rate_hz);
    delay.tv_nsec = (long)((1000000000ul / rate_hz) % 1000000000ul);
    if (delay.tv_sec == 0 && delay.tv_nsec == 0) {
        delay.tv_nsec = 10000000L;
    }
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR && keep_running) {
    }
}

int main(int argc, char **argv)
{
    udp_transport_t transport;
    can_frame_t sync_frame;
    unsigned long rate_hz = parse_rate_hz(argc, argv);
    unsigned long sequence = 0u;

    if (rate_hz == 0u) {
        return 2;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (!can_frame_init(&sync_frame,
                        spacecan_make_can_id(SPACECAN_FRAME_SYNC, SPACECAN_NODE_BROADCAST),
                        NULL,
                        0u,
                        false)) {
        fputs("controller_simulator: failed to create SYNC frame\n", stderr);
        return 1;
    }

    if (udp_transport_open(&transport, AETHERFLOW_UDP_GROUP, AETHERFLOW_UDP_PORT) != TRANSPORT_OK) {
        fprintf(stderr,
                "controller_simulator: failed to open UDP multicast bus %s:%u\n",
                AETHERFLOW_UDP_GROUP,
                (unsigned int)AETHERFLOW_UDP_PORT);
        return 1;
    }

    printf("controller_simulator: UDP multicast bus %s:%u rate=%luHz\n",
           AETHERFLOW_UDP_GROUP,
           (unsigned int)AETHERFLOW_UDP_PORT,
           rate_hz);

    while (keep_running) {
        if (udp_transport_send(&transport, &sync_frame) != TRANSPORT_OK) {
            fprintf(stderr, "controller_simulator: failed to send SYNC: %s\n", strerror(errno));
            udp_transport_close(&transport);
            return 1;
        }
        ++sequence;
        printf("controller_simulator: TX SYNC #%lu id=0x%03X\n", sequence, (unsigned int)sync_frame.id);
        fflush(stdout);
        sleep_for_rate(rate_hz);
    }

    udp_transport_close(&transport);
    puts("controller_simulator: stopped");
    return 0;
}
