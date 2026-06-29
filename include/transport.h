#ifndef AETHERFLOW_TRANSPORT_H
#define AETHERFLOW_TRANSPORT_H

#include "can_frame.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AETHERFLOW_UDP_GROUP "224.0.0.1"
#define AETHERFLOW_UDP_PORT 40700u

typedef enum {
    TRANSPORT_OK = 0,
    TRANSPORT_ERR = -1,
    TRANSPORT_TIMEOUT = -2,
} transport_status_t;

typedef struct {
    int fd; // receive socket, intentionally public for select()-based services
    int tx_fd;
    struct {
        uint32_t addr_be;
        uint16_t port_be;
    } destination;
} udp_transport_t;

transport_status_t udp_transport_open(udp_transport_t *transport,
                                      const char *multicast_group,
                                      uint16_t port);
transport_status_t udp_transport_send(udp_transport_t *transport, const can_frame_t *frame);
transport_status_t udp_transport_recv(udp_transport_t *transport, can_frame_t *frame, int timeout_ms);
void udp_transport_close(udp_transport_t *transport);

#ifdef __cplusplus
}
#endif

#endif /* AETHERFLOW_TRANSPORT_H */
