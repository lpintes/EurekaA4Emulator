#ifndef EUREKA_SERIAL_SOCKET_H
#define EUREKA_SERIAL_SOCKET_H

#include <mutex>
#include <optional>

#include "serial_link.h"

// The RS-232 socket itself, plugged into the machine for as long as it runs,
// with at most one cable in it at a time: a TcpLink to another emulator or a
// ComLink to a port of the host (ea4-7zw.3).
//
// It exists so that changing cables never means changing what the machine is
// wired to -- the machine calls its link without a lock, so swapping that
// pointer would have to go through the worker's queue.  Here the swap is under
// a lock of its own, and the window can do it directly, as it already does
// with the TCP cable.
//
// The machine tells its link about RTS and the line format only when they
// change, so a cable plugged in later would never hear them.  The socket
// keeps the last of each and hands them to every cable as it goes in.
class SerialSocket : public SerialLink {
 public:
  // nullptr leaves the socket empty: no CTS, and what is sent is lost.
  void Plug(SerialLink* cable) {
    std::lock_guard guard(lock_);
    cable_ = cable;
    if (cable_ == nullptr) return;
    cable_->SetRequestToSend(rts_);
    if (format_) cable_->SetLineFormat(*format_);
  }

  void SetLineFormat(const LineFormat& format) override {
    std::lock_guard guard(lock_);
    format_ = format;
    if (cable_ != nullptr) cable_->SetLineFormat(format);
  }
  bool ClearToSend() override {
    std::lock_guard guard(lock_);
    return cable_ != nullptr && cable_->ClearToSend();
  }
  void SetRequestToSend(bool asserted) override {
    std::lock_guard guard(lock_);
    rts_ = asserted;
    if (cable_ != nullptr) cable_->SetRequestToSend(asserted);
  }
  void Transmit(uint8_t character) override {
    std::lock_guard guard(lock_);
    if (cable_ != nullptr) cable_->Transmit(character);
  }
  std::optional<uint8_t> Receive() override {
    std::lock_guard guard(lock_);
    return cable_ != nullptr ? cable_->Receive() : std::nullopt;
  }

 private:
  std::mutex lock_;
  SerialLink* cable_ = nullptr;
  bool rts_ = false;
  std::optional<LineFormat> format_;
};

#endif
