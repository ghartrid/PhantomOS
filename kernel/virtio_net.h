/*
 * PhantomOS VirtIO Network Driver
 * "To Create, Not To Destroy"
 *
 * VirtIO-net PCI driver with minimal network stack (ARP + ICMP).
 * Static IP 10.0.2.15/24, gateway 10.0.2.2 (QEMU user-mode defaults).
 */

#ifndef PHANTOMOS_VIRTIO_NET_H
#define PHANTOMOS_VIRTIO_NET_H

#include <stdint.h>
#include <stddef.h>

struct net_stats {
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t arp_replies_sent;
    uint64_t icmp_replies_sent;
    uint64_t ping_sent;
    uint64_t ping_received;
};

int virtio_net_init(void);
int virtio_net_available(void);
const uint8_t *virtio_net_get_mac(void);
int virtio_net_link_up(void);
const char *virtio_net_get_ip(void);
const struct net_stats *virtio_net_get_stats(void);
void virtio_net_poll(void);
int virtio_net_ping(uint32_t dest_ip, uint16_t seq);
int virtio_net_ping_check(void);
void virtio_net_dump_info(void);

#endif /* PHANTOMOS_VIRTIO_NET_H */
