// #include <arpa/inet.h> /* htons */
// #include <errno.h>
// #include <netinet/in.h>  // IPPROTO_TCP, IPPROTO_UDP
// #include <netinet/tcp.h> // TCP_NODELAY
// #include <netdb.h>
// #include <stdbool.h>
// #include <stdio.h>
// #include <stdlib.h>
// #include <string.h>
// #include <sys/socket.h>
// #include <sys/types.h>
// #include <unistd.h>

// #include "util.h"
// #include "net_common.h"
// #include "net_util.h"
// #include "socket_common.h"

#include <unistd.h>      // Defines read(), write(), and close()
#include <netinet/in.h>  // IPPROTO_TCP, IPPROTO_UDP
#include <netinet/tcp.h> // TCP_NODELAY
#include <errno.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdarg.h> //va_list
#include <string.h> // strerror

#include "util.h"
#include "socket_common.h"

#ifndef NUMBER_OF_FEDERATES
#define NUMBER_OF_FEDERATES 1
#endif

/** Number of nanoseconds to sleep before retrying a socket read. */
#define SOCKET_READ_RETRY_INTERVAL 1000000

// Mutex lock held while performing socket close operations.
// A deadlock can occur if two threads simulataneously attempt to close the same socket.
lf_mutex_t socket_mutex;

int create_real_time_tcp_socket_errexit() {
  int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0) {
    lf_print_error_system_failure("Could not open TCP socket.");
  }
  // Disable Nagle's algorithm which bundles together small TCP messages to
  // reduce network traffic.
  // TODO: Re-consider if we should do this, and whether disabling delayed ACKs
  // is enough.
  int flag = 1;
  int result = setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));

  if (result < 0) {
    lf_print_error_system_failure("Failed to disable Nagle algorithm on socket server.");
  }

#if defined(PLATFORM_Linux)
  // Disable delayed ACKs. Only possible on Linux
  result = setsockopt(sock, IPPROTO_TCP, TCP_QUICKACK, &flag, sizeof(int));

  if (result < 0) {
    lf_print_error_system_failure("Failed to disable Nagle algorithm on socket server.");
  }
#endif // Linux

  return sock;
}

/**
 * Set the socket timeout options.
 * @param socket_descriptor
 * @param timeout_time
 */
static void set_socket_timeout_option(int socket_descriptor, struct timeval* timeout_time) {
  // Set the option for this socket to reuse the same address
  int true_variable = 1; // setsockopt() requires a reference to the value assigned to an option
  if (setsockopt(socket_descriptor, SOL_SOCKET, SO_REUSEADDR, &true_variable, sizeof(int32_t)) < 0) {
    lf_print_error("RTI failed to set SO_REUSEADDR option on the socket: %s.", strerror(errno));
  }
  // Set the timeout on the socket so that read and write operations don't block for too long
  if (setsockopt(socket_descriptor, SOL_SOCKET, SO_RCVTIMEO, (const char*)timeout_time, sizeof(*timeout_time)) < 0) {
    lf_print_error("RTI failed to set SO_RCVTIMEO option on the socket: %s.", strerror(errno));
  }
  if (setsockopt(socket_descriptor, SOL_SOCKET, SO_SNDTIMEO, (const char*)timeout_time, sizeof(*timeout_time)) < 0) {
    lf_print_error("RTI failed to set SO_SNDTIMEO option on the socket: %s.", strerror(errno));
  }
}

/**
 * Set the socket bind options. If the specified port is 0, it means this is a federate socket server. If the specified
 * port is 1, it is creating a RTI server. RTI servers use the port increment when the default port is not available.
 * Returns the actually used port.
 *
 * @param socket_descriptor
 * @param specified_port
 * @return The final port number used.
 */
static int set_socket_bind_option(int socket_descriptor, uint16_t specified_port) {
  // Server file descriptor.
  struct sockaddr_in server_fd;
  // Zero out the server address structure.
  bzero((char*)&server_fd, sizeof(server_fd));
  uint16_t used_port = specified_port;
  if (specified_port == 1) { // RTI is set to 1 if no specified port.
    used_port = DEFAULT_PORT;
  }
  server_fd.sin_family = AF_INET;         // IPv4
  server_fd.sin_addr.s_addr = INADDR_ANY; // All interfaces, 0.0.0.0.
  server_fd.sin_port = htons(used_port);  // Convert the port number from host byte order to network byte order.

  int result = bind(socket_descriptor, (struct sockaddr*)&server_fd, sizeof(server_fd));

  // Try repeatedly to bind to a port. If no specific port is specified, then
  // increment the port number each time.

  int count = 1;
  while (result != 0 && count++ < PORT_BIND_RETRY_LIMIT) {
    if (specified_port == 1) { // RTI is set to 1 if no specified port.
      lf_print_warning("RTI failed to get port %d.", used_port);
      used_port++;
      if (used_port >= DEFAULT_PORT + MAX_NUM_PORT_ADDRESSES)
        used_port = DEFAULT_PORT;
      lf_print_warning("RTI will try again with port %d.", used_port);
      server_fd.sin_port = htons(used_port);
      // Do not sleep.
    } else {
      lf_print("Failed to bind socket on port %d. Will try again.", used_port);
      lf_sleep(PORT_BIND_RETRY_INTERVAL);
    }
    result = bind(socket_descriptor, (struct sockaddr*)&server_fd, sizeof(server_fd));
  }

  // Set the global server port.
  if (specified_port == 0) { // Federates are set to 0 if no specified port.
    // Need to retrieve the port number assigned by the OS.
    struct sockaddr_in assigned;
    socklen_t addr_len = sizeof(assigned);
    if (getsockname(socket_descriptor, (struct sockaddr*)&assigned, &addr_len) < 0) {
      lf_print_error_and_exit("Failed to retrieve assigned port number.");
    }
    used_port = ntohs(assigned.sin_port);
  }
  if (result != 0) {
    lf_print_error_and_exit("Failed to bind the RTI socket. Port %d is not available. ", used_port);
  }
  return used_port;
}

