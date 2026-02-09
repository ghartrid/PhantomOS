/*
 * PhantomOS VirtIO Network Driver
 * "To Create, Not To Destroy"
 *
 * VirtIO-net PCI driver with minimal network stack:
 *   - ARP: respond to requests, resolve gateway MAC
 *   - ICMP: respond to echo requests (ping), send echo requests
 *   - Static IP: 10.0.2.15/24, gateway 10.0.2.2 (QEMU user-mode defaults)
 *
 * Uses the same VirtIO PCI transport as virtio_console.c:
 *   1. Detect PCI device (0x1AF4/0x1000 transitional or 0x1AF4/0x1041 modern)
 *   2. Walk PCI capabilities for Common/Notify/ISR/Device config
 *   3. Set up receiveq (queue 0) and transmitq (queue 1)
 *   4. Pre-fill receive descriptors, transmit on demand
 */

#include "virtio_net.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "io.h"
#include "timer.h"
#include <stdint.h>
#include <stddef.h>

/*============================================================================
 * External Declarations
 *============================================================================*/

extern int kprintf(const char *fmt, ...);
extern void *memset(void *s, int c, size_t n);
extern void *memcpy(void *dest, const void *src, size_t n);
extern int strcmp(const char *s1, const char *s2);

/*============================================================================
 * Constants
 *============================================================================*/

#define VIRTIO_NET_DEVICE_ID        0x1000  /* Transitional */
#define VIRTIO_NET_DEVICE_ID_V1     0x1041  /* Modern (0x1040+1) */
#define VIRTIO_VENDOR_ID            0x1AF4

#define VNET_QUEUE_SIZE     64      /* Virtqueue entries */
#define VNET_RX_BUF_SIZE    1526    /* 10 virtio hdr + 14 eth + 1500 MTU + 2 pad */

/* VirtIO PCI capability types */
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4

/* VirtIO device status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_FEATURES_OK   8
#define VIRTIO_STATUS_DRIVER_OK     4

/* Virtqueue descriptor flags */
#define VIRTQ_DESC_F_NEXT       1
#define VIRTQ_DESC_F_WRITE      2   /* Device writes (for receive) */

/* PCI capability list */
#define PCI_REG_CAP_PTR     0x34
#define PCI_REG_STATUS_CAP  0x10

/* VMM page flags */
#define PTE_PRESENT     (1ULL << 0)
#define PTE_WRITABLE    (1ULL << 1)
#define PTE_NOCACHE     (1ULL << 4)
#define PTE_WRITETHROUGH (1ULL << 3)

/* VirtIO net feature bits */
#define VIRTIO_NET_F_MAC        (1U << 5)
#define VIRTIO_NET_F_STATUS     (1U << 16)

/* Ethernet */
#define ETH_ALEN            6
#define ETH_HLEN            14
#define ETH_TYPE_ARP        0x0806
#define ETH_TYPE_IPV4       0x0800

/* ARP */
#define ARP_HW_ETHER        1
#define ARP_OP_REQUEST      1
#define ARP_OP_REPLY        2

/* IP / ICMP */
#define IP_PROTO_ICMP       1
#define ICMP_ECHO_REPLY     0
#define ICMP_ECHO_REQUEST   8

/* VirtIO net header size */
#define VIRTIO_NET_HDR_SIZE 10

/*============================================================================
 * Byte-Order Helpers (x86 is little-endian, network is big-endian)
 *============================================================================*/

static inline uint16_t htons(uint16_t x)
{
    return (uint16_t)((x >> 8) | (x << 8));
}
static inline uint16_t ntohs(uint16_t x) { return htons(x); }

static inline uint32_t htonl(uint32_t x)
{
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) |
           ((x & 0xFF0000) >> 8) | ((x & 0xFF000000) >> 24);
}
static inline uint32_t ntohl(uint32_t x) { return htonl(x); }

/*============================================================================
 * IP Checksum
 *============================================================================*/

static uint16_t ip_checksum(const void *data, size_t len)
{
    const uint16_t *words = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) {
        sum += *words++;
        len -= 2;
    }
    if (len == 1)
        sum += *(const uint8_t *)words;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

/*============================================================================
 * Network Protocol Structures
 *============================================================================*/

struct virtio_net_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} __attribute__((packed));

struct eth_hdr {
    uint8_t  dst[ETH_ALEN];
    uint8_t  src[ETH_ALEN];
    uint16_t ethertype;
} __attribute__((packed));

