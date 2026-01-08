/**
 * @file net/driver/loopback.h
 * @brief Loopback network device.
 *
 * The loopback device (lo) sends packets directly back to the
 * receive queue, useful for testing and local communication.
 */

#ifndef NET_DRIVER_LOOPBACK_H
#define NET_DRIVER_LOOPBACK_H

/**
 * @brief Initialize and register the loopback device.
 *
 * Creates "lo" device with IP 127.0.0.1.
 */
void loopback_init(void);

#endif /* NET_DRIVER_LOOPBACK_H */