void create_TCP_server(uint16_t port, int* final_socket, uint16_t* final_port) {
  // Create an IPv4 socket for TCP.
  int socket_descriptor = create_real_time_tcp_socket_errexit();
  if (socket_descriptor < 0) {
    lf_print_error_system_failure("Failed to create TCP socket.");
  }
  // Set the timeout time for the communications of the server
  struct timeval timeout_time =
      (struct timeval){.tv_sec = TCP_TIMEOUT_TIME / BILLION, .tv_usec = (TCP_TIMEOUT_TIME % BILLION) / 1000};
  set_socket_timeout_option(socket_descriptor, &timeout_time);
  int used_port = set_socket_bind_option(socket_descriptor, port);

  // Enable listening for socket connections.
  // The second argument is the maximum number of queued socket requests,
  // which according to the Mac man page is limited to 128.
  listen(socket_descriptor, 128);
  *final_socket = socket_descriptor;
  *final_port = used_port;
}

void create_UDP_server(uint16_t port, int* final_socket, uint16_t* final_port) {
  // Create a UDP socket.
  int socket_descriptor = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socket_descriptor < 0) {
    lf_print_error_system_failure("Failed to create UDP socket.");
  }
  // Set the timeout time for the communications of the server
  struct timeval timeout_time =
      (struct timeval){.tv_sec = UDP_TIMEOUT_TIME / BILLION, .tv_usec = (UDP_TIMEOUT_TIME % BILLION) / 1000};
  set_socket_timeout_option(socket_descriptor, &timeout_time);
  int used_port = set_socket_bind_option(socket_descriptor, port);
  *final_socket = socket_descriptor;
  *final_port = used_port;
}

int accept_socket(int socket, struct sockaddr* client_fd) {
  // Wait for an incoming connection request.
  uint32_t client_length = sizeof(*client_fd);
  // The following blocks until a federate connects.
  int socket_id = -1;
  while (1) {
    socket_id = accept(socket, client_fd, &client_length);
    if (socket_id >= 0) {
      // Got a socket
      break;
    } else if (socket_id < 0 && (errno != EAGAIN || errno != EWOULDBLOCK)) {
      lf_print_error_system_failure("RTI failed to accept the socket.");
    } else {
      // Try again
      lf_print_warning("RTI failed to accept the socket. %s. Trying again.", strerror(errno));
      continue;
    }
  }
  return socket_id;
}

int read_from_socket(int socket, size_t num_bytes, unsigned char* buffer) {
  if (socket < 0) {
    // Socket is not open.
    errno = EBADF;
    return -1;
  }
  ssize_t bytes_read = 0;
  while (bytes_read < (ssize_t)num_bytes) {
    ssize_t more = read(socket, buffer + bytes_read, num_bytes - (size_t)bytes_read);
    if (more < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      // Those error codes set by the socket indicates
      // that we should try again (@see man errno).
      LF_PRINT_DEBUG("Reading from socket %d failed with error: `%s`. Will try again.", socket, strerror(errno));
      lf_sleep(DELAY_BETWEEN_SOCKET_RETRIES);
      continue;
    } else if (more < 0) {
      // A more serious error occurred.
      lf_print_error("Reading from socket %d failed. With error: `%s`", socket, strerror(errno));
      return -1;
    } else if (more == 0) {
      // EOF received.
      return 1;
    }
    bytes_read += more;
  }
  return 0;
}

int read_from_socket_close_on_error(int* socket, size_t num_bytes, unsigned char* buffer) {
  assert(socket);
  int read_failed = read_from_socket(*socket, num_bytes, buffer);
  if (read_failed) {
    // Read failed.
    // Socket has probably been closed from the other side.
    // Shut down and close the socket from this side.
    shutdown(*socket, SHUT_RDWR);
    close(*socket);
    // Mark the socket closed.
    *socket = -1;
    return -1;
  }
  return 0;
}

