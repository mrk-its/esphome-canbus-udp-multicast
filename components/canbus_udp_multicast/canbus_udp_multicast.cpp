#include "esphome.h"
#include "canbus_udp_multicast.h"

static bool file_reader(cmp_ctx_t *ctx, void *data, size_t limit) {
  memcpy(data, ctx->buf, limit);
  ctx->buf = ((char *) ctx->buf) + limit;
  return true;
}

static bool file_skipper(cmp_ctx_t *ctx, size_t count) {
  ctx->buf = ((char *) ctx->buf) + count;
  return false;
}

static size_t file_writer(cmp_ctx_t *ctx, const void *data, size_t count) {
  memcpy(ctx->buf, data, count);
  ctx->buf = ((char *) ctx->buf) + count;
  return count;
}

namespace esphome {
namespace canbus_udp_multicast {

int CanbusUdpMulticast::esp_join_multicast_group(int sockfd) {
  struct ip_mreq imreq;
  struct in_addr iaddr;
  memset(&imreq, 0, sizeof(imreq));
  memset(&iaddr, 0, sizeof(iaddr));
  int err = 0;

  // Configure sending interface of multicast group
  esp_netif_ip_info_t ip_info;
  err = esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey(this->if_key), &ip_info);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to get IP address info. Error 0x%x", err);
    goto err;
  }
  inet_addr_from_ip4addr(&iaddr, &ip_info.ip);
  err = setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_IF, &iaddr, sizeof(struct in_addr));
  if (err < 0) {
    ESP_LOGE(TAG, "Failed to set IP_MULTICAST_IF. Error %d", errno);
    goto err;
  }

  // Configure the address of monitoring multicast group
  inet_aton(multicast_ip, &imreq.imr_multiaddr.s_addr);

  // Configure the socket to join the multicast group
  err = setsockopt(sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &imreq, sizeof(struct ip_mreq));
  if (err < 0) {
    ESP_LOGE(TAG, "Failed to set IP_ADD_MEMBERSHIP. Error %d", errno);
  }
err:
  return err;
}

bool CanbusUdpMulticast::decode_can_frame(uint8_t *buffer, size_t len, struct canbus::CanFrame *frame) {
  cmp_ctx_t cmp;
  cmp_init(&cmp, buffer, file_reader, file_skipper, 0);

  frame->can_id = 0xffffffff;
  frame->can_data_length_code = 0xff;
  frame->use_extended_id = false;
  uint32_t data_size = 0xffffffff;

  uint32_t map_size, n;
  if (cmp_read_map(&cmp, &map_size)) {
    int32_t num;

    ESP_LOGV(TAG, "map_size: %ld", map_size);
    char key[33];
    for (n = 0; n < map_size; n++) {
      uint32_t size = 32;
      if (cmp_read_str(&cmp, key, &size)) {
        ESP_LOGV(TAG, "key: %s", key);
        if (!strncmp(key, "arbi", 4)) {
          if (cmp_read_int(&cmp, (int32_t *) &frame->can_id)) {
            ESP_LOGV(TAG, "  %s: %ld", key, frame->can_id);
          } else
            break;
        } else if (!strncmp(key, "data", 4)) {
          uint32_t size = 64;
          if (cmp_read_bin(&cmp, frame->data, &size)) {
            data_size = size;
            ESP_LOGV(TAG, "  data size: %ld", size);
          } else
            break;
        } else if (!strcmp(key, "is_extended_id")) {
          if (cmp_read_bool(&cmp, &frame->use_extended_id)) {
            ESP_LOGV(TAG, "  %s: %d", key, frame->use_extended_id);
          } else
            break;
        } else if (!strcmp(key, "dlc")) {
          if (cmp_read_int(&cmp, &num)) {
            frame->can_data_length_code = num;
            ESP_LOGV(TAG, "  %s: %d", key, frame->can_data_length_code);
          } else
            break;
        } else {
          bool skipped = cmp_skip_object(&cmp, 0);
          ESP_LOGV(TAG, "  skipped: %d", skipped);
          if (!skipped)
            break;
        }
      } else
        break;
    }
    if (n == map_size && frame->can_id < 2048 && data_size <= 64 && frame->can_data_length_code < 0xff) {
      return true;
    }
  }
  ESP_LOGE(TAG, "messge decode error");
  return false;
}

