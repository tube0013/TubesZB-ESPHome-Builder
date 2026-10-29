#pragma once
#include "ezsp_bootloader.h"
#include "esphome/components/efr32_info/ash_util.h"

namespace esphome::efr32_flasher {
class EZSPBootloaderUART {
 public:
  explicit EZSPBootloaderUART(uart::UARTComponent *uart) : state_(uart) {}
  bool reset() {
    state_.reset_counters(); expected_rx_ = 0; rx_.clear(); escaped_ = discarded_ = false;
    efr32::ash::send_reset(state_.uart);
    const auto start = millis();
    while (millis() - start < 2500) {
      efr32::ash::Frame frame;
      if (!recv_frame_(frame, 200)) continue;
      if (frame.type == efr32::ash::FrameType::ERROR) return false;
      if (frame.control == 0xC1 && frame.payload.size() == 2 && frame.payload[0] == 2) {
        state_.reset_counters();
        efr32::ash::send_ack(state_);
        return true;
      }
    }
    return false;
  }
  bool command(bool extended, uint8_t seq, uint16_t id, const std::vector<uint8_t> &payload,
               std::vector<uint8_t> &reply) {
    const auto data = ezsp_bootloader::request(extended, seq, id, payload);
    const uint8_t sent_seq = state_.tx_seq;
    efr32::ash::send_data_frame(state_, data, expected_rx_);
    const auto start = millis();
    uint8_t retries = 0;
    while (millis() - start < 3000) {
      efr32::ash::Frame frame;
      if (!recv_frame_(frame, 200)) continue;
      if (frame.type == efr32::ash::FrameType::ERROR || frame.type == efr32::ash::FrameType::RESET) return false;
      if (frame.type == efr32::ash::FrameType::NAK) {
        if ((frame.control & 7) != sent_seq || retries++ >= 2) return false;
        const auto next = state_.tx_seq;
        state_.tx_seq = sent_seq;
        efr32::ash::send_data_frame(state_, data, expected_rx_, true);
        state_.tx_seq = next;
        continue;
      }
      if (frame.type != efr32::ash::FrameType::DATA) continue;
      const auto received = uint8_t((frame.control >> 4) & 7);
      if (received != expected_rx_) {
        if (received == uint8_t((expected_rx_ + 7) & 7)) efr32::ash::send_ack(state_, false, expected_rx_);
        else efr32::ash::send_nak(state_, expected_rx_);
        continue;
      }
      expected_rx_ = uint8_t((expected_rx_ + 1) & 7);
      efr32::ash::send_ack(state_, false, expected_rx_);
      if (ezsp_bootloader::response(frame.payload, extended, seq, id, reply)) return true;
      std::vector<uint8_t> ignored;
      if (ezsp_bootloader::response(frame.payload, extended, seq, 0x0058, ignored)) return false;
    }
    return false;
  }
 private:
  // Software flow-control bytes are out of band unless escaped. Keep partial
  // frames across receive slices, bound memory, and verify CRC before any reply
  // can authorize bootloader entry. A slice has an absolute timeout even in noise.
  bool recv_frame_(efr32::ash::Frame &frame, uint32_t timeout_ms) {
    const auto start = millis();
    while (millis() - start < timeout_ms) {
      App.feed_wdt();
      uint8_t byte;
      if (!state_.uart->available() || !state_.uart->read_byte(&byte)) { esphome::delay(1); continue; }
      if (byte == efr32::ash::XON || byte == efr32::ash::XOFF) continue;
      if (escaped_) { byte ^= 0x20; escaped_ = false; }
      else {
        if (byte == efr32::ash::CAN) { rx_.clear(); discarded_ = escaped_ = false; continue; }
        if (byte == efr32::ash::SUB) { rx_.clear(); discarded_ = true; continue; }
        if (byte == efr32::ash::FLAG) {
          if (!discarded_ && rx_.size() >= 3) {
            const auto size = rx_.size() - 2;
            const auto crc = uint16_t(rx_[size] << 8) | rx_[size + 1];
            if (efr32::ash::crc_hqx_be(rx_.data(), size) == crc) {
              frame.control = rx_[0]; frame.type = efr32::ash::classify_control(frame.control);
              frame.payload.assign(rx_.begin() + 1, rx_.begin() + size);
              if (frame.type == efr32::ash::FrameType::DATA) efr32::ash::randomize(frame.payload);
              rx_.clear(); return true;
            }
          }
          rx_.clear(); discarded_ = escaped_ = false; continue;
        }
        if (byte == efr32::ash::ESC) { escaped_ = true; continue; }
      }
      if (discarded_) continue;
      if (rx_.size() >= 256) { rx_.clear(); discarded_ = true; continue; }
      rx_.push_back(byte);
    }
    return false;
  }
  std::vector<uint8_t> rx_;
  bool escaped_{false}, discarded_{false};
  efr32::ash::LinkState state_;
  uint8_t expected_rx_{0};
};
}  // namespace esphome::efr32_flasher
