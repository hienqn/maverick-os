# Network Stack Implementation Plan for Deep TCP/IP Learning

## Overview

Build a complete network stack from scratch in Pintos to deeply understand TCP/IP protocols. The implementation follows a bottom-up approach with heavy emphasis on TCP internals.

**Your Background**: Advanced networking knowledge, focused on deep TCP understanding
**Testing Strategy**: Both loopback (simple) and QEMU host networking (realistic)

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    SOCKET LAYER (syscalls)                  │
│      socket(), bind(), listen(), connect(), send(), recv()  │
└──────────────────────────────┬──────────────────────────────┘
                               │
           ┌───────────────────┴───────────────────┐
           │                                       │
┌──────────┴──────────┐               ┌────────────┴────────────┐
│        TCP          │               │          UDP            │
│  - State machine    │               │  - Connectionless       │
│  - Congestion ctrl  │               │  - Simple send/recv     │
│  - Reliability      │               │                         │
└──────────┬──────────┘               └────────────┬────────────┘
           └───────────────────┬───────────────────┘
                               │
┌──────────────────────────────┴──────────────────────────────┐
│                         IP LAYER                             │
│           Routing, ICMP, checksums, fragmentation            │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────┴──────────────────────────────┐
│                    ETHERNET + ARP                            │
│             Frame handling, address resolution               │
└──────────────────────────────┬──────────────────────────────┘
                               │
┌──────────────────────────────┴──────────────────────────────┐
│                   NETWORK DEVICE LAYER                       │
│               netdev abstraction, packet queues              │
└──────────────────────────────┬──────────────────────────────┘
                               │
           ┌───────────────────┴───────────────────┐
           │                                       │
┌──────────┴──────────┐               ┌────────────┴────────────┐
│    E1000 Driver     │               │    Loopback Driver      │
│  - DMA rings        │               │  - Internal queue       │
│  - MMIO, IRQ        │               │                         │
└─────────────────────┘               └─────────────────────────┘
```

---

## File Organization

Create `src/net/` directory:

```
src/net/
├── net.h / net.c           # Subsystem init
├── driver/
│   ├── netdev.h/c          # Device abstraction
│   ├── e1000.h/c           # E1000 driver (move from devices/)
│   └── loopback.c          # Loopback device
├── link/
│   ├── ethernet.h/c        # Ethernet frames
│   └── arp.h/c             # ARP protocol/cache
├── inet/
│   ├── ip.h/c              # IPv4 handling
│   ├── icmp.h/c            # ICMP (ping)
│   └── route.h/c           # Routing table
├── transport/
│   ├── udp.h/c             # UDP protocol
│   ├── tcp.h/c             # TCP core + state machine
│   ├── tcp_input.c         # TCP receive path
│   ├── tcp_output.c        # TCP send path
│   ├── tcp_timer.c         # RTT, RTO, timers
│   └── tcp_congestion.c    # Slow start, AIMD, fast retransmit
├── socket/
│   ├── socket.h/c          # Socket structures
│   └── socket_syscall.c    # Syscall handlers
├── buf/
│   └── pbuf.h/c            # Packet buffers
└── util/
    ├── checksum.h/c        # IP/TCP/UDP checksums
    └── byteorder.h         # Network byte order