void read_from_socket_fail_on_error(int* socket, size_t num_bytes, unsigned char* buffer, lf_mutex_t* mutex,
                                    char* format, ...) {
  va_list args;
  assert(socket);
  int read_failed = read_from_socket_close_on_error(socket, num_bytes, buffer);
  if (read_failed) {
    // Read failed.
    if (mutex != NULL) {
      LF_MUTEX_UNLOCK(mutex);
    }
    if (format != NULL) {
      va_start(args, format);
      lf_print_error_system_failure(format, args);
      va_end(args);
    } else {
      lf_print_error_system_failure("Failed to read from socket.");
    }
  }
}

ssize_t peek_from_socket(int socket, unsigned char* result) {
  ssize_t bytes_read = recv(socket, result, 1, MSG_DONTWAIT | MSG_PEEK);
  if (bytes_read < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return 0;
  else
    return bytes_read;
}

int write_to_socket(int socket, size_t num_bytes, unsigned char* buffer) {
  if (socket < 0) {
    // Socket is not open.
    errno = EBADF;
    return -1;
  }
  ssize_t bytes_written = 0;
  while (bytes_written < (ssize_t)num_bytes) {
    ssize_t more = write(socket, buffer + bytes_written, num_bytes - (size_t)bytes_written);
    if (more <= 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
      // The error codes EAGAIN or EWOULDBLOCK indicate
      // that we should try again (@see man errno).
      // The error code EINTR means the system call was interrupted before completing.
      LF_PRINT_DEBUG("Writing to socket %d was blocked. Will try again.", socket);
      lf_sleep(DELAY_BETWEEN_SOCKET_RETRIES);
      continue;
    } else if (more < 0) {
      // A more serious error occurred.
      lf_print_error("Writing to socket %d failed. With error: `%s`", socket, strerror(errno));
      return -1;
    }
    bytes_written += more;
  }
  return 0;
}

int write_to_socket_close_on_error(int* socket, size_t num_bytes, unsigned char* buffer) {
  assert(socket);
  int result = write_to_socket(*socket, num_bytes, buffer);
  if (result) {
    // Write failed.
    // Socket has probably been closed from the other side.
    // Shut down and close the socket from this side.
    shutdown(*socket, SHUT_RDWR);
    close(*socket);
    // Mark the socket closed.
    *socket = -1;
  }
  return result;
}

void write_to_socket_fail_on_error(int* socket, size_t num_bytes, unsigned char* buffer, lf_mutex_t* mutex,
                                   char* format, ...) {
  va_list args;
  assert(socket);
  int result = write_to_socket_close_on_error(socket, num_bytes, buffer);
  if (result) {
    // Write failed.
    if (mutex != NULL) {
      LF_MUTEX_UNLOCK(mutex);
    }
    if (format != NULL) {
      va_start(args, format);
      lf_print_error_system_failure(format, args);
      va_end(args);
    } else {
      lf_print_error("Failed to write to socket. Closing it.");
    }
  }
}

int connect_to_socket(int sock, const char* hostname, int port) {
  struct addrinfo hints;
  struct addrinfo* result;
  int ret = -1;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;       /* Allow IPv4 */
  hints.ai_socktype = SOCK_STREAM; /* Stream socket */
  hints.ai_protocol = IPPROTO_TCP; /* TCP protocol */
  hints.ai_addr = NULL;
  hints.ai_next = NULL;
  hints.ai_flags = AI_NUMERICSERV; /* Allow only numeric port numbers */

  uint16_t used_port = (port == 0) ? DEFAULT_PORT : (uint16_t) port;

  instant_t start_connect = lf_time_physical();
  // while (!_lf_termination_executed) { // Not working...
  while (1) {
    if (CHECK_TIMEOUT(start_connect, CONNECT_TIMEOUT)) {
      lf_print_error("Failed to connect with timeout: " PRINTF_TIME ". Giving up.", CONNECT_TIMEOUT);
      break;
    }
    // Convert port number to string.
    char str[6];
    sprintf(str, "%u", used_port);

    // Get address structure matching hostname and hints criteria, and
    // set port to the port number provided in str. There should only
    // ever be one matching address structure, and we connect to that.
    if (getaddrinfo(hostname, (const char*)&str, &hints, &result)) {
      lf_print_error("No host matching given hostname: %s", hostname);
      break;
    }
    ret = connect(sock, result->ai_addr, result->ai_addrlen);
    if (ret < 0) {
      lf_sleep(CONNECT_RETRY_INTERVAL);
      if (port == 0) {
        used_port++;
        if (used_port >= DEFAULT_PORT + MAX_NUM_PORT_ADDRESSES) {
          used_port = DEFAULT_PORT;
        }
      }
      lf_print_warning("Could not connect. Will try again every " PRINTF_TIME " nanoseconds.\n",
                       CONNECT_RETRY_INTERVAL);
      continue;
    } else {
      break;
    }
    freeaddrinfo(result);
  }
  lf_print("Connected to %s:%d.", hostname, used_port);
  return ret;
}
