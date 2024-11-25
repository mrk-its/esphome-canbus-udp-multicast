#pragma once
#include "cmp.h"

#include "esphome.h"
using namespace esphome;

#include "esphome/core/component.h"
#include "esphome/core/defines.h"

namespace esphome {
namespace canbus_udp_multicast {
const char *const TAG = "canbus_udp_multicast";

class CanbusUdpMulticast : public canbus::Canbus {
  char *multicast_ip;
  int multicast_port;
  bool is_initialized = false;

  struct sockaddr_in dest_addr;
  int sockfd = 0;

 public:
  canbus::Canbus *canbus;

  CanbusUdpMulticast(canbus::Canbus *canbus, const char *multicast_ip, int multicast_port) {
    this->canbus = canbus;
    this->multicast_ip = (char *) multicast_ip;
    this->multicast_port = multicast_port;
  };

  void send_udp_multicast(uint32_t can_id, bool use_extended_id, bool remote_transmission_request,
                          const std::vector<uint8_t> &data);

  float get_setup_priority() const override;
  void setup();
  void loop() override;

 protected:
  bool setup_internal() override;
  canbus::Error send_message_no_loopback(struct canbus::CanFrame *frame);
  canbus::Error send_message(struct canbus::CanFrame *frame) override;
  canbus::Error read_message(struct canbus::CanFrame *frame) override;

 private:
  void trigger(uint32_t can_id, bool use_extended_id, bool remote_transmission_request,
               const std::vector<uint8_t> &data);
  bool decode_can_frame(uint8_t *buffer, size_t len, struct canbus::CanFrame *frame);
  int esp_join_multicast_group(int sockfd);
};
}  // namespace canbus_udp_multicast
}  // namespace esphome
