// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "rsp.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "rsp_packet.h"

namespace rsp {

namespace {

// Timeout = 1 seconds.
constexpr int kReceiveTimeoutMicroSec = 0;
constexpr int kReceiveTimeoutSec = 1;
// No specification found about max length,
// Using 4000 as nRF52840-dk supported packet size.
constexpr int kReceiveBufferLength = 4000;

// Returns the data wrapped in the given packet.
// Format: $ data # 2-bytes checksum
std::string GetPacketdata(std::string_view packet) {
  if (packet.size() < 4) {
    return "";
  }
  return std::string(packet.substr(1, packet.size() - 4));
}

}  // namespace

RemoteSerialProtocol::RemoteSerialProtocol()
    : recv_buffer_(kReceiveBufferLength) {}

bool RemoteSerialProtocol::Initialize() {
  return ((socket_ = socket(AF_INET, SOCK_STREAM, 0)) != -1);
}

bool RemoteSerialProtocol::Connect(int port) {
  if (socket_ < 0) {
    return false;
  }
  struct sockaddr_in server_address;
  server_address.sin_family = AF_INET;
  server_address.sin_port = htons(port);
  if (inet_pton(AF_INET, "127.0.0.1", &server_address.sin_addr) <= 0) {
    return false;
  }
  return (connect(socket_, (struct sockaddr*)&server_address,
                  sizeof(server_address)) != -1);
}

bool RemoteSerialProtocol::Terminate() { return (close(socket_) != -1); }

bool RemoteSerialProtocol::SendPacket(RspPacket packet, int retries /* = 1 */) {
  int num_retries = 0;
  char const* buf = packet.ToString().c_str();
  while (num_retries < retries) {
    int aux = send(socket_, buf, strlen(buf), 0);
    if (aux != -1 && ReadAcknowledgement()) {
      return true;
    }
    ++num_retries;
  }
  return false;
}

// The possible reply packets are listed in:
// https://sourceware.org/gdb/current/onlinedocs/gdb/Stop-Reply-Packets.html#Stop-Reply-Packets
std::tuple<bool, std::string> RemoteSerialProtocol::ReceivePacket() {
  auto [ok, response] = Receive(kReceiveBufferLength);
  return {ok, GetPacketdata(response)};
}

std::tuple<bool, std::string> RemoteSerialProtocol::SendRecvPacket(
    RspPacket packet, int retries /* = 1 */) {
  if (!SendPacket(packet, retries)) {
    return {false, ""};
  }
  return ReceivePacket();
}

std::tuple<bool, std::string> RemoteSerialProtocol::Receive(
    int receive_length) {
  fd_set file_set;
  FD_ZERO(&file_set);
  FD_SET(socket_, &file_set);
  struct timeval tv {
    kReceiveTimeoutSec, kReceiveTimeoutMicroSec
  };
  int return_value = select(socket_ + 1, &file_set, NULL, NULL, &tv);
  if (return_value <= 0) {
    return {false, ""};
  }
  int real_len = recv(socket_, recv_buffer_.data(), receive_length, 0);
  if (real_len == -1) {
    return {false, ""};
  }
  return {true,
          std::string(recv_buffer_.begin(), recv_buffer_.begin() + real_len)};
}

// Acknowledgement is either '+' or '-'
bool RemoteSerialProtocol::ReadAcknowledgement() {
  auto [ok, response] = Receive(1);
  return ok && (response == "+");
}

}  // namespace rsp