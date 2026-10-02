#ifndef EUREKA_COM_LINK_H
#define EUREKA_COM_LINK_H

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "serial_link.h"

// The RS-232 socket wired to a serial port of the host (ea4-7zw.3): a USB
// adapter to a printer or a real Eureka, or one end of a com0com pair.
//
// The firmware does its own flow control -- it waits for CTS before sending
// (184F2) and drops RTS when its buffer fills (1860D) -- so the driver does
// none: our RTS goes out on the RTS wire as the machine sets it, and the CTS
// wire comes back as ClearToSend.  The start, parity and stop bits are the
// host UART's, made from the LineFormat the machine reports.
//
// The machine calls the SerialLink half from its own thread; the port is
// touched only by a thread of this class, and the two meet in two queues under
// one lock -- the same split as TcpLink.
class ComLink : public SerialLink {
 public:
  enum class Event {
    // The port stopped answering, typically a USB adapter pulled out.  Not
    // reported for Close(), which the caller knows.
    kLost,
    // The driver refused the line format; the detail says which.  The port
    // stays open, but the far end will not understand what it gets.
    kFormatRefused,
  };
  // Called on the link's own thread.  It must not call back into the link --
  // Close() joins that very thread.
  using Listener = std::function<void(Event, const std::wstring& detail)>;

  ComLink();
  ~ComLink() override;
  ComLink(const ComLink&) = delete;
  ComLink& operator=(const ComLink&) = delete;

  // Set before Open.
  void SetListener(Listener listener) { listener_ = std::move(listener); }

  // Opens "COM8" and the like.  Done here and now, so a port that does not
  // exist or that another program holds is refused at once.
  bool Open(const std::wstring& name, std::wstring& error);
  // Lets go of the port.  Safe from any thread but the listener's.
  void Close();

  bool is_open() const { return open_.load(); }

  // SerialLink.  With no port open, CTS is not asserted and what is
  // transmitted is lost -- the same as an unplugged socket.  The format and
  // RTS are remembered and applied when a port opens.
  void SetLineFormat(const LineFormat& format) override;
  bool ClearToSend() override;
  void SetRequestToSend(bool asserted) override;
  void Transmit(uint8_t character) override;
  std::optional<uint8_t> Receive() override;

 private:
  void Serve();
  // On the worker; false when the driver refused the format.
  bool ApplyFormat(const LineFormat& format, std::wstring& detail);
  void Notify(Event event, const std::wstring& detail = {});

  Listener listener_;
  std::thread worker_;
  // HANDLEs; void* so that this header does not drag in windows.h.  The port
  // belongs to the worker while it runs.
  void* port_ = nullptr;
  void* stop_ = nullptr;
  void* wake_ = nullptr;

  std::atomic<bool> open_{false};
  std::atomic<bool> cts_{false};

  std::mutex lock_;
  std::deque<uint8_t> inbox_;
  std::string outbox_;
  LineFormat format_{9600, 8, LineFormat::Parity::kNone, 1};
  bool formatChanged_ = false;
  bool rts_ = false;
  bool rtsChanged_ = false;
};

// The serial ports Windows knows of right now, "COM3" before "COM10".
std::vector<std::wstring> ListComPorts();

#endif
