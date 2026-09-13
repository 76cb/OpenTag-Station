#pragma once

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#ifdef ARDUINO
#include <lwip/sockets.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

namespace opentag::network {
struct TcpConnection { int socket{-1}; int error{0}; };

// Preserve SO_ERROR before close. Arduino 2.0.17's WiFiClient logs sockerr but
// returns only 0, leaving errno=EINPROGRESS for both refusal and route failure.
// The successful descriptor is handed straight to WiFiClient; no probe socket
// or second connection is opened. Address is already resolved, in network order.
inline TcpConnection connect_tcp_ipv4(std::uint32_t address, std::uint16_t port,
                                    std::int32_t timeout_ms) {
  if (timeout_ms <= 0) return {-1, ETIMEDOUT};
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return {-1, errno};
  const auto fail = [fd](int error) { ::close(fd); return TcpConnection{-1, error}; };
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return fail(errno);
  sockaddr_in remote{};
  remote.sin_family = AF_INET;
  remote.sin_addr.s_addr = address;
  remote.sin_port = htons(port);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) < 0 && errno != EINPROGRESS)
    return fail(errno);
  fd_set writable;
  FD_ZERO(&writable);
  FD_SET(fd, &writable);
  timeval timeout{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
  const int ready = ::select(fd + 1, nullptr, &writable, nullptr, &timeout);
  if (ready < 0) return fail(errno);
  if (!ready) return fail(ETIMEDOUT);
  int error = 0;
  socklen_t size = sizeof(error);
  if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0) return fail(errno);
  if (error) return fail(error);
  timeout = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
  if (::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0 ||
      ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
      ::fcntl(fd, F_SETFL, flags) < 0) return fail(errno);
  return {fd, 0};
}
}  // namespace opentag::network
