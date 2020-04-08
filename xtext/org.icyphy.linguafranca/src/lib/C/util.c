/**
 * @file
 * @author Edward A. Lee (eal@berkeley.edu)
 *
 * @section LICENSE
Copyright (c) 2020, The University of California at Berkeley.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

 * @section DESCRIPTION
 * Utility functions for a federate in a federated execution.
 */

/** Print the error defined by the errno variable with the
 *  specified message as a prefix, then exit with error code 1.
 *  @param msg The prefix to the message.
 */
void error(char *msg) {
    perror(msg);
    exit(1);
}

#define HOST_LITTLE_ENDIAN 1
#define HOST_BIG_ENDIAN 2

/** Return true (1) if the host is big endian. Otherwise,
 *  return false.
 */
int host_is_big_endian() {
    static int host = 0;
    union {
        int uint;
        unsigned char c[4];
    } x;
    if (host == 0) {
        // Determine the endianness of the host by setting the low-order bit.
        x.uint = 0x01;
        host = (x.c[3] == 0x01) ? HOST_BIG_ENDIAN : HOST_LITTLE_ENDIAN;
    }
    return (host == HOST_BIG_ENDIAN);
}

// Error messages.
char* ERROR_DISCONNECTED = "ERROR socket is not connected";
char* ERROR_EOF = "ERROR peer sent EOF";

/** Read the specified number of bytes from the specified socket into the
 *  specified buffer. If a disconnect or an EOF occurs during this
 *  reading, report an error and exit.
 *  @param socket The socket ID.
 *  @param num_bytes The number of bytes to read.
 *  @param buffer The buffer into which to put the bytes.
 */
void read_from_socket(int socket, int num_bytes, unsigned char* buffer) {
    int bytes_read = 0;
    while (bytes_read < num_bytes) {
        int more = read(socket, buffer + bytes_read, num_bytes - bytes_read);
        if (more < 0) error(ERROR_DISCONNECTED);
        if (more == 0) error(ERROR_EOF);
        bytes_read += more;
    }
}

/** Write the specified number of bytes to the specified socket from the
 *  specified buffer. If a disconnect or an EOF occurs during this
 *  reading, report an error and exit.
 *  @param socket The socket ID.
 *  @param num_bytes The number of bytes to write.
 *  @param buffer The buffer from which to get the bytes.
 */
void write_to_socket(int socket, int num_bytes, unsigned char* buffer) {
    int bytes_written = 0;
    while (bytes_written < num_bytes) {
        int more = write(socket, buffer + bytes_written, num_bytes - bytes_written);
        if (more < 0) error(ERROR_DISCONNECTED);
        if (more == 0) error(ERROR_EOF);
        bytes_written += more;
    }
}

/** If this host is little endian, then reverse the order of
 *  the bytes of the argument. Otherwise, return the argument
 *  unchanged. This can be used to convert the argument to
 *  network order (big endian) and then back again.
 *  Network transmissions, by convention, are big endian,
 *  meaning that the high-order byte is sent first.
 *  But many platforms, including my Mac, are little endian,
 *  meaning that the low-order byte is first in memory.
 *  @param src The argument to convert.
 */
int swap_bytes_if_big_endian_int(int src) {
    union {
        int uint;
        unsigned char c[4];
    } x;
    if (!host_is_big_endian()) return src;
    // printf("DEBUG: Host is little endian.\n");
    x.uint = src;
    // printf("DEBUG: Before swapping bytes: %lld.\n", x.ull);
    unsigned char c;
    // Swap bytes.
    c = x.c[0]; x.c[0] = x.c[3]; x.c[3] = c;
    c = x.c[1]; x.c[1] = x.c[2]; x.c[2] = c;
    // printf("DEBUG: After swapping bytes: %lld.\n", x.ull);
    return x.uint;
}

/** If this host is little endian, then reverse the order of
 *  the bytes of the argument. Otherwise, return the argument
 *  unchanged. This can be used to convert the argument to
 *  network order (big endian) and then back again.
 *  Network transmissions, by convention, are big endian,
 *  meaning that the high-order byte is sent first.
 *  But many platforms, including my Mac, are little endian,
 *  meaning that the low-order byte is first in memory.
 *  @param src The argument to convert.
 */
