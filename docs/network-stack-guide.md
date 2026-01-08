# Network Stack Guide for UDP/TCP Implementation

This guide covers everything you need to understand about the network stack layers 1-4 before implementing UDP and TCP. Read this thoroughly - understanding these foundations is critical for a working transport layer.

---

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Byte Ordering (Critical!)](#byte-ordering-critical)
3. [Packet Buffers (pbuf)](#packet-buffers-pbuf)
4. [Checksum Calculation](#checksum-calculation)
5. [Network Device Layer](#network-device-layer)
6. [Ethernet Layer](#ethernet-layer)
7. [ARP Protocol](#arp-protocol)
8. [IP Layer](#ip-layer)
9. [Packet Flow](#packet-flow)
10. [What UDP/TCP Will Use](#what-udptcp-will-use)

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    YOUR IMPLEMENTATION                       │
│                    ==================                        │
│                                                             │
│         ┌─────────────────┐     ┌─────────────────┐         │
│         │       UDP       │     │       TCP       │         │
│         │   (port demux)  │     │  (state machine │         │
│         │                 │     │   + reliability)│         │
│         └────────┬────────┘     └────────┬────────┘         │
│                  │                       │                   │
│                  └───────────┬───────────┘                   │
│                              │                               │
└──────────────────────────────┼───────────────────────────────┘
                               │
┌──────────────────────────────┼───────────────────────────────┐
│                    ALREADY IMPLEMENTED                       │
│                    ===================                       │
│                              │                               │
│                  ┌───────────┴───────────┐                   │
│                  │       IP Layer        │                   │
│                  │  (routing, checksum)  │                   │
│                  └───────────┬───────────┘                   │
│                              │                               │
│                  ┌───────────┴───────────┐                   │
│                  │   Ethernet + ARP      │                   │
│                  │ (framing, MAC resolve)│                   │
│                  └───────────┬───────────┘                   │
│                              │                               │
│                  ┌───────────┴───────────┐                   │
│                  │   Network Devices     │                   │
│                  │  (E1000, Loopback)    │                   │
│                  └───────────────────────┘                   │
└──────────────────────────────────────────────────────────────┘
```

---

## Byte Ordering (Critical!)

**This is the #1 source of bugs in network code. Understand this thoroughly.**

### The Problem

- Your CPU (x86) is **little-endian**: least significant byte first
- Network protocols are **big-endian**: most significant byte first

```
Value: 0x1234

Little-endian (memory):  [0x34] [0x12]   (x86)
Big-endian (memory):     [0x12] [0x34]   (network)
```

### The Solution: Conversion Macros

Located in `net/util/byteorder.h`:

```c
htons(x)  // Host TO Network Short (16-bit)
htonl(x)  // Host TO Network Long  (32-bit)
ntohs(x)  // Network TO Host Short (16-bit)
ntohl(x)  // Network TO Host Long  (32-bit)
```

### Rules to Remember

1. **All multi-byte fields in packet headers are network order**
2. **Convert when writing to headers**: `hdr->port = htons(port);`
3. **Convert when reading from headers**: `port = ntohs(hdr->port);`
4. **IP addresses are stored in network order** (already converted by `ip_addr_from_str`)
5. **Single bytes don't need conversion**

### Common Mistakes

```c
// WRONG - comparing host order to network order
if (hdr->dst_port == 80) { ... }

// CORRECT
if (ntohs(hdr->dst_port) == 80) { ... }
// OR
if (hdr->dst_port == htons(80)) { ... }

// WRONG - arithmetic on network order values
hdr->length = hdr->length + 20;

// CORRECT
hdr->length = htons(ntohs(hdr->length) + 20);
```

---

## Packet Buffers (pbuf)

### Overview

Packet buffers (`struct pbuf`) are the fundamental data unit. They manage packet data and support efficient header prepend/strip operations.

**Header file**: `net/buf/pbuf.h`

### Structure

```c
struct pbuf {
  struct pbuf *next;    // Chain support (rarely used for single packets)
  void *payload;        // Current data pointer (moves for header ops)
  uint16_t tot_len;     // Total length of chain
  uint16_t len;         // Length of this buffer's data
  uint8_t type;         // PBUF_RAM or PBUF_REF
  uint8_t ref;          // Reference count
  uint8_t flags;        // Reserved
  uint8_t _pad;         // Alignment
};
```

### Allocation Layers

```c
struct pbuf *pbuf_alloc(int layer, uint16_t size, enum pbuf_type type);
```

The `layer` parameter reserves header space:

| Layer | Reserved Space | Use Case |
|-------|---------------|----------|
| `PBUF_TRANSPORT` | 64 bytes | **Use this for UDP/TCP** - room for all headers |
| `PBUF_IP` | 34 bytes | Ethernet + IP headers |
| `PBUF_LINK` | 14 bytes | Ethernet header only |
| `PBUF_RAW` | 0 bytes | Pre-built packets |

### Header Manipulation

**This is how you add protocol headers:**

```c
bool pbuf_header(struct pbuf *p, int16_t delta);
```

- **Negative delta**: Add header space (prepend) - moves payload backward
- **Positive delta**: Strip header (consume) - moves payload forward

```c
// Creating a UDP packet:
struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, payload_len, PBUF_RAM);
memcpy(p->payload, user_data, payload_len);

// Add UDP header (8 bytes)
pbuf_header(p, -8);  // Now p->payload points to UDP header space
struct udp_hdr *udp = p->payload;
udp->src_port = htons(src_port);
// ... fill rest of header

// Add IP header (20 bytes) - done by ip_output()
pbuf_header(p, -20);
```

### Data Operations

```c
// Copy data into pbuf at offset
size_t pbuf_copy_in(struct pbuf *p, const void *data, size_t len, size_t offset);

// Copy data out of pbuf from offset
size_t pbuf_copy_out(const struct pbuf *p, void *buf, size_t len, size_t offset);

// Get pointer to contiguous data (returns NULL if spans multiple pbufs)
void *pbuf_get_contiguous(const struct pbuf *p, size_t offset, size_t len);
```

### Reference Counting

```c
void pbuf_ref(struct pbuf *p);           // Increment ref count
struct pbuf *pbuf_free(struct pbuf *p);  // Decrement, free if zero
```

**Important**: When you pass a pbuf to `ip_output()`, it takes ownership. Don't free it yourself.

---

## Checksum Calculation

### IP Checksum (Simple)

The IP header checksum covers only the IP header itself:

```c
uint16_t checksum(const void *data, size_t len);
```

Returns the checksum in **network byte order**.

### TCP/UDP Checksum (With Pseudo-Header)

**Critical**: TCP and UDP checksums include a "pseudo-header" containing IP addresses. This is NOT optional for TCP.

```
Pseudo-header (12 bytes):
┌────────────────────────────────────┐
│         Source IP Address          │  4 bytes
├────────────────────────────────────┤
│       Destination IP Address       │  4 bytes
├──────────┬─────────┬───────────────┤
│   Zero   │Protocol │  TCP/UDP Len  │  4 bytes
│  (8 bit) │ (8 bit) │   (16 bit)    │
└──────────┴─────────┴───────────────┘
```

### How to Compute UDP/TCP Checksum

```c
// Step 1: Compute pseudo-header checksum
uint32_t sum = checksum_pseudo_header(src_ip, dst_ip, protocol, tcp_udp_length);

// Step 2: Add in the TCP/UDP header + data
sum = checksum_partial(tcp_udp_data, tcp_udp_length, sum);

// Step 3: Finalize
uint16_t final_checksum = checksum_finish(sum);
```

### Complete Example for UDP

```c
int udp_send(uint32_t src_ip, uint32_t dst_ip,
             uint16_t src_port, uint16_t dst_port,
             const void *data, size_t len) {

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    memcpy(p->payload, data, len);

    // Add UDP header
    pbuf_header(p, -UDP_HEADER_LEN);
    struct udp_hdr *udp = p->payload;

    uint16_t udp_len = UDP_HEADER_LEN + len;

    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(udp_len);
    udp->checksum = 0;  // Must be zero for checksum calculation

    // Calculate checksum with pseudo-header
    uint32_t sum = checksum_pseudo_header(src_ip, dst_ip, IP_PROTO_UDP, udp_len);
    sum = checksum_partial(udp, udp_len, sum);
    udp->checksum = checksum_finish(sum);

    // Send via IP layer
    return ip_output(NULL, p, src_ip, dst_ip, IP_PROTO_UDP, IP_DEFAULT_TTL);
}
```

### Checksum Verification on Receive

```c
void udp_input(struct netdev *dev, struct pbuf *p,
               uint32_t src_ip, uint32_t dst_ip) {
    struct udp_hdr *udp = p->payload;
    uint16_t udp_len = ntohs(udp->length);

    // Verify checksum (if non-zero; zero means "no checksum" for UDP)
    if (udp->checksum != 0) {
        uint32_t sum = checksum_pseudo_header(src_ip, dst_ip, IP_PROTO_UDP, udp_len);
        sum = checksum_partial(udp, udp_len, sum);
        uint16_t result = checksum_finish(sum);

        // Valid checksum produces 0x0000 or 0xFFFF
        if (result != 0x0000 && result != 0xFFFF) {
            // Bad checksum - drop packet
            pbuf_free(p);
            return;
        }
    }

    // Process valid packet...
}
```

---

## Network Device Layer

### Overview

The `netdev` abstraction represents network interfaces (E1000, loopback).

**Header**: `net/driver/netdev.h`

### Key Structure

```c
struct netdev {
    char name[8];           // "eth0", "lo"
    uint8_t mac_addr[6];    // MAC address
    uint32_t ip_addr;       // IP address (network order)
    uint32_t netmask;       // Subnet mask
    uint32_t gateway;       // Default gateway
    uint16_t mtu;           // Max transmission unit (1500 for Ethernet)
    uint32_t flags;         // NETDEV_FLAG_UP, NETDEV_FLAG_LOOPBACK

    // Statistics
    uint32_t tx_packets, rx_packets;
    uint32_t tx_bytes, rx_bytes;

    const struct netdev_ops *ops;  // Driver operations
};
```

### Key Functions

```c
// Find device by name
struct netdev *netdev_find_by_name(const char *name);

// Get loopback device
struct netdev *netdev_get_loopback(void);

// Transmit packet (takes ownership of pbuf)
int netdev_transmit(struct netdev *dev, struct pbuf *p);

// Receive packet (returns NULL if none available)
struct pbuf *netdev_receive(struct netdev *dev);
```

### Important Notes

- `eth0` is the E1000 NIC connected to the external network
- `lo` is the loopback device for 127.0.0.1
- MTU is 1500 bytes - your UDP/TCP data must respect this

---

## Ethernet Layer

### Overview

Handles Ethernet frame creation and parsing.

**Header**: `net/link/ethernet.h`

### Frame Format

```
┌─────────────┬─────────────┬───────────┬─────────────────┐
│  Dest MAC   │  Src MAC    │ EtherType │     Payload     │
│  (6 bytes)  │  (6 bytes)  │ (2 bytes) │  (46-1500 bytes)│
└─────────────┴─────────────┴───────────┴─────────────────┘
```

### Header Structure

```c
struct eth_hdr {
    uint8_t dst_mac[6];     // Destination MAC
    uint8_t src_mac[6];     // Source MAC
    uint16_t ethertype;     // Protocol (network order)
} __attribute__((packed));  // 14 bytes total
```

### EtherTypes

```c
#define ETH_TYPE_IP   0x0800
#define ETH_TYPE_ARP  0x0806
```

### Key Functions

```c
// Send Ethernet frame (prepends header, transmits)
int ethernet_output(struct netdev *dev, struct pbuf *p,
                    const uint8_t *dst_mac, uint16_t ethertype);

// Process received frame (strips header, dispatches to IP/ARP)
void ethernet_input(struct netdev *dev, struct pbuf *p);
```

**Note**: You rarely call these directly - `ip_output()` handles Ethernet framing.

---

## ARP Protocol

### Overview

ARP resolves IP addresses to MAC addresses. The IP layer uses it automatically.

**Header**: `net/link/arp.h`

### Key Function

```c
// Returns true if MAC found, false if ARP request sent (packet queued)
bool arp_resolve(struct netdev *dev, uint32_t ip, uint8_t *mac_out);
```

### How It Works

1. `ip_output()` needs to send to IP X.X.X.X
2. Calls `arp_resolve()` to get MAC address
3. If in cache: returns immediately with MAC
4. If not in cache: sends ARP request, returns false, packet queued
5. When ARP reply arrives, queued packets are sent

**For UDP/TCP**: You don't need to interact with ARP directly. The IP layer handles it.

---

## IP Layer

### Overview

Handles IP packet creation, routing, and reception.

**Header**: `net/inet/ip.h`

### IP Header Structure

```c
struct ip_hdr {
    uint8_t version_ihl;    // Version (4 bits) + Header Length (4 bits)
    uint8_t tos;            // Type of Service
    uint16_t tot_len;       // Total length (network order)
    uint16_t id;            // Identification
    uint16_t frag_off;      // Fragment offset + flags
    uint8_t ttl;            // Time to Live
    uint8_t protocol;       // Protocol (TCP=6, UDP=17)
    uint16_t checksum;      // Header checksum
    uint32_t src_addr;      // Source IP (network order)
    uint32_t dst_addr;      // Destination IP (network order)
} __attribute__((packed));  // 20 bytes (minimum)
```

### Header Macros

```c
IP_HDR_VERSION(hdr)  // Extract IP version (should be 4)
IP_HDR_IHL(hdr)      // Header length in 32-bit words
IP_HDR_HLEN(hdr)     // Header length in bytes (IHL * 4)
```

### Protocol Numbers

```c
#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17
```

### Key Functions

#### Sending Packets

```c
int ip_output(struct netdev *dev, struct pbuf *p,
              uint32_t src, uint32_t dst,
              uint8_t protocol, uint8_t ttl);
```

- `dev`: Device to send on (NULL for automatic routing)
- `p`: Packet buffer with payload (IP header will be prepended)
- `src`: Source IP (0 for auto-select)
- `dst`: Destination IP
- `protocol`: IP_PROTO_TCP or IP_PROTO_UDP
- `ttl`: Time to live (use IP_DEFAULT_TTL = 64)
- **Returns**: 0 on success, negative on error
- **Note**: Takes ownership of pbuf

#### Receiving Packets

The IP layer calls your protocol handler:

```c
// You implement these:
void tcp_input(struct netdev *dev, struct pbuf *p,
               uint32_t src_ip, uint32_t dst_ip);

void udp_input(struct netdev *dev, struct pbuf *p,
               uint32_t src_ip, uint32_t dst_ip);
```

When called:
- `p->payload` points to the TCP/UDP header (IP header stripped)
- `p->len` is the TCP/UDP segment length
- You own the pbuf - must free it when done

### Utility Functions

```c
// Parse "192.168.1.1" to network-order uint32_t
uint32_t ip_addr_from_str(const char *str);

// Convert network-order IP to string (buf must be >=16 bytes)
char *ip_addr_to_str(uint32_t ip, char *buf);

// Check if IP is local to this host
int ip_is_local(uint32_t ip);
```

---

## Packet Flow

### Outbound (Sending)

```
Application
    │
    ▼
┌─────────────────────────────────────────────────────────┐
│ TCP/UDP Layer                                           │
│  1. Allocate pbuf with PBUF_TRANSPORT                   │
│  2. Copy user data to payload                           │
│  3. pbuf_header(p, -header_size) to prepend header      │
│  4. Fill in TCP/UDP header fields                       │
│  5. Calculate checksum with pseudo-header               │
│  6. Call ip_output(dev, p, src, dst, protocol, ttl)     │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│ IP Layer                                                │
│  1. Look up route                                       │
│  2. pbuf_header(p, -20) to prepend IP header            │
│  3. Fill in IP header fields                            │
│  4. Calculate IP header checksum                        │
│  5. Resolve next-hop MAC via ARP                        │
│  6. Call ethernet_output(dev, p, mac, ETH_TYPE_IP)      │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│ Ethernet Layer                                          │
│  1. pbuf_header(p, -14) to prepend Ethernet header      │
│  2. Fill in MAC addresses and EtherType                 │
│  3. Call netdev_transmit(dev, p)                        │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
                    Hardware
```

### Inbound (Receiving)

```
                    Hardware
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│ Network Input Thread                                    │
│  1. Poll devices: p = netdev_receive(dev)               │
│  2. Call ethernet_input(dev, p)                         │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│ Ethernet Layer                                          │
│  1. Parse Ethernet header                               │
│  2. pbuf_header(p, 14) to strip Ethernet header         │
│  3. Dispatch based on EtherType:                        │
│     - ETH_TYPE_IP  → ip_input(dev, p)                   │
│     - ETH_TYPE_ARP → arp_input(dev, p)                  │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│ IP Layer                                                │
│  1. Validate IP header (version, checksum, length)      │
│  2. Check if destination is for us                      │
│  3. pbuf_header(p, IP_HLEN) to strip IP header          │
│  4. Dispatch based on protocol:                         │
│     - IP_PROTO_TCP  → tcp_input(dev, p, src, dst)       │
│     - IP_PROTO_UDP  → udp_input(dev, p, src, dst)       │
│     - IP_PROTO_ICMP → icmp_input(dev, p, src, dst)      │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│ TCP/UDP Layer (YOUR IMPLEMENTATION)                     │
│  1. p->payload points to TCP/UDP header                 │
│  2. Verify checksum with pseudo-header                  │
│  3. Look up connection/socket by ports                  │
│  4. Process segment (TCP state machine, etc.)           │
│  5. Deliver data to application                         │
│  6. pbuf_free(p) when done                              │
└─────────────────────────────────────────────────────────┘
                         │
                         ▼
                   Application
```

---

## What UDP/TCP Will Use

### Summary of Functions You'll Call

```c
// Packet buffer management
struct pbuf *pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
bool pbuf_header(p, -header_len);  // Add header
bool pbuf_header(p, header_len);   // Strip header (on receive)
size_t pbuf_copy_in(p, data, len, offset);
size_t pbuf_copy_out(p, buf, len, offset);
void *pbuf_get_contiguous(p, offset, len);
void pbuf_free(p);

// Checksum (critical for TCP, important for UDP)
uint32_t checksum_pseudo_header(src_ip, dst_ip, protocol, len);
uint32_t checksum_partial(data, len, sum);
uint16_t checksum_finish(sum);

// IP output
int ip_output(dev, p, src_ip, dst_ip, protocol, ttl);

// IP utilities
uint32_t ip_addr_from_str(str);
char *ip_addr_to_str(ip, buf);
int ip_is_local(ip);

// Device lookup (for socket binding)
struct netdev *netdev_find_by_name(name);
struct netdev *netdev_get_loopback(void);

// Byte order
htons(x), htonl(x), ntohs(x), ntohl(x)
```

### Functions You'll Implement

```c
// Called by IP layer when TCP/UDP packet arrives
void tcp_input(struct netdev *dev, struct pbuf *p,
               uint32_t src_ip, uint32_t dst_ip);

void udp_input(struct netdev *dev, struct pbuf *p,
               uint32_t src_ip, uint32_t dst_ip);
```

### Header Sizes to Remember

| Protocol | Header Size | Notes |
|----------|-------------|-------|
| Ethernet | 14 bytes | Handled by lower layers |
| IP | 20 bytes | Minimum, can have options |
| UDP | 8 bytes | Fixed size |
| TCP | 20 bytes | Minimum, can have options (up to 60) |

### Port Number Rules

- Ports 0-1023: Well-known (require privilege)
- Ports 1024-49151: Registered
- Ports 49152-65535: Dynamic/ephemeral

---

## Testing Your Implementation

Run the network test suite to verify the foundation:

```c
// In src/threads/init.c, uncomment:
net_run_all_tests();
```

This runs 90 tests covering:
- Packet buffers (allocation, headers, copy operations)
- Checksums (including pseudo-header for UDP/TCP)
- Network devices (E1000, loopback)
- Ethernet framing
- ARP resolution
- IP layer (routing, addresses, output)
- Transport header structure verification

All tests should pass before you start implementing UDP/TCP.

---

## Quick Reference Card

```
┌────────────────────────────────────────────────────────────────┐
│                    NETWORK BYTE ORDER                          │
├────────────────────────────────────────────────────────────────┤
│  Writing header:  hdr->field = htons(value);                   │
│  Reading header:  value = ntohs(hdr->field);                   │
│  IP addresses:    Always network order in headers              │
└────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│                    PBUF OPERATIONS                             │
├────────────────────────────────────────────────────────────────┤
│  Allocate:        pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM)    │
│  Add header:      pbuf_header(p, -size)  // negative!          │
│  Strip header:    pbuf_header(p, size)   // positive           │
│  Free:            pbuf_free(p)                                 │
└────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│                    UDP/TCP CHECKSUM                            │
├────────────────────────────────────────────────────────────────┤
│  1. sum = checksum_pseudo_header(src, dst, proto, len)         │
│  2. sum = checksum_partial(hdr_and_data, len, sum)             │
│  3. checksum = checksum_finish(sum)                            │
│                                                                │
│  Verification: result should be 0x0000 or 0xFFFF               │
└────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│                    SENDING A PACKET                            │
├────────────────────────────────────────────────────────────────┤
│  1. p = pbuf_alloc(PBUF_TRANSPORT, payload_len, PBUF_RAM)      │
│  2. memcpy(p->payload, data, payload_len)                      │
│  3. pbuf_header(p, -HEADER_LEN)                                │
│  4. Fill header fields (use htons/htonl!)                      │
│  5. Calculate checksum                                         │
│  6. ip_output(NULL, p, src, dst, IP_PROTO_xxx, 64)             │
│     (ip_output takes ownership - don't free p!)                │
└────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│                    RECEIVING A PACKET                          │
├────────────────────────────────────────────────────────────────┤
│  xxx_input(dev, p, src_ip, dst_ip) called by IP layer          │
│  - p->payload points to TCP/UDP header                         │
│  - Verify checksum                                             │
│  - Strip header: pbuf_header(p, HEADER_LEN)                    │
│  - Process data                                                │
│  - pbuf_free(p) when done                                      │
└────────────────────────────────────────────────────────────────┘
```

---

## Common Pitfalls

1. **Forgetting byte order conversion** - Use htons/htonl for ALL multi-byte header fields
2. **Wrong checksum calculation** - Must include pseudo-header for TCP/UDP
3. **Not freeing pbufs** - Every allocated pbuf must be freed exactly once
4. **Double-freeing pbufs** - ip_output takes ownership, don't free after calling it
5. **Wrong pbuf_header sign** - Negative to add headers, positive to strip
6. **Using PBUF_RAW for new packets** - Use PBUF_TRANSPORT to reserve header space
7. **Comparing network-order values** - Convert to host order first, or convert constants to network order

Good luck with your UDP and TCP implementation!