struct arp_pkt {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[ETH_ALEN];
    uint32_t sender_ip;
    uint8_t  target_mac[ETH_ALEN];
    uint32_t target_ip;
} __attribute__((packed));

struct ipv4_hdr {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t total_len;
    uint16_t identification;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed));

struct icmp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

/*============================================================================
 * Virtqueue Structures (duplicated from virtio_console.c)
 *============================================================================*/

struct virtq_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct virtq_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VNET_QUEUE_SIZE];
    uint16_t used_event;
} __attribute__((packed));

struct virtq_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct virtq_used {
    uint16_t flags;
    uint16_t idx;
    struct virtq_used_elem ring[VNET_QUEUE_SIZE];
    uint16_t avail_event;
} __attribute__((packed));

struct virtio_pci_common_cfg {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint64_t queue_desc;
    uint64_t queue_avail;
    uint64_t queue_used;
} __attribute__((packed));

/*============================================================================
 * Driver State
 *============================================================================*/

static struct {
    int                     detected;
    int                     initialized;
    const struct pci_device *pci_dev;

    /* MMIO-mapped VirtIO config structures */
    volatile struct virtio_pci_common_cfg *common_cfg;
    volatile uint8_t       *isr_cfg;
    volatile uint8_t       *device_cfg;
    volatile uint16_t      *notify_base;
    uint32_t                notify_off_multiplier;

    /* Receiveq (virtqueue 0) */
    struct virtq_desc      *rx_desc;
    struct virtq_avail     *rx_avail;
    struct virtq_used      *rx_used;
    uint16_t                rx_last_used;
    uint16_t                rx_notify_off;

    /* Transmitq (virtqueue 1) */
    struct virtq_desc      *tx_desc;
    struct virtq_avail     *tx_avail;
    struct virtq_used      *tx_used;
    uint16_t                tx_free_head;
    uint16_t                tx_last_used;
    uint16_t                tx_notify_off;

    /* Receive buffers */
    uint8_t                *rx_bufs;    /* VNET_QUEUE_SIZE * VNET_RX_BUF_SIZE */

    /* Transmit buffer */
    uint8_t                *tx_buf;     /* Single page for transmit data */

    /* Device info */
    uint8_t                 mac[6];
    uint32_t                ip;         /* Host byte order */
    uint32_t                gateway;
    uint32_t                netmask;

    /* ARP cache (gateway only) */
    uint8_t                 gateway_mac[6];
    int                     gateway_mac_known;

    /* Ping state */
    uint16_t                ping_id;
    uint16_t                ping_seq;
    uint64_t                ping_send_time_ms;
    int                     ping_reply_received;
    int                     ping_rtt_ms;

    /* Statistics */
    struct net_stats        stats;
} vnet;

/*============================================================================
 * PCI Capability Walking (same pattern as virtio_console.c)
 *============================================================================*/

static int find_virtio_caps(void)
{
    uint8_t bus = vnet.pci_dev->bus;
    uint8_t dev = vnet.pci_dev->device;
    uint8_t func = vnet.pci_dev->function;

    uint16_t status = pci_config_read16(bus, dev, func, 0x06);
    if (!(status & PCI_REG_STATUS_CAP)) {
        kprintf("[VirtIO Net] No PCI capabilities\n");
        return -1;
    }

    uint8_t cap_ptr = pci_config_read8(bus, dev, func, PCI_REG_CAP_PTR);
    cap_ptr &= 0xFC;

    int found_common = 0, found_notify = 0;

    while (cap_ptr) {
        uint8_t cap_id   = pci_config_read8(bus, dev, func, cap_ptr);
        uint8_t cap_next = pci_config_read8(bus, dev, func, cap_ptr + 1);

        if (cap_id == 0x09) {  /* VirtIO vendor capability */
            uint8_t cfg_type = pci_config_read8(bus, dev, func, cap_ptr + 3);
            uint8_t bar_idx  = pci_config_read8(bus, dev, func, cap_ptr + 4);
            uint32_t offset  = pci_config_read32(bus, dev, func, cap_ptr + 8);
            uint32_t length  = pci_config_read32(bus, dev, func, cap_ptr + 12);

            uint64_t bar_base = vnet.pci_dev->bar_addr[bar_idx];
            if (bar_base == 0) {
                cap_ptr = cap_next;
                continue;
            }

            /* Map the BAR region */
            uint64_t map_addr = bar_base + offset;
            uint64_t map_pages = (length + 4095) / 4096;
            for (uint64_t p = 0; p < map_pages; p++) {
                uint64_t page = (map_addr + p * 4096) & ~0xFFFULL;
                vmm_map_page(page, page,
                             PTE_PRESENT | PTE_WRITABLE |
                             PTE_NOCACHE | PTE_WRITETHROUGH);
            }

            volatile void *mapped = (volatile void *)(uintptr_t)(bar_base + offset);

            switch (cfg_type) {
            case VIRTIO_PCI_CAP_COMMON_CFG:
                vnet.common_cfg = (volatile struct virtio_pci_common_cfg *)mapped;
                found_common = 1;
                break;
            case VIRTIO_PCI_CAP_NOTIFY_CFG:
                vnet.notify_base = (volatile uint16_t *)mapped;
                vnet.notify_off_multiplier =
                    pci_config_read32(bus, dev, func, cap_ptr + 16);
                found_notify = 1;
                break;
            case VIRTIO_PCI_CAP_ISR_CFG:
                vnet.isr_cfg = (volatile uint8_t *)mapped;
                break;
            case VIRTIO_PCI_CAP_DEVICE_CFG:
                vnet.device_cfg = (volatile uint8_t *)mapped;
                break;
            }
        }

        cap_ptr = cap_next;
    }

    if (!found_common || !found_notify) {
        kprintf("[VirtIO Net] Missing required capabilities\n");
        return -1;
    }
    return 0;
}

