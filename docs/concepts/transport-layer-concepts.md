# Transport Layer Concepts and Principles

This document covers the theoretical concepts you must understand before implementing UDP and TCP. The previous guide covered the "how" (API usage) - this covers the "why" and "what".

---

## Table of Contents

1. [Transport Layer Fundamentals](#transport-layer-fundamentals)
2. [UDP Concepts](#udp-concepts)
3. [TCP Concepts](#tcp-concepts)
4. [Protocol Control Blocks (PCBs)](#protocol-control-blocks-pcbs)
5. [Port Demultiplexing](#port-demultiplexing)
6. [Concurrency and Synchronization](#concurrency-and-synchronization)
7. [Buffer Management](#buffer-management)
8. [Timer Management](#timer-management)
9. [Error Handling](#error-handling)

---

## Transport Layer Fundamentals

### What Transport Layer Does

The transport layer provides **process-to-process** communication, built on top of IP's **host-to-host** communication.

```
┌─────────────────────────────────────────────────────────────┐
│  Application A          Network           Application B     │
│  (Process)                                 (Process)        │
│       │                                        ▲            │
│       │    ┌──────────────────────────────┐    │            │
│       └───►│     Transport Layer          │────┘            │
│            │  (process-to-process)        │                 │
│            │  Identified by: IP + Port    │                 │
│            └──────────────┬───────────────┘                 │
│                           │                                 │
│            ┌──────────────┴───────────────┐                 │
│            │        IP Layer              │                 │
│            │   (host-to-host)             │                 │
│            │   Identified by: IP address  │                 │
│            └──────────────────────────────┘                 │
└─────────────────────────────────────────────────────────────┘
```

### The Socket Pair

A connection/association is uniquely identified by a **5-tuple**:

```
(Protocol, Local IP, Local Port, Remote IP, Remote Port)
```

For TCP, this identifies a unique connection. For UDP, this identifies a communication endpoint.

### Multiplexing and Demultiplexing

**Multiplexing** (sending): Multiple applications share the network through different ports.

**Demultiplexing** (receiving): Incoming packets are directed to the correct application based on port numbers.

```
        Sending Host                         Receiving Host
┌─────────────────────────┐           ┌─────────────────────────┐
│  App1    App2    App3   │           │  App1    App2    App3   │
│   │       │       │     │           │   ▲       ▲       ▲     │
│   │       │       │     │           │   │       │       │     │
│ port    port    port    │           │ port    port    port    │
│ 5000    5001    5002    │           │ 5000    5001    5002    │
│   │       │       │     │           │   │       │       │     │
│   └───────┼───────┘     │           │   └───────┼───────┘     │
│           │             │           │           │             │
│     Transport Layer     │           │     Transport Layer     │
│     (multiplexing)      │           │     (demultiplexing)    │
│           │             │           │           ▲             │
└───────────┼─────────────┘           └───────────┼─────────────┘
            │                                     │
            └──────────► Network ─────────────────┘
```

---

## UDP Concepts

### Overview

UDP (User Datagram Protocol) is the simpler transport protocol:
- **Connectionless**: No handshake, no state
- **Unreliable**: No delivery guarantees, no ordering
- **Message-oriented**: Preserves message boundaries

### When to Use UDP

- Real-time applications (VoIP, video streaming, gaming)
- DNS queries (single request-response)
- When you want to implement your own reliability
- Broadcast/multicast

### UDP Header

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
├─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┤
│          Source Port          │       Destination Port        │
├───────────────────────────────┼───────────────────────────────┤
│            Length             │           Checksum            │
├───────────────────────────────┴───────────────────────────────┤
│                             Data                              │
└───────────────────────────────────────────────────────────────┘
```

- **Length**: Header (8 bytes) + Data length
- **Checksum**: Optional in IPv4, mandatory in IPv6

### UDP Implementation Checklist

1. **PCB Management**: Track bound sockets
2. **Port Allocation**: Assign ephemeral ports
3. **Demultiplexing**: Route to correct socket by port
4. **Send Path**: Build header, compute checksum, call ip_output()
5. **Receive Path**: Validate, find socket, queue data
6. **Blocking Receive**: Wait on semaphore/condition

---

## TCP Concepts

TCP is significantly more complex. Understand these concepts thoroughly.

### The Three Properties TCP Provides

1. **Reliable Delivery**: Data arrives, or sender knows it didn't
2. **In-Order Delivery**: Data arrives in the order it was sent
3. **Flow Control**: Sender doesn't overwhelm receiver

### TCP Header

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
├─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┴─┤
│          Source Port          │       Destination Port        │
├───────────────────────────────────────────────────────────────┤
│                        Sequence Number                        │
├───────────────────────────────────────────────────────────────┤
│                    Acknowledgment Number                      │
├───────┬───────┬─┬─┬─┬─┬─┬─┬───────────────────────────────────┤
│  Data │       │U│A│P│R│S│F│                                   │
│ Offset│ Rsrvd │R│C│S│S│Y│I│            Window                 │
│       │       │G│K│H│T│N│N│                                   │
├───────┴───────┴─┴─┴─┴─┴─┴─┴───────────────────────────────────┤
│           Checksum            │         Urgent Pointer        │
├───────────────────────────────┴───────────────────────────────┤
│                    Options (if Data Offset > 5)               │
├───────────────────────────────────────────────────────────────┤
│                             Data                              │
└───────────────────────────────────────────────────────────────┘
```

### TCP Flags

| Flag | Name | Meaning |
|------|------|---------|
| SYN | Synchronize | Initiate connection, synchronize sequence numbers |
| ACK | Acknowledgment | Acknowledgment field is valid |
| FIN | Finish | Sender is done sending |
| RST | Reset | Abort connection immediately |
| PSH | Push | Deliver data to application immediately |
| URG | Urgent | Urgent pointer field is valid |

### Sequence Numbers

Every byte of data has a sequence number. This enables:
- **Ordering**: Reassemble out-of-order segments
- **Duplicate Detection**: Discard already-received data
- **Acknowledgment**: "I received bytes up to X"

```
Sequence Space (32-bit, wraps around):

     Sent and ACKed     Sent, not ACKed    Can send     Can't send yet
    ─────────────────┼─────────────────┼──────────────┼─────────────────
                   SND.UNA           SND.NXT      SND.UNA+SND.WND

    SND.UNA = oldest unacknowledged sequence number
    SND.NXT = next sequence number to send
    SND.WND = send window (how much receiver can accept)
```

### The Three-Way Handshake

```
    Client                                    Server
      │                                          │
      │  SYN, Seq=X                              │
      │─────────────────────────────────────────►│
      │                                          │
      │             SYN+ACK, Seq=Y, Ack=X+1      │
      │◄─────────────────────────────────────────│
      │                                          │
      │  ACK, Seq=X+1, Ack=Y+1                   │
      │─────────────────────────────────────────►│
      │                                          │
      │          Connection Established          │
      │◄────────────────────────────────────────►│
```

**Why three-way?**
- Both sides must agree on initial sequence numbers
- Protects against old duplicate SYNs

### The Four-Way Close

```
    Client                                    Server
      │                                          │
      │  FIN, Seq=X                              │
      │─────────────────────────────────────────►│  Client done sending
      │                                          │
      │             ACK, Ack=X+1                 │
      │◄─────────────────────────────────────────│  Server ACKs FIN
      │                                          │
      │    (Server may still send data...)       │
      │                                          │
      │             FIN, Seq=Y                   │
      │◄─────────────────────────────────────────│  Server done sending
      │                                          │
      │  ACK, Ack=Y+1                            │
      │─────────────────────────────────────────►│  Client ACKs FIN
      │                                          │
      │     (TIME_WAIT for 2*MSL)                │
```

**Why four-way?** Each direction closes independently (half-close).

### TCP State Machine (The 11 States)

```
                              ┌───────────────────────────────────────┐
                              │                CLOSED                 │
                              └───────────────────────────────────────┘
                                    │                      │
           ┌────────────────────────┘                      └─────────────────────────┐
           │ Passive OPEN                                         Active OPEN        │
           │ create TCB                                           create TCB         │
           ▼                                                      send SYN           │
    ┌─────────────┐                                                    │             │
    │   LISTEN    │◄───────────────────────────────────────────────────┼─────────────┤
    └─────────────┘                                                    ▼             │
           │                                                    ┌─────────────┐      │
           │ rcv SYN                                             │  SYN_SENT   │      │
           │ send SYN,ACK                                        └─────────────┘      │
           ▼                                                           │              │
    ┌─────────────┐                                    rcv SYN,ACK     │              │
    │  SYN_RCVD   │                                    send ACK        │              │
    └─────────────┘                                          │         │              │
           │                                                 │         │              │
           │ rcv ACK of SYN                                  │         │              │
           │                                                 ▼         ▼              │
           └────────────────────────────────►┌─────────────────────────────┐          │
                                             │        ESTABLISHED         │◄─────────┘
                                             └─────────────────────────────┘
                                                    │              │
                       ┌────────────────────────────┘              └────────────────────────┐
                       │ CLOSE                                               rcv FIN        │
                       │ send FIN                                            send ACK       │
                       ▼                                                           │        │
                ┌─────────────┐                                                    ▼        │
                │ FIN_WAIT_1  │                                             ┌─────────────┐ │
                └─────────────┘                                             │ CLOSE_WAIT  │ │
                    │      │                                                └─────────────┘ │
       rcv ACK of FIN      │ rcv FIN                                               │        │
                │          │ send ACK                                    CLOSE     │        │
                ▼          ▼                                             send FIN  │        │
         ┌─────────────┐ ┌─────────────┐                                      │    │        │
         │ FIN_WAIT_2  │ │   CLOSING   │                                      ▼    ▼        │
         └─────────────┘ └─────────────┘                                ┌─────────────┐     │
                │              │                                        │  LAST_ACK   │     │
                │ rcv FIN      │ rcv ACK of FIN                         └─────────────┘     │
                │ send ACK     │                                               │            │
                ▼              ▼                                               │ rcv ACK    │
         ┌─────────────────────────────┐                                       ▼            │
         │         TIME_WAIT           │                      ┌───────────────────────────┐ │
         │       (wait 2*MSL)          │                      │          CLOSED           │ │
         └─────────────────────────────┘                      └───────────────────────────┘ │
                       │                                                       ▲            │
                       │ 2MSL timeout                                          │            │
                       └───────────────────────────────────────────────────────┘            │
```

### State Descriptions

| State | Description |
|-------|-------------|
| CLOSED | No connection |
| LISTEN | Waiting for connection request (server) |
| SYN_SENT | SYN sent, waiting for SYN+ACK (client) |
| SYN_RCVD | SYN received, SYN+ACK sent, waiting for ACK |
| ESTABLISHED | Connection open, data transfer |
| FIN_WAIT_1 | FIN sent, waiting for ACK or FIN |
| FIN_WAIT_2 | FIN ACKed, waiting for FIN from other side |
| CLOSE_WAIT | FIN received, waiting for application to close |
| CLOSING | Both sides sent FIN simultaneously |
| LAST_ACK | FIN sent after receiving FIN, waiting for ACK |
| TIME_WAIT | Waiting to ensure remote received final ACK |

### Sliding Window Protocol

The sliding window enables:
1. **Pipelining**: Multiple segments in flight
2. **Flow Control**: Receiver advertises available buffer space

```
Sender's View:
                        Window (what we can send)
                    ├─────────────────────────────┤
    ────┬───────────┼───────────────┬─────────────┼───────────────────────
        │  ACKed    │  Sent, not    │   Can send  │    Cannot send
        │           │    ACKed      │             │     (outside window)
    ────┴───────────┴───────────────┴─────────────┴───────────────────────
                  SND.UNA         SND.NXT

Receiver's View:
                    Receive Window
                ├─────────────────────────┤
    ────────────┼─────────────────────────┼───────────────────────────────
      Received  │   Acceptable            │   Not acceptable
      & ACKed   │   (within window)       │   (outside window)
    ────────────┴─────────────────────────┴───────────────────────────────
              RCV.NXT
```

### Retransmission and RTT Estimation

**Problem**: How long to wait before retransmitting?

**Solution**: Estimate Round-Trip Time (RTT) and compute Retransmission Timeout (RTO).

```
Jacobson/Karels Algorithm (RFC 6298):

On receiving ACK for new data:
    SampleRTT = current_time - time_segment_was_sent

    If first measurement:
        SRTT = SampleRTT
        RTTVAR = SampleRTT / 2
    Else:
        RTTVAR = (1 - β) * RTTVAR + β * |SampleRTT - SRTT|    (β = 1/4)
        SRTT = (1 - α) * SRTT + α * SampleRTT                 (α = 1/8)

    RTO = SRTT + max(G, 4 * RTTVAR)    (G = clock granularity)

Karn's Algorithm:
    Don't update RTT estimates from retransmitted segments
    (can't tell if ACK is for original or retransmission)

Exponential Backoff:
    On timeout: RTO = RTO * 2 (up to maximum)
```

### Congestion Control

Prevents sender from overwhelming the **network** (not just the receiver).

**Key Variables:**
- `cwnd`: Congestion window (sender's estimate of network capacity)
- `ssthresh`: Slow start threshold
- `rwnd`: Receiver's advertised window

**Effective Window** = min(cwnd, rwnd)

#### Slow Start

```
Initial: cwnd = 1 MSS (or 2-4 MSS per RFC)

For each ACK received:
    cwnd = cwnd + MSS

Effect: cwnd doubles every RTT (exponential growth)
Continue until: cwnd >= ssthresh, then switch to Congestion Avoidance
```

#### Congestion Avoidance

```
For each ACK received:
    cwnd = cwnd + MSS * MSS / cwnd

Effect: cwnd increases by ~1 MSS per RTT (linear growth)
```

#### On Timeout (Packet Loss)

```
ssthresh = cwnd / 2
cwnd = 1 MSS
Return to Slow Start
```

#### Fast Retransmit / Fast Recovery

```
On receiving 3 duplicate ACKs:
    ssthresh = cwnd / 2
    Retransmit the missing segment immediately (don't wait for timeout)
    cwnd = ssthresh + 3 MSS
    Enter Fast Recovery

During Fast Recovery:
    For each additional duplicate ACK:
        cwnd = cwnd + MSS

On receiving new ACK:
    cwnd = ssthresh
    Exit Fast Recovery, enter Congestion Avoidance
```

```
       cwnd
        │
        │          Timeout
        │             │
        │    ┌────────┼──────┐
        │   /         │       \
        │  / Slow    │        \ Congestion
        │ /  Start    │         \ Avoidance
        │/            │          \
        ├─────────────┴───────────┴─────────► time
        │            ssthresh
        │
```

---

## Protocol Control Blocks (PCBs)

A PCB stores all state for a connection/socket.

### UDP PCB (Simple)

```c
struct udp_pcb {
    uint32_t local_ip;      // Bound IP (0 = any)
    uint16_t local_port;    // Bound port
    uint32_t remote_ip;     // Connected remote (0 = any)
    uint16_t remote_port;   // Connected remote port (0 = any)

    struct list recv_queue; // Received datagrams
    struct semaphore recv_sem;
    struct lock lock;

    bool bound;
    struct list_elem elem;  // In global PCB list
};
```

### TCP PCB (Complex)

```c
struct tcp_pcb {
    // Connection identification
    uint32_t local_ip, remote_ip;
    uint16_t local_port, remote_port;

    // State machine
    enum tcp_state state;

    // Send sequence space
    uint32_t snd_una;   // Oldest unACKed byte
    uint32_t snd_nxt;   // Next byte to send
    uint32_t snd_wnd;   // Send window (from receiver)
    uint32_t iss;       // Initial send sequence number

    // Receive sequence space
    uint32_t rcv_nxt;   // Next expected byte
    uint32_t rcv_wnd;   // Receive window (our buffer space)
    uint32_t irs;       // Initial receive sequence number

    // Buffers
    struct send_buffer *send_buf;   // Retransmission buffer
    struct recv_buffer *recv_buf;   // Reassembly buffer

    // Timers
    int64_t rto;        // Retransmission timeout
    int64_t srtt;       // Smoothed RTT
    int64_t rttvar;     // RTT variance

    // Congestion control
    uint32_t cwnd;      // Congestion window
    uint32_t ssthresh;  // Slow start threshold

    // Synchronization
    struct lock lock;
    struct condition connect_cond;
    struct condition accept_cond;
    struct condition send_cond;
    struct condition recv_cond;

    // For listening sockets
    struct list accept_queue;
    int backlog;

    struct list_elem elem;
};
```

### PCB Lifecycle

```
UDP:
    create → bind → [connect] → send/recv → close

TCP Server:
    create → bind → listen → accept → send/recv → close

TCP Client:
    create → [bind] → connect → send/recv → close
```

---

## Port Demultiplexing

When a packet arrives, find the correct PCB:

### UDP Demultiplexing

```c
struct udp_pcb *udp_find_pcb(uint32_t local_ip, uint16_t local_port,
                              uint32_t remote_ip, uint16_t remote_port) {
    // Search PCB list for best match:
    // 1. Exact match (all four values match)
    // 2. Connected to any remote (remote_ip/port = 0)
    // 3. Bound to any local IP (local_ip = 0)
}
```

### TCP Demultiplexing

```c
struct tcp_pcb *tcp_find_pcb(uint32_t local_ip, uint16_t local_port,
                              uint32_t remote_ip, uint16_t remote_port) {
    // For ESTABLISHED connections: exact 4-tuple match
    // For LISTEN sockets: match local port, any remote
}
```

### Port Allocation

When binding to port 0, allocate an ephemeral port:

```c
uint16_t allocate_ephemeral_port(void) {
    static uint16_t next_port = 49152;  // Start of ephemeral range

    // Find unused port in range 49152-65535
    for (int i = 0; i < 16384; i++) {
        uint16_t port = next_port++;
        if (next_port > 65535) next_port = 49152;

        if (!port_in_use(port))
            return port;
    }
    return 0;  // All ports in use
}
```

---

## Concurrency and Synchronization

### The Network Input Thread

Packets arrive asynchronously via the network input thread:

```c
// In net/net.c - already implemented
void net_input_thread(void *aux) {
    while (true) {
        // Poll all devices for received packets
        for each device:
            while ((p = netdev_receive(dev)) != NULL)
                ethernet_input(dev, p);

        // Yield to other threads
        thread_yield();
    }
}
```

This thread calls `tcp_input()` / `udp_input()` - your code runs in this context!

### Synchronization Strategy

```
┌─────────────────────────────────────────────────────────────┐
│                     User Thread                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐         │
│  │   send()    │  │   recv()    │  │  connect()  │         │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘         │
│         │                │                │                 │
│         ▼                ▼                ▼                 │
│  ┌────────────────────────────────────────────────────────┐ │
│  │                    PCB Lock                            │ │
│  │                                                        │ │
│  │   - Modify PCB state                                   │ │
│  │   - Access send/recv buffers                           │ │
│  │   - Wait on condition variables                        │ │
│  │                                                        │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
                              │ (shared PCB)
                              │
┌─────────────────────────────────────────────────────────────┐
│                  Network Input Thread                       │
│                                                             │
│  ┌─────────────────┐                                        │
│  │  tcp_input()    │                                        │
│  │  udp_input()    │                                        │
│  └────────┬────────┘                                        │
│           │                                                 │
│           ▼                                                 │
│  ┌────────────────────────────────────────────────────────┐ │
│  │                    PCB Lock                            │ │
│  │                                                        │ │
│  │   - Update sequence numbers                            │ │
│  │   - Queue received data                                │ │
│  │   - Signal condition variables                         │ │
│  │   - Change TCP state                                   │ │
│  │                                                        │ │
│  └────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

### Locking Rules

1. **One lock per PCB**: Protects all PCB fields
2. **Hold lock briefly**: Especially in input path
3. **No lock for global PCB list**: Use separate lock
4. **Lock ordering**: Always acquire global list lock before PCB lock

### Blocking Operations

```c
// Example: TCP recv() blocking until data available
int tcp_recv(struct tcp_pcb *pcb, void *buf, size_t len) {
    lock_acquire(&pcb->lock);

    // Wait for data or connection close
    while (recv_buffer_empty(pcb) && pcb->state == TCP_ESTABLISHED) {
        cond_wait(&pcb->recv_cond, &pcb->lock);
    }

    // Copy data from receive buffer
    int bytes = copy_from_recv_buffer(pcb, buf, len);

    lock_release(&pcb->lock);
    return bytes;
}

// In tcp_input(), after queuing data:
lock_acquire(&pcb->lock);
queue_received_data(pcb, p);
cond_signal(&pcb->recv_cond, &pcb->lock);  // Wake up blocked recv()
lock_release(&pcb->lock);
```

---

## Buffer Management

### Send Buffer (TCP)

Stores data until acknowledged:

```
┌─────────────────────────────────────────────────────────────┐
│                       Send Buffer                           │
├─────────────────────────────────────────────────────────────┤
│   ◄── ACKed ──►│◄── Sent, not ACKed ──►│◄── Not sent ──►   │
│   (can free)   │  (may retransmit)      │  (waiting)        │
├────────────────┼────────────────────────┼───────────────────┤
│                │                        │                   │
│  seq 1000-1999 │    seq 2000-3999      │   seq 4000-4999   │
│                │                        │                   │
└────────────────┴────────────────────────┴───────────────────┘
                 ▲                        ▲
              SND.UNA                  SND.NXT
```

**Operations:**
- `send()`: Append to end, update SND.NXT
- `ACK received`: Free ACKed data, advance SND.UNA
- `Timeout`: Retransmit from SND.UNA

### Receive Buffer (TCP)

Handles out-of-order segments and application reads:

```
┌─────────────────────────────────────────────────────────────┐
│                      Receive Buffer                         │
├─────────────────────────────────────────────────────────────┤
│   ◄── Read by app ──►│◄── Received, not read ──►│◄ Gap ►│  │
│   (can free)          │  (contiguous from RCV.NXT)│ (hole)│  │
├───────────────────────┼──────────────────────────┼────────┤  │
│                       │                          │        │  │
│                       │    seq 5000-5999        │ 6000-? │  │
│                       │                          │        │  │
└───────────────────────┴──────────────────────────┴────────┴──┘
                        ▲
                     RCV.NXT
```

**Operations:**
- `Segment received`: Insert at correct position, advance RCV.NXT if contiguous
- `recv()`: Copy to user, free space, advance read pointer

### Receive Buffer (UDP)

Simpler - just a queue of datagrams:

```c
struct udp_datagram {
    uint32_t src_ip;
    uint16_t src_port;
    struct pbuf *p;
    struct list_elem elem;
};

// On receive:
datagram->p = p;
list_push_back(&pcb->recv_queue, &datagram->elem);
sema_up(&pcb->recv_sem);

// On recv():
sema_down(&pcb->recv_sem);
datagram = list_pop_front(&pcb->recv_queue);
copy data to user buffer
pbuf_free(datagram->p);
```

---

## Timer Management

### TCP Timers

| Timer | Purpose | Action on Expiry |
|-------|---------|------------------|
| Retransmission | Detect lost segments | Retransmit, backoff RTO |
| TIME_WAIT | Allow old segments to expire | Delete TCB |
| Keepalive | Detect dead connections | Send probe or close |
| Persist | Probe zero window | Send window probe |

### Implementation Approach

```c
// Option 1: Per-PCB timer thread (simple but expensive)
// Option 2: Global timer thread checking all PCBs (recommended)

void tcp_timer_thread(void *aux) {
    while (true) {
        timer_sleep(100);  // Check every 100ms

        for each pcb in pcb_list:
            lock_acquire(&pcb->lock);

            if (pcb->state != TCP_CLOSED) {
                // Check retransmission timer
                if (time_since_last_send > pcb->rto) {
                    tcp_retransmit(pcb);
                    pcb->rto *= 2;  // Exponential backoff
                }

                // Check TIME_WAIT timer
                if (pcb->state == TCP_TIME_WAIT &&
                    time_in_state > 2 * MSL) {
                    pcb->state = TCP_CLOSED;
                    // Clean up PCB
                }
            }

            lock_release(&pcb->lock);
    }
}
```

---

## Error Handling

### Connection Reset (RST)

Send RST when:
- Receiving segment for non-existent connection
- Receiving segment in invalid state
- Application aborts connection

On receiving RST:
- Abort connection immediately
- Wake up any blocked operations with error

### ICMP Errors

The IP layer may receive ICMP errors (destination unreachable, etc.):

```c
void icmp_error_handler(uint32_t src_ip, uint32_t dst_ip,
                        uint8_t type, uint8_t code,
                        void *original_ip_header) {
    // Extract protocol and ports from original header
    // Find matching PCB
    // Report error to application
}
```

### Error Propagation

```c
// In recv(), after waking up:
if (pcb->error != 0) {
    int err = pcb->error;
    pcb->error = 0;
    lock_release(&pcb->lock);
    return -err;  // Return negative error code
}
```

---

## Implementation Order

### Suggested Order

1. **UDP First** (simpler, validates lower layers)
   - PCB structure and list
   - Port binding
   - udp_output() - send path
   - udp_input() - receive path
   - Blocking recv with semaphore

2. **TCP Phase A - State Machine**
   - PCB structure
   - Connection establishment (3-way handshake)
   - tcp_connect() - client side
   - tcp_listen() + tcp_accept() - server side
   - Connection termination (4-way close)

3. **TCP Phase B - Data Transfer**
   - Send/receive buffers
   - tcp_send() - basic sending
   - tcp_recv() - basic receiving
   - Sequence number handling
   - Simple retransmission (timeout only)

4. **TCP Phase C - Timers**
   - RTT estimation
   - Adaptive RTO
   - TIME_WAIT handling

5. **TCP Phase D - Congestion Control**
   - Slow start
   - Congestion avoidance
   - Fast retransmit/recovery

---

## Quick Reference: TCP Sequence Number Arithmetic

Sequence numbers are 32-bit and wrap around. Use these macros for comparison:

```c
// Is seq1 < seq2 (accounting for wraparound)?
#define SEQ_LT(seq1, seq2)  ((int32_t)((seq1) - (seq2)) < 0)
#define SEQ_LEQ(seq1, seq2) ((int32_t)((seq1) - (seq2)) <= 0)
#define SEQ_GT(seq1, seq2)  ((int32_t)((seq1) - (seq2)) > 0)
#define SEQ_GEQ(seq1, seq2) ((int32_t)((seq1) - (seq2)) >= 0)

// Examples:
// SEQ_LT(0xFFFFFFFF, 0x00000001) == true  (wraparound)
// SEQ_LT(100, 200) == true
// SEQ_LT(200, 100) == false
```

---

## References

- RFC 793: Transmission Control Protocol
- RFC 768: User Datagram Protocol
- RFC 6298: Computing TCP's Retransmission Timer
- RFC 5681: TCP Congestion Control
- Stevens, "TCP/IP Illustrated, Volume 1"
- Stevens, "Unix Network Programming, Volume 1"