void CanbusUdpMulticast::setup() {
  struct sockaddr_in saddr;
  memset(&dest_addr, 0, sizeof(dest_addr));
  memset(&saddr, 0, sizeof(saddr));

  ESP_LOGCONFIG(TAG, "Setting up CanbusUdpMulticast");
  if (!this->setup_internal()) {
    ESP_LOGE(TAG, "setup error!");
    this->mark_failed();
  }

  sockfd = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sockfd == -1) {
    ESP_LOGE(TAG, "Create UDP socket fail");
    return;
  }

  struct timeval rcvtimeout;
  rcvtimeout.tv_sec = 0;
  rcvtimeout.tv_usec = 100000;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &rcvtimeout, sizeof(rcvtimeout)) < 0) {
    ESP_LOGE(TAG, "Failed to set SO_RCVTIMEO. Error %d", errno);
    close(sockfd);
    return;
  }

  uint32_t bufsize = 16384;
  if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) < 0) {
    ESP_LOGE(TAG, "Failed to set SO_RCVBUF. Error %d", errno);
    close(sockfd);
    return;
  }

  // Bind socket
  saddr.sin_family = PF_INET;
  saddr.sin_port = htons(multicast_port);
  saddr.sin_addr.s_addr = htonl(INADDR_ANY);
  int ret = bind(sockfd, (struct sockaddr *) &saddr, sizeof(struct sockaddr_in));
  if (ret < 0) {
    ESP_LOGE(TAG, "Failed to bind socket. Error %d", errno);
    close(sockfd);
    return;
  }
  // Set multicast TTL to 1, limiting the multicast packet to one route
  uint8_t ttl = 1;
  ret = setsockopt(sockfd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(uint8_t));
  if (ret < 0) {
    ESP_LOGE(TAG, "Failed to set IP_MULTICAST_TTL. Error %d", errno);
    close(sockfd);
    return;
  }

  // Join the multicast group
  ret = esp_join_multicast_group(sockfd);
  if (ret < 0) {
    ESP_LOGE(TAG, "Failed to join multicast group");
    close(sockfd);
    return;
  }

  // Set multicast destination address and port
  dest_addr = {
      .sin_len = 0,
      .sin_family = AF_INET,
      .sin_port = htons(multicast_port),
      .sin_addr = {0},
      .sin_zero = {0},
  };
  inet_aton(multicast_ip, &dest_addr.sin_addr.s_addr);

  this->is_initialized = true;
  ESP_LOGCONFIG(TAG, "setup done");
}

float CanbusUdpMulticast::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

void CanbusUdpMulticast::loop() {
  if (!is_initialized)
    return;

  uint8_t udp_recv_buf[256];
  ESP_LOGV(TAG, "calling recvfrom");
  int size = recvfrom(sockfd, udp_recv_buf, 256, MSG_DONTWAIT, 0, 0);
  if (size >= 0) {
    ESP_LOGV(TAG, "received message, len: %d", size);

    canbus::CanFrame frame;
    if (decode_can_frame(udp_recv_buf, 256, &frame)) {
      ESP_LOGD(TAG, "decoded frame: can_id: %03x, dlc: %d", frame.can_id, frame.can_data_length_code);
      send_message_no_loopback(&frame);
    }

  } else {
    if (errno != 11) {
      ESP_LOGE(TAG, "err: %d", errno);
    }
  }
}