/*============================================================================
 * Virtqueue Setup
 *============================================================================*/

static int setup_virtqueue(int queue_idx,
                           struct virtq_desc **out_desc,
                           struct virtq_avail **out_avail,
                           struct virtq_used **out_used,
                           uint16_t *out_notify_off)
{
    volatile struct virtio_pci_common_cfg *cfg = vnet.common_cfg;

    cfg->queue_select = (uint16_t)queue_idx;
    __asm__ volatile("mfence" ::: "memory");

    uint16_t max_size = cfg->queue_size;
    if (max_size == 0) return -1;
    if (max_size > VNET_QUEUE_SIZE)
        max_size = VNET_QUEUE_SIZE;
    cfg->queue_size = max_size;

    void *vq_mem = pmm_alloc_pages(2);
    if (!vq_mem) return -1;
    memset(vq_mem, 0, 8192);

    uint64_t vq_phys = (uint64_t)(uintptr_t)vq_mem;

    *out_desc = (struct virtq_desc *)vq_mem;
    *out_avail = (struct virtq_avail *)((uint8_t *)vq_mem +
                  max_size * sizeof(struct virtq_desc));

    uint64_t used_offset = max_size * sizeof(struct virtq_desc) +
                           sizeof(struct virtq_avail);
    used_offset = (used_offset + 0xFFF) & ~0xFFFULL;
    *out_used = (struct virtq_used *)((uint8_t *)vq_mem + used_offset);

    for (uint16_t i = 0; i < max_size - 1; i++)
        (*out_desc)[i].next = i + 1;
    (*out_desc)[max_size - 1].next = 0xFFFF;

    *out_notify_off = cfg->queue_notify_off;

    cfg->queue_desc  = vq_phys;
    cfg->queue_avail = vq_phys +
                       (uint64_t)((uint8_t *)*out_avail - (uint8_t *)vq_mem);
    cfg->queue_used  = vq_phys + used_offset;
    __asm__ volatile("mfence" ::: "memory");

    cfg->queue_enable = 1;
    __asm__ volatile("mfence" ::: "memory");

    return 0;
}

/*============================================================================
 * Queue Notification
 *============================================================================*/

static void kick_queue(uint16_t notify_off, uint16_t queue_idx)
{
    __asm__ volatile("mfence" ::: "memory");
    volatile uint16_t *notify_addr = (volatile uint16_t *)
        ((uint8_t *)vnet.notify_base +
         (uint32_t)notify_off * vnet.notify_off_multiplier);
    *notify_addr = queue_idx;
}

/*============================================================================
 * Raw Transmit
 *============================================================================*/