```

---

## Implementation Phases

### Phase 1: Network Device Layer + Loopback
**Goal**: Device abstraction and packet buffer management
**Milestone**: Loopback echo test passes

Key tasks:
1. Implement `pbuf` (packet buffer) allocation/free
2. Create `netdev` abstraction (following `block.h` pattern)
3. Implement loopback driver (packets go straight back to RX queue)
4. Create network input thread (following kbd.c top/bottom-half pattern)

Key files: `src/net/buf/pbuf.c`, `src/net/driver/netdev.c`, `src/net/driver/loopback.c`

---

### Phase 2: E1000 Driver
**Goal**: Real hardware packet I/O with DMA
**Milestone**: E1000 sends/receives raw Ethernet frames

Key tasks:
1. Read MAC address from EEPROM via MMIO
2. Set up TX/RX descriptor rings (DMA)
3. Implement TX path with descriptor management
4. Implement RX path with interrupt handling
5. Wire up IRQ handler

Key data structures:
- `struct e1000_tx_desc` / `struct e1000_rx_desc` (hardware format)
- TX/RX ring management with head/tail pointers

Key file: `src/net/driver/e1000.c` (complete rewrite of existing stub)

---

### Phase 3: Ethernet + ARP
**Goal**: Link layer with address resolution
**Milestone**: ARP resolution works

Key tasks:
1. Ethernet frame parsing/building
2. ARP cache with timeout
3. ARP request/reply handling
4. Packet queueing during resolution

Key files: `src/net/link/ethernet.c`, `src/net/link/arp.c`

---

### Phase 4: IP + ICMP
**Goal**: Network layer with ping support
**Milestone**: `ping` from host to Pintos works

Key tasks:
1. IP header parsing/building with checksum
2. Simple routing table (gateway, local network)
3. ICMP echo request/reply

Key files: `src/net/inet/ip.c`, `src/net/inet/icmp.c`, `src/net/inet/route.c`

---

### Phase 5: UDP
**Goal**: Simple transport layer
**Milestone**: UDP echo server works

Key tasks:
1. UDP header with pseudo-header checksum
2. Port demultiplexing via UDP PCB table
3. sendto/recvfrom operations

Key file: `src/net/transport/udp.c`

---

### Phase 6: TCP (Deep Dive - 4 sub-phases)

#### Phase 6A: State Machine
**Goal**: Master the 11-state TCP FSM

All 11 states:
```
CLOSED → LISTEN (passive open)
CLOSED → SYN_SENT (active open)
LISTEN → SYN_RCVD (recv SYN)
SYN_SENT → ESTABLISHED (recv SYN+ACK)
SYN_RCVD → ESTABLISHED (recv ACK)
ESTABLISHED → FIN_WAIT_1 (send FIN)
ESTABLISHED → CLOSE_WAIT (recv FIN)
FIN_WAIT_1 → FIN_WAIT_2 (recv ACK)
FIN_WAIT_1 → CLOSING (recv FIN)
FIN_WAIT_2 → TIME_WAIT (recv FIN)
CLOSE_WAIT → LAST_ACK (send FIN)
CLOSING → TIME_WAIT (recv ACK)
LAST_ACK → CLOSED (recv ACK)
TIME_WAIT → CLOSED (2*MSL timeout)
```

Key data structure - TCP Control Block (TCB):
```c
struct tcp_pcb {
    // Connection ID
    uint32_t local_ip, remote_ip;
    uint16_t local_port, remote_port;

    // State machine
    enum tcp_state state;

    // Send sequence space
    uint32_t snd_una;    // oldest unacked
    uint32_t snd_nxt;    // next to send
    uint32_t snd_wnd;    // send window
    uint32_t iss;        // initial send seq

    // Receive sequence space
    uint32_t rcv_nxt;    // next expected
    uint32_t rcv_wnd;    // receive window
    uint32_t irs;        // initial recv seq

    // Buffers, timers, congestion (below)
};
```

#### Phase 6B: Sliding Window + Reliability
**Goal**: Data transfer with retransmission

Key concepts:
- Send buffer: holds data until ACKed
- Receive buffer: handles out-of-order segments
- Cumulative ACKs
- Window updates

#### Phase 6C: Timers + RTT Estimation
**Goal**: Jacobson/Karels algorithm for RTO

RTT estimation (per RFC 6298):
```
On ACK for new data:
  SampleRTT = now - segment_send_time

  If first sample:
    SRTT = SampleRTT
    RTTVAR = SampleRTT / 2
  Else:
    RTTVAR = (1-β)*RTTVAR + β*|SampleRTT - SRTT|  (β = 1/4)
    SRTT = (1-α)*SRTT + α*SampleRTT               (α = 1/8)

  RTO = SRTT + max(G, 4*RTTVAR)