void CanbusUdpMulticast::trigger(uint32_t can_id, bool use_extended_id, bool remote_transmission_request,
                                 const std::vector<uint8_t> &data) {
  // fire all triggers
  // TODO: currently we can't check can_id, can_mask, remote_transmission_request because these trigger fields
  // are protected
  for (auto *trigger : this->triggers_) {
    trigger->trigger(data, can_id, remote_transmission_request);
  }
}

bool CanbusUdpMulticast::setup_internal() {
  if (!this->canbus) {
    return true;
  }
  Automation<std::vector<uint8_t>, uint32_t, bool> *automation;
  LambdaAction<std::vector<uint8_t>, uint32_t, bool> *lambdaaction;
  canbus::CanbusTrigger *canbus_canbustrigger;

  canbus_canbustrigger = new canbus::CanbusTrigger(this->canbus, 0, 0, false);
  canbus_canbustrigger->set_component_source("canbus_udp_multicast");
  App.register_component(canbus_canbustrigger);
  automation = new Automation<std::vector<uint8_t>, uint32_t, bool>(canbus_canbustrigger);
  auto cb = [this](std::vector<uint8_t> x, uint32_t can_id, bool remote_transmission_request) -> void {
    trigger(can_id, this->use_extended_id_, remote_transmission_request, x);
    this->send_udp_multicast(can_id, this->use_extended_id_, remote_transmission_request, x);
  };
  lambdaaction = new LambdaAction<std::vector<uint8_t>, uint32_t, bool>(cb);
  automation->add_actions({lambdaaction});
  ESP_LOGI(TAG, "trigger installed!");

  return true;
}

void CanbusUdpMulticast::send_udp_multicast(uint32_t can_id, bool use_extended_id, bool remote_transmission_request,
                                            const std::vector<uint8_t> &data) {
  if (!is_initialized)
    return;

  char message[256];

  cmp_ctx_t cmp;
  cmp_init(&cmp, message, 0, 0, file_writer);

  cmp_write_map(&cmp, 4);
  cmp_write_str(&cmp, "arbitration_id", 14);
  cmp_write_u16(&cmp, can_id);

  cmp_write_str(&cmp, "data", 4);
  cmp_write_bin(&cmp, data.data(), data.size());

  cmp_write_str(&cmp, "is_extended_id", 14);
  cmp_write_bool(&cmp, use_extended_id);

  cmp_write_str(&cmp, "is_remote_frame", 15);
  cmp_write_bool(&cmp, remote_transmission_request);

  int msg_len = ((char *) cmp.buf) - message;

  // Call sendto() to send multicast data
  int ret = ::sendto(sockfd, message, msg_len, 0, (struct sockaddr *) &dest_addr, sizeof(struct sockaddr));
  if (ret < 0) {
    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
  } else {
    ESP_LOGD(TAG, "sent, can_id: %03x", can_id);
  }
}

canbus::Error CanbusUdpMulticast::send_message_no_loopback(struct canbus::CanFrame *frame) {
  std::vector<uint8_t> data = std::vector<uint8_t>(frame->data, frame->data + frame->can_data_length_code);
  if (this->canbus) {
    this->canbus->send_data(frame->can_id, frame->use_extended_id, frame->remote_transmission_request, data);
  }
  trigger(frame->can_id, frame->use_extended_id, frame->remote_transmission_request, data);
  return canbus::ERROR_OK;
}

canbus::Error CanbusUdpMulticast::send_message(struct canbus::CanFrame *frame) {
  std::vector<uint8_t> data = std::vector<uint8_t>(frame->data, frame->data + frame->can_data_length_code);
  if (this->canbus) {
    this->canbus->send_data(frame->can_id, frame->use_extended_id, frame->remote_transmission_request, data);
  }
  send_udp_multicast(frame->can_id, frame->use_extended_id, frame->remote_transmission_request, data);
  return canbus::ERROR_OK;
};

canbus::Error CanbusUdpMulticast::read_message(struct canbus::CanFrame *frame) { return canbus::ERROR_NOMSG; };

}  // namespace canbus_udp_multicast
}  // namespace esphome