static int virtio_net_send_raw(const void *data, uint32_t len)
{
    uint16_t idx = vnet.tx_free_head;
    if (idx == 0xFFFF) return -1;
    vnet.tx_free_head = vnet.tx_desc[idx].next;

    memcpy(vnet.tx_buf, data, len);

    vnet.tx_desc[idx].addr = (uint64_t)(uintptr_t)vnet.tx_buf;
    vnet.tx_desc[idx].len = len;
    vnet.tx_desc[idx].flags = 0;  /* Device reads */
    vnet.tx_desc[idx].next = 0xFFFF;

    uint16_t avail_idx = vnet.tx_avail->idx;
    vnet.tx_avail->ring[avail_idx % VNET_QUEUE_SIZE] = idx;
    __asm__ volatile("mfence" ::: "memory");
    vnet.tx_avail->idx = avail_idx + 1;

    kick_queue(vnet.tx_notify_off, 1);

    for (int i = 0; i < 1000000; i++) {
        if (vnet.tx_used->idx != vnet.tx_last_used) {
            vnet.tx_last_used = vnet.tx_used->idx;
            vnet.tx_desc[idx].next = vnet.tx_free_head;
            vnet.tx_free_head = idx;
            vnet.stats.tx_packets++;
            vnet.stats.tx_bytes += len;
            return 0;
        }
        __asm__ volatile("pause" ::: "memory");
    }

    /* Timeout: reclaim descriptor anyway */
    vnet.tx_desc[idx].next = vnet.tx_free_head;
    vnet.tx_free_head = idx;
    return -1;
}

/*============================================================================
 * Packet Construction Helpers
 *============================================================================*/

static int send_eth_frame(const uint8_t *dst_mac, uint16_t ethertype,
                          const void *payload, uint32_t payload_len)
{
    uint8_t pkt[1600];
    uint32_t offset = 0;

    /* VirtIO net header (all zeros = no offloading) */
    struct virtio_net_hdr *vhdr = (struct virtio_net_hdr *)pkt;
    memset(vhdr, 0, VIRTIO_NET_HDR_SIZE);
    offset += VIRTIO_NET_HDR_SIZE;

    /* Ethernet header */
    struct eth_hdr *eth = (struct eth_hdr *)(pkt + offset);
    memcpy(eth->dst, dst_mac, ETH_ALEN);
    memcpy(eth->src, vnet.mac, ETH_ALEN);
    eth->ethertype = htons(ethertype);
    offset += ETH_HLEN;

    if (offset + payload_len > sizeof(pkt)) return -1;
    memcpy(pkt + offset, payload, payload_len);
    offset += payload_len;

    return virtio_net_send_raw(pkt, offset);
}

/*============================================================================
 * ARP
 *============================================================================*/

static void send_arp_reply(const uint8_t *target_mac, uint32_t target_ip_host)
{
    struct arp_pkt arp;
    memset(&arp, 0, sizeof(arp));
    arp.hw_type = htons(ARP_HW_ETHER);
    arp.proto_type = htons(ETH_TYPE_IPV4);
    arp.hw_len = ETH_ALEN;
    arp.proto_len = 4;
    arp.opcode = htons(ARP_OP_REPLY);
    memcpy(arp.sender_mac, vnet.mac, ETH_ALEN);
    arp.sender_ip = htonl(vnet.ip);
    memcpy(arp.target_mac, target_mac, ETH_ALEN);
    arp.target_ip = htonl(target_ip_host);

    send_eth_frame(target_mac, ETH_TYPE_ARP, &arp, sizeof(arp));
    vnet.stats.arp_replies_sent++;
}

static void send_arp_request(uint32_t target_ip_host)
{
    static const uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    struct arp_pkt arp;
    memset(&arp, 0, sizeof(arp));
    arp.hw_type = htons(ARP_HW_ETHER);
    arp.proto_type = htons(ETH_TYPE_IPV4);
    arp.hw_len = ETH_ALEN;
    arp.proto_len = 4;
    arp.opcode = htons(ARP_OP_REQUEST);
    memcpy(arp.sender_mac, vnet.mac, ETH_ALEN);
    arp.sender_ip = htonl(vnet.ip);
    memset(arp.target_mac, 0, ETH_ALEN);
    arp.target_ip = htonl(target_ip_host);

    send_eth_frame(broadcast, ETH_TYPE_ARP, &arp, sizeof(arp));
}

/*============================================================================
 * ICMP
 *============================================================================*/