Karn's Algorithm: Don't update RTT from retransmitted segments
Exponential backoff: RTO doubles on each timeout
```

#### Phase 6D: Congestion Control
**Goal**: Slow start, congestion avoidance, fast retransmit/recovery

Algorithms:
1. **Slow Start**: cwnd doubles each RTT (exponential growth until ssthresh)
2. **Congestion Avoidance**: cwnd += MSS/cwnd per ACK (linear growth)
3. **Fast Retransmit**: Retransmit on 3 duplicate ACKs (don't wait for RTO)
4. **Fast Recovery**: cwnd = ssthresh + 3*MSS, inflate for dup ACKs, deflate on new ACK

Key: `effective_window = min(cwnd, rwnd)`

**Milestone**: TCP echo server works

---

### Phase 7: Socket API
**Goal**: User-space interface
**Milestone**: User programs can use sockets

New syscalls:
- `socket(domain, type, protocol)`
- `bind(fd, addr, len)`, `listen(fd, backlog)`, `accept(fd, addr, len)`
- `connect(fd, addr, len)`
- `send(fd, buf, len, flags)`, `recv(fd, buf, len, flags)`
- `sendto()`, `recvfrom()` (for UDP)
- `close()` (extend existing)

Integrate with existing fd table in `src/userprog/`.

---

### Phase 8: Integration Test
**Goal**: Full stack verification
**Milestone**: HTTP GET works

Test: Simple HTTP client fetches page from host, or host fetches from Pintos HTTP server.

---

## Key Files to Reference

| File | Purpose |
|------|---------|
| `src/devices/e1000.c` | Current stub - starting point for driver |
| `src/devices/intq.c` | Interrupt queue pattern for packet queues |
| `src/devices/kbd.c` | Top/bottom-half interrupt pattern |
| `src/devices/block.h` | Device abstraction pattern to follow |
| `src/userprog/syscall.c` | Syscall dispatch pattern |
| `src/lib/kernel/hash.h` | Hash table for TCB lookup |

---

## Testing Strategy

### Loopback Testing (Simple)
- No hardware dependency
- Test protocol logic in isolation
- Useful for: UDP, TCP state machine, socket API

### QEMU Testing (Realistic)
```bash
# User networking (easy, no root)
qemu-system-i386 -netdev user,id=net0,hostfwd=tcp::5555-:80 \
    -device e1000,netdev=net0 -hda pintos.img

# Packet capture for debugging
qemu-system-i386 -object filter-dump,id=d0,netdev=net0,file=packets.pcap \
    -netdev user,id=net0 -device e1000,netdev=net0
```

Use Wireshark on `packets.pcap` to debug protocol issues.

### Key Test Points
1. Phase 2: Raw frame TX/RX (tcpdump/Wireshark)
2. Phase 4: `ping 10.0.2.15` from host
3. Phase 5: `nc -u 10.0.2.15 7777` for UDP
4. Phase 6: `nc 10.0.2.15 7777` for TCP
5. Phase 8: `curl http://10.0.2.15:8080/`

---

## Summary

| Phase | Milestone | Key Learning |
|-------|-----------|--------------|
| 1 | Loopback echo | Device abstraction |
| 2 | E1000 raw frames | DMA, MMIO, interrupts |
| 3 | ARP works | Address resolution |
| 4 | **Ping works** | IP, ICMP |
| 5 | UDP echo | Simple transport |
| 6A | TCP states | 11-state FSM |
| 6B | TCP data | Sliding window |
| 6C | TCP timers | RTT/RTO |
| 6D | **TCP echo** | Congestion control |
| 7 | Socket API | Syscalls |
| 8 | **HTTP GET** | Full integration |

This plan gives you deep hands-on experience with every layer of the TCP/IP stack, with particular depth in TCP's reliability and congestion control mechanisms.
