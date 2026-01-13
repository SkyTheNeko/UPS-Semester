/**
 * @file net.h
 * @brief Low-level networking helpers
 * 
 * Seminar work of "Fundamentals of Computer Networks"
 */

#ifndef NET_H
#define NET_H

#pragma once
#include <stddef.h>

/**
 * @brief Creates and binds a listening TCP socket
 *
 * @param ip    IP address to bind
 * @param port  TCP port
 *
 * @return Listening socket fd, or -1 on error
 */
int net_listen(const char* ip, int port);

/**
 * @brief Sets a socket to non-blocking mode
 *
 * @param fd    Socket file descriptor
 *
 * @return 0 on success, -1 on error
 */
int net_set_nonblock(int fd);

/**
 * @brief Sends all data through a socket
 *
 * @param fd    Socket file descriptor
 * @param data  Buffer to send
 * @param len   Length of data
 *
 * @return 0 on success, -1 on error
 */
int net_send_all(int fd, const char* data, size_t len);

#endif