long long swap_bytes_if_big_endian_ll(long long src) {
    union {
        long long ull;
        unsigned char c[8];
    } x;
    if (!host_is_big_endian()) return src;
    // printf("DEBUG: Host is little endian.\n");
    x.ull = src;
    // printf("DEBUG: Before swapping bytes: %lld.\n", x.ull);
    unsigned char c;
    // Swap bytes.
    c = x.c[0]; x.c[0] = x.c[7]; x.c[7] = c;
    c = x.c[1]; x.c[1] = x.c[6]; x.c[6] = c;
    c = x.c[2]; x.c[2] = x.c[5]; x.c[5] = c;
    c = x.c[3]; x.c[3] = x.c[4]; x.c[4] = c;
    // printf("DEBUG: After swapping bytes: %lld.\n", x.ull);
    return x.ull;
}

/** If this host is little endian, then reverse the order of
 *  the bytes of the argument. Otherwise, return the argument
 *  unchanged. This can be used to convert the argument to
 *  network order (big endian) and then back again.
 *  Network transmissions, by convention, are big endian,
 *  meaning that the high-order byte is sent first.
 *  But many platforms, including my Mac, are little endian,
 *  meaning that the low-order byte is first in memory.
 *  @param src The argument to convert.
 */
int swap_bytes_if_big_endian_ushort(unsigned short src) {
    union {
        unsigned short uint;
        unsigned char c[2];
    } x;
    if (!host_is_big_endian()) return src;
    // printf("DEBUG: Host is little endian.\n");
    x.uint = src;
    // printf("DEBUG: Before swapping bytes: %lld.\n", x.ull);
    unsigned char c;
    // Swap bytes.
    c = x.c[0]; x.c[0] = x.c[1]; x.c[1] = c;
    // printf("DEBUG: After swapping bytes: %lld.\n", x.ull);
    return x.uint;
}

/** Extract an int from the specified byte sequence.
 *  This will swap the order of the bytes if this machine is big endian.
 *  @param bytes The address of the start of the sequence of bytes.
 */
int extract_int(unsigned char* bytes) {
    union {
        int uint;
        unsigned char c[sizeof(int)];
    } result;
    memcpy(&result.c, bytes, sizeof(int));
    return swap_bytes_if_big_endian_int(result.uint);
}

/** Extract a long long from the specified byte sequence.
 *  This will swap the order of the bytes if this machine is big endian.
 *  @param bytes The address of the start of the sequence of bytes.
 */
long long extract_ll(unsigned char* bytes) {
    union {
        long long ull;
        unsigned char c[sizeof(long long)];
    } result;
    memcpy(&result.c, bytes, sizeof(long long));
    return swap_bytes_if_big_endian_ll(result.ull);
}

/** Extract an unsigned short from the specified byte sequence.
 *  This will swap the order of the bytes if this machine is big endian.
 *  @param bytes The address of the start of the sequence of bytes.
 */
unsigned short extract_ushort(unsigned char* bytes) {
    union {
        unsigned short ushort;
        unsigned char c[sizeof(unsigned short)];
    } result;
    memcpy(&result.c, bytes, sizeof(unsigned short));
    return swap_bytes_if_big_endian_ushort(result.ushort);
}

/** Extract the core header information that all messages between
 *  federates share. The core header information is two bytes with
 *  the ID of the destination port, two bytes with the ID of the destination
 *  federate, and four bytes with the length of the message.
 *  @param buffer The buffer to read from.
 *  @param port_id The place to put the port ID.
 *  @param federate_id The place to put the federate ID.
 *  @param length The place to put the length.
 */
void extract_header(
        unsigned char* buffer,
        unsigned short* port_id,
        unsigned short* federate_id,
        unsigned int* length
) {
    // The first two bytes are the ID of the destination reactor.
    *port_id = extract_ushort(buffer);
    // The next four bytes are the message length.
    // The next two bytes are the ID of the destination federate.
    *federate_id = extract_ushort(buffer + 2);

    // printf("DEBUG: Message for port %d of federate %d.\n", *port_id, *federate_id);
    // FIXME: Better error handling needed here.
    assert(*federate_id < NUMBER_OF_FEDERATES);
    // The next four bytes are the message length.
    *length = extract_int(buffer + 4);

    // printf("DEBUG: Federate receiving message to port %d to federate %d of length %d.\n", port_id, federate_id, length);
}