static void send_icmp_echo_reply(uint32_t dst_ip_host, const uint8_t *dst_mac,
                                  uint16_t id_net, uint16_t seq_net,
                                  const void *data, uint32_t data_len)
{
    uint8_t pkt[1600];
    uint32_t offset = 0;

    memset(pkt, 0, VIRTIO_NET_HDR_SIZE);
    offset += VIRTIO_NET_HDR_SIZE;

    struct eth_hdr *eth = (struct eth_hdr *)(pkt + offset);
    memcpy(eth->dst, dst_mac, ETH_ALEN);
    memcpy(eth->src, vnet.mac, ETH_ALEN);
    eth->ethertype = htons(ETH_TYPE_IPV4);
    offset += ETH_HLEN;

    uint32_t ip_payload_len = (uint32_t)sizeof(struct icmp_hdr) + data_len;
    struct ipv4_hdr *ip = (struct ipv4_hdr *)(pkt + offset);
    memset(ip, 0, sizeof(*ip));
    ip->ver_ihl = 0x45;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_ICMP;
    ip->total_len = htons((uint16_t)(20 + ip_payload_len));
    ip->src_ip = htonl(vnet.ip);
    ip->dst_ip = htonl(dst_ip_host);
    ip->checksum = 0;
    ip->checksum = ip_checksum(ip, 20);
    offset += 20;

    struct icmp_hdr *icmp = (struct icmp_hdr *)(pkt + offset);
    icmp->type = ICMP_ECHO_REPLY;
    icmp->code = 0;
    icmp->id = id_net;      /* Already network byte order */
    icmp->seq = seq_net;    /* Already network byte order */
    icmp->checksum = 0;
    offset += (uint32_t)sizeof(struct icmp_hdr);

    if (data_len > 0 && offset + data_len <= sizeof(pkt)) {
        memcpy(pkt + offset, data, data_len);
        offset += data_len;
    }

    icmp->checksum = ip_checksum(icmp, (size_t)sizeof(struct icmp_hdr) + data_len);

    virtio_net_send_raw(pkt, offset);
    vnet.stats.icmp_replies_sent++;
}

static int send_icmp_echo_request(uint32_t dst_ip_host, uint16_t id, uint16_t seq)
{
    if (!vnet.gateway_mac_known) return -1;

    uint8_t pkt[1600];
    uint32_t offset = 0;

    memset(pkt, 0, VIRTIO_NET_HDR_SIZE);
    offset += VIRTIO_NET_HDR_SIZE;

    struct eth_hdr *eth = (struct eth_hdr *)(pkt + offset);
    memcpy(eth->dst, vnet.gateway_mac, ETH_ALEN);
    memcpy(eth->src, vnet.mac, ETH_ALEN);
    eth->ethertype = htons(ETH_TYPE_IPV4);
    offset += ETH_HLEN;

    struct ipv4_hdr *ip = (struct ipv4_hdr *)(pkt + offset);
    memset(ip, 0, sizeof(*ip));
    ip->ver_ihl = 0x45;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_ICMP;
    ip->total_len = htons(20 + 8);  /* IP + ICMP echo (no payload) */
    ip->src_ip = htonl(vnet.ip);
    ip->dst_ip = htonl(dst_ip_host);
    ip->checksum = 0;
    ip->checksum = ip_checksum(ip, 20);
    offset += 20;

    struct icmp_hdr *icmp = (struct icmp_hdr *)(pkt + offset);
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->id = htons(id);
    icmp->seq = htons(seq);
    icmp->checksum = 0;
    icmp->checksum = ip_checksum(icmp, sizeof(struct icmp_hdr));
    offset += (uint32_t)sizeof(struct icmp_hdr);

    vnet.stats.ping_sent++;
    return virtio_net_send_raw(pkt, offset);
}

/*============================================================================
 * Receive Processing
 *============================================================================*/

static void process_arp(const uint8_t *pkt_start, uint32_t len)
{
    if (len < sizeof(struct arp_pkt)) return;
    const struct arp_pkt *arp = (const struct arp_pkt *)pkt_start;

    uint16_t op = ntohs(arp->opcode);
    uint32_t target_ip = ntohl(arp->target_ip);
    uint32_t sender_ip = ntohl(arp->sender_ip);

    if (op == ARP_OP_REQUEST && target_ip == vnet.ip) {
        send_arp_reply(arp->sender_mac, sender_ip);
    }
    else if (op == ARP_OP_REPLY && sender_ip == vnet.gateway) {
        memcpy(vnet.gateway_mac, arp->sender_mac, ETH_ALEN);
        vnet.gateway_mac_known = 1;
    }
}

