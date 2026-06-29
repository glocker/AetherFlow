#define _DARWIN_C_SOURCE

#include "transport.h"

#include "can_frame_wire.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static struct sockaddr_in destination_addr(const udp_transport_t *transport)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = transport->destination.addr_be;
    addr.sin_port = transport->destination.port_be;
    return addr;
}

static void close_pair(int rx_fd, int tx_fd)
{
    if (rx_fd >= 0) {
        close(rx_fd);
    }
    if (tx_fd >= 0) {
        close(tx_fd);
    }
}

transport_status_t udp_transport_open(udp_transport_t *transport,
                                      const char *multicast_group,
                                      uint16_t port)
{
    int rx_fd = -1;
    int tx_fd = -1;
    int yes = 1;
    unsigned char ttl = 1u;
    unsigned char loop = 1u;
    struct sockaddr_in bind_addr;
    struct ip_mreq membership;
    struct in_addr group_addr;

    if (transport == NULL || multicast_group == NULL || port == 0u) {
        return TRANSPORT_ERR;
    }

    memset(transport, 0, sizeof(*transport));
    transport->fd = -1;
    transport->tx_fd = -1;

    if (inet_pton(AF_INET, multicast_group, &group_addr) != 1) {
        return TRANSPORT_ERR;
    }
    rx_fd = socket(AF_INET, SOCK_DGRAM, 0);
    tx_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (rx_fd < 0 || tx_fd < 0) {
        close_pair(rx_fd, tx_fd);
        return TRANSPORT_ERR;
    }
    if (fcntl(tx_fd, F_SETFL, O_NONBLOCK) != 0) {
        close_pair(rx_fd, tx_fd);
        return TRANSPORT_ERR;
    }

    if (setsockopt(rx_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0) {
        close_pair(rx_fd, tx_fd);
        return TRANSPORT_ERR;
    }
#ifdef SO_REUSEPORT
    if (setsockopt(rx_fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes)) != 0) {
        close_pair(rx_fd, tx_fd);
        return TRANSPORT_ERR;
    }
#endif

    if (setsockopt(tx_fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) != 0 ||
        setsockopt(tx_fd, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop)) != 0) {
        close_pair(rx_fd, tx_fd);
        return TRANSPORT_ERR;
    }

    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(port);
    if (bind(rx_fd, (const struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        close_pair(rx_fd, tx_fd);
        return TRANSPORT_ERR;
    }

    memset(&membership, 0, sizeof(membership));
    membership.imr_multiaddr = group_addr;
    membership.imr_interface.s_addr = htonl(INADDR_ANY);
    if (setsockopt(rx_fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership, sizeof(membership)) != 0) {
        close_pair(rx_fd, tx_fd);
        return TRANSPORT_ERR;
    }

    transport->fd = rx_fd;
    transport->tx_fd = tx_fd;
    transport->destination.addr_be = group_addr.s_addr;
    transport->destination.port_be = htons(port);
    return TRANSPORT_OK;
}

transport_status_t udp_transport_send(udp_transport_t *transport, const can_frame_t *frame)
{
    uint8_t wire[CAN_FRAME_WIRE_SIZE];
    size_t wire_len = 0u;
    struct sockaddr_in addr;
    ssize_t sent;

    if (transport == NULL || transport->tx_fd < 0 || frame == NULL) {
        return TRANSPORT_ERR;
    }
    if (!can_frame_wire_encode(frame, wire, sizeof(wire), &wire_len)) {
        return TRANSPORT_ERR;
    }

    addr = destination_addr(transport);
    sent = sendto(transport->tx_fd,
                  wire,
                  wire_len,
                  0,
                  (const struct sockaddr *)&addr,
                  sizeof(addr));
    return sent == (ssize_t)wire_len ? TRANSPORT_OK : TRANSPORT_ERR;
}

transport_status_t udp_transport_recv(udp_transport_t *transport, can_frame_t *frame, int timeout_ms)
{
    uint8_t wire[CAN_FRAME_WIRE_SIZE];
    fd_set read_fds;
    struct timeval timeout;
    struct timeval *timeout_ptr = NULL;
    ssize_t received;
    int ready;

    if (transport == NULL || transport->fd < 0 || frame == NULL) {
        return TRANSPORT_ERR;
    }

    FD_ZERO(&read_fds);
    FD_SET(transport->fd, &read_fds);
    if (timeout_ms >= 0) {
        timeout.tv_sec = timeout_ms / 1000;
        timeout.tv_usec = (timeout_ms % 1000) * 1000;
        timeout_ptr = &timeout;
    }

    do {
        ready = select(transport->fd + 1, &read_fds, NULL, NULL, timeout_ptr);
    } while (ready < 0 && errno == EINTR);

    if (ready == 0) {
        return TRANSPORT_TIMEOUT;
    }
    if (ready < 0) {
        return TRANSPORT_ERR;
    }

    received = recvfrom(transport->fd, wire, sizeof(wire), 0, NULL, NULL);
    if (received != (ssize_t)sizeof(wire)) {
        return TRANSPORT_ERR;
    }
    return can_frame_wire_decode(wire, sizeof(wire), frame) ? TRANSPORT_OK : TRANSPORT_ERR;
}

void udp_transport_close(udp_transport_t *transport)
{
    if (transport != NULL) {
        if (transport->fd >= 0) {
            close(transport->fd);
            transport->fd = -1;
        }
        if (transport->tx_fd >= 0) {
            close(transport->tx_fd);
            transport->tx_fd = -1;
        }
    }
}