static void process_icmp(const struct ipv4_hdr *ip,
                         const uint8_t *icmp_data, uint32_t icmp_len,
                         const uint8_t *src_mac)
{
    if (icmp_len < sizeof(struct icmp_hdr)) return;
    const struct icmp_hdr *icmp = (const struct icmp_hdr *)icmp_data;

    if (icmp->type == ICMP_ECHO_REQUEST && icmp->code == 0) {
        uint32_t data_len = icmp_len - (uint32_t)sizeof(struct icmp_hdr);
        const void *data = (data_len > 0) ?
            (icmp_data + sizeof(struct icmp_hdr)) : NULL;
        send_icmp_echo_reply(ntohl(ip->src_ip), src_mac,
                              icmp->id, icmp->seq, data, data_len);
    }
    else if (icmp->type == ICMP_ECHO_REPLY && icmp->code == 0) {
        if (ntohs(icmp->id) == vnet.ping_id) {
            vnet.ping_reply_received = 1;
            vnet.ping_rtt_ms = (int)(timer_get_ms() - vnet.ping_send_time_ms);
            vnet.stats.ping_received++;
        }
    }
}

static void process_ipv4(const uint8_t *pkt_start, uint32_t len,
                         const uint8_t *src_mac)
{
    if (len < 20) return;
    const struct ipv4_hdr *ip = (const struct ipv4_hdr *)pkt_start;

    uint8_t ihl = (uint8_t)((ip->ver_ihl & 0x0F) * 4);
    if (ihl < 20 || len < ihl) return;

    uint32_t dst = ntohl(ip->dst_ip);
    if (dst != vnet.ip && dst != 0xFFFFFFFF) return;

    const uint8_t *payload = pkt_start + ihl;
    uint32_t payload_len = ntohs(ip->total_len) - ihl;
    if (payload_len > len - ihl) payload_len = len - ihl;

    if (ip->protocol == IP_PROTO_ICMP)
        process_icmp(ip, payload, payload_len, src_mac);
}

static void process_packet(const uint8_t *raw, uint32_t total_len)
{
    if (total_len < VIRTIO_NET_HDR_SIZE + ETH_HLEN) return;

    const uint8_t *eth_start = raw + VIRTIO_NET_HDR_SIZE;
    uint32_t eth_len = total_len - VIRTIO_NET_HDR_SIZE;

    const struct eth_hdr *eth = (const struct eth_hdr *)eth_start;
    uint16_t ethertype = ntohs(eth->ethertype);

    const uint8_t *payload = eth_start + ETH_HLEN;
    uint32_t payload_len = eth_len - ETH_HLEN;

    switch (ethertype) {
    case ETH_TYPE_ARP:
        process_arp(payload, payload_len);
        break;
    case ETH_TYPE_IPV4:
        process_ipv4(payload, payload_len, eth->src);
        break;
    }
}

/*============================================================================
 * Poll (call from event loop)
 *============================================================================*/

void virtio_net_poll(void)
{
    if (!vnet.initialized) return;

    int requeued = 0;

    while (vnet.rx_used->idx != vnet.rx_last_used) {
        uint16_t used_idx = vnet.rx_last_used % VNET_QUEUE_SIZE;
        uint32_t desc_id = vnet.rx_used->ring[used_idx].id;
        uint32_t data_len = vnet.rx_used->ring[used_idx].len;

        uint8_t *rx_data = vnet.rx_bufs + desc_id * VNET_RX_BUF_SIZE;

        vnet.stats.rx_packets++;
        vnet.stats.rx_bytes += data_len;

        process_packet(rx_data, data_len);

        /* Re-queue the descriptor */
        vnet.rx_desc[desc_id].len = VNET_RX_BUF_SIZE;
        vnet.rx_desc[desc_id].flags = VIRTQ_DESC_F_WRITE;

        uint16_t avail_idx = vnet.rx_avail->idx;
        vnet.rx_avail->ring[avail_idx % VNET_QUEUE_SIZE] = (uint16_t)desc_id;
        __asm__ volatile("mfence" ::: "memory");
        vnet.rx_avail->idx = avail_idx + 1;

        vnet.rx_last_used++;
        requeued = 1;
    }

    if (requeued)
        kick_queue(vnet.rx_notify_off, 0);
}

/*============================================================================
 * Initialization
 *============================================================================*/

static void read_mac_from_device(void)
{
    if (vnet.device_cfg) {
        for (int i = 0; i < 6; i++)
            vnet.mac[i] = vnet.device_cfg[i];
    }
}

int virtio_net_init(void)
{
    memset(&vnet, 0, sizeof(vnet));

    const struct pci_device *dev = pci_find_by_id(VIRTIO_VENDOR_ID,
                                                   VIRTIO_NET_DEVICE_ID);
    if (!dev)
        dev = pci_find_by_id(VIRTIO_VENDOR_ID, VIRTIO_NET_DEVICE_ID_V1);
    if (!dev) return -1;

    vnet.pci_dev = dev;
    vnet.detected = 1;
    kprintf("[VirtIO Net] Found: vendor 0x%x device 0x%x\n",
            dev->vendor_id, dev->device_id);

    pci_enable_bus_master(dev);
    pci_enable_memory_space(dev);

    if (find_virtio_caps() != 0) return -1;

    volatile struct virtio_pci_common_cfg *cfg = vnet.common_cfg;

    /* Reset */
    cfg->device_status = 0;
    __asm__ volatile("mfence" ::: "memory");

    /* Acknowledge + Driver */
    cfg->device_status = VIRTIO_STATUS_ACKNOWLEDGE;
    __asm__ volatile("mfence" ::: "memory");
    cfg->device_status |= VIRTIO_STATUS_DRIVER;
    __asm__ volatile("mfence" ::: "memory");

    /* Feature negotiation */
    cfg->device_feature_select = 0;
    __asm__ volatile("mfence" ::: "memory");
    uint32_t dev_features = cfg->device_feature;

    uint32_t our_features = 0;
    if (dev_features & VIRTIO_NET_F_MAC)
        our_features |= VIRTIO_NET_F_MAC;
    if (dev_features & VIRTIO_NET_F_STATUS)
        our_features |= VIRTIO_NET_F_STATUS;

    cfg->driver_feature_select = 0;
    cfg->driver_feature = our_features;
    __asm__ volatile("mfence" ::: "memory");

    cfg->device_status |= VIRTIO_STATUS_FEATURES_OK;
    __asm__ volatile("mfence" ::: "memory");

    if (!(cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
        kprintf("[VirtIO Net] Feature negotiation failed\n");
        cfg->device_status = 0;
        return -1;
    }

    /* Read MAC address */
    read_mac_from_device();
    kprintf("[VirtIO Net] MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
            vnet.mac[0], vnet.mac[1], vnet.mac[2],
            vnet.mac[3], vnet.mac[4], vnet.mac[5]);

    /* Set up receiveq (queue 0) */
    if (setup_virtqueue(0, &vnet.rx_desc, &vnet.rx_avail, &vnet.rx_used,
                        &vnet.rx_notify_off) != 0) {
        kprintf("[VirtIO Net] Failed to set up receiveq\n");
        cfg->device_status = 0;
        return -1;
    }
    vnet.rx_last_used = 0;

    /* Set up transmitq (queue 1) */
    if (setup_virtqueue(1, &vnet.tx_desc, &vnet.tx_avail, &vnet.tx_used,
                        &vnet.tx_notify_off) != 0) {
        kprintf("[VirtIO Net] Failed to set up transmitq\n");
        cfg->device_status = 0;
        return -1;
    }
    vnet.tx_free_head = 0;
    vnet.tx_last_used = 0;

    /* Allocate RX buffers */
    size_t rx_pages = (VNET_QUEUE_SIZE * VNET_RX_BUF_SIZE + 4095) / 4096;
    vnet.rx_bufs = (uint8_t *)pmm_alloc_pages(rx_pages);
    if (!vnet.rx_bufs) {
        kprintf("[VirtIO Net] Cannot allocate rx buffers\n");
        cfg->device_status = 0;
        return -1;
    }
    memset(vnet.rx_bufs, 0, rx_pages * 4096);

    /* Allocate TX buffer */
    vnet.tx_buf = (uint8_t *)pmm_alloc_pages(1);
    if (!vnet.tx_buf) {
        kprintf("[VirtIO Net] Cannot allocate tx buffer\n");
        cfg->device_status = 0;
        return -1;
    }

    /* Pre-fill receive descriptors */
    for (int i = 0; i < VNET_QUEUE_SIZE; i++) {
        vnet.rx_desc[i].addr = (uint64_t)(uintptr_t)(vnet.rx_bufs +
                                i * VNET_RX_BUF_SIZE);
        vnet.rx_desc[i].len = VNET_RX_BUF_SIZE;
        vnet.rx_desc[i].flags = VIRTQ_DESC_F_WRITE;
        vnet.rx_desc[i].next = 0xFFFF;
        vnet.rx_avail->ring[i] = (uint16_t)i;
    }
    vnet.rx_avail->idx = VNET_QUEUE_SIZE;

    /* Static IP configuration (QEMU user-mode defaults) */
    vnet.ip      = 0x0A00020F;  /* 10.0.2.15 */
    vnet.gateway = 0x0A000202;  /* 10.0.2.2  */
    vnet.netmask = 0xFFFFFF00;  /* 255.255.255.0 */

    /* Driver ready */
    cfg->device_status |= VIRTIO_STATUS_DRIVER_OK;
    __asm__ volatile("mfence" ::: "memory");

    /* Kick receiveq */
    kick_queue(vnet.rx_notify_off, 0);

    vnet.initialized = 1;
    kprintf("[VirtIO Net] Initialized (IP 10.0.2.15, GW 10.0.2.2)\n");

    /* Send ARP request to learn gateway MAC */
    send_arp_request(vnet.gateway);

    return 0;
}

/*============================================================================
 * Ping API
 *============================================================================*/

int virtio_net_ping(uint32_t dest_ip, uint16_t seq)
{
    if (!vnet.initialized) return -1;

    vnet.ping_id = 0x4F53;  /* "OS" */
    vnet.ping_seq = seq;
    vnet.ping_reply_received = 0;
    vnet.ping_rtt_ms = -1;
    vnet.ping_send_time_ms = timer_get_ms();

    if (!vnet.gateway_mac_known) {
        send_arp_request(vnet.gateway);
        for (int i = 0; i < 50 && !vnet.gateway_mac_known; i++) {
            timer_sleep_ms(10);
            virtio_net_poll();
        }
        if (!vnet.gateway_mac_known) return -1;
    }

    return send_icmp_echo_request(dest_ip, vnet.ping_id, seq);
}

int virtio_net_ping_check(void)
{
    virtio_net_poll();
    if (vnet.ping_reply_received)
        return vnet.ping_rtt_ms;
    return -1;
}

/*============================================================================
 * Accessor Functions
 *============================================================================*/

int virtio_net_available(void)
{
    return vnet.initialized;
}

const uint8_t *virtio_net_get_mac(void)
{
    return vnet.initialized ? vnet.mac : NULL;
}

int virtio_net_link_up(void)
{
    if (!vnet.initialized || !vnet.device_cfg) return 0;
    uint16_t status = *(volatile uint16_t *)(vnet.device_cfg + 6);
    return (status & 1) ? 1 : 0;
}

const char *virtio_net_get_ip(void)
{
    return "10.0.2.15";
}

const struct net_stats *virtio_net_get_stats(void)
{
    return &vnet.stats;
}

void virtio_net_dump_info(void)
{
    kprintf("\nVirtIO Network:\n");
    if (!vnet.detected) {
        kprintf("  Not detected\n");
        return;
    }
    kprintf("  PCI:      %u:%u.%u\n",
            vnet.pci_dev->bus, vnet.pci_dev->device, vnet.pci_dev->function);
    kprintf("  MAC:      %02x:%02x:%02x:%02x:%02x:%02x\n",
            vnet.mac[0], vnet.mac[1], vnet.mac[2],
            vnet.mac[3], vnet.mac[4], vnet.mac[5]);
    kprintf("  Link:     %s\n", virtio_net_link_up() ? "Up" : "Down");
    kprintf("  IP:       10.0.2.15\n");
    kprintf("  Gateway:  10.0.2.2\n");
    kprintf("  Netmask:  255.255.255.0\n");
    if (vnet.gateway_mac_known) {
        kprintf("  GW MAC:   %02x:%02x:%02x:%02x:%02x:%02x\n",
                vnet.gateway_mac[0], vnet.gateway_mac[1],
                vnet.gateway_mac[2], vnet.gateway_mac[3],
                vnet.gateway_mac[4], vnet.gateway_mac[5]);
    }
    kprintf("  TX:       %u packets, %u bytes\n",
            (unsigned)vnet.stats.tx_packets,
            (unsigned)vnet.stats.tx_bytes);
    kprintf("  RX:       %u packets, %u bytes\n",
            (unsigned)vnet.stats.rx_packets,
            (unsigned)vnet.stats.rx_bytes);
    kprintf("  ARP sent: %u\n", (unsigned)vnet.stats.arp_replies_sent);
    kprintf("  ICMP:     %u replies, %u pings sent, %u received\n",
            (unsigned)vnet.stats.icmp_replies_sent,
            (unsigned)vnet.stats.ping_sent,
            (unsigned)vnet.stats.ping_received);
}
