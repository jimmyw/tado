/*
 * Home Assistant Mobile App integration client
 *
 * Registers as a mobile_app device, then uses the webhook API to
 * register and update binary sensors. This gives us a proper HA
 * device with grouped entities — no MQTT needed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ha_client.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/client.h>
#include <zephyr/net/socket.h>

LOG_MODULE_REGISTER(ha_client, CONFIG_LOG_DEFAULT_LEVEL);

#define HA_HOST CONFIG_HA_SERVER_IP
#define HA_PORT CONFIG_HA_SERVER_PORT
#define HA_TOKEN CONFIG_HA_LONG_LIVED_TOKEN

#define RECV_BUF_SIZE 512
#define BODY_BUF_SIZE 512
#define WEBHOOK_ID_SIZE 80
#define URL_BUF_SIZE 128

static uint8_t recv_buf[RECV_BUF_SIZE];
static char webhook_id[WEBHOOK_ID_SIZE];
static bool registered;

/* ── HTTP helpers ───────────────────────────────────────────────────── */

static int response_cb(struct http_response *rsp, enum http_final_call final,
                       void *user_data) {
  if (final == HTTP_DATA_FINAL) {
    LOG_DBG("HA response: %d %s", rsp->http_status_code, rsp->http_status);
  }
  return 0;
}

static int ha_connect(void) {
  struct sockaddr_in addr;

  addr.sin_family = AF_INET;
  addr.sin_port = htons(HA_PORT);

  if (zsock_inet_pton(AF_INET, HA_HOST, &addr.sin_addr) != 1) {
    LOG_ERR("Invalid HA server IP: %s", HA_HOST);
    return -EINVAL;
  }

  int sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0) {
    LOG_ERR("Socket create failed: %d", errno);
    return -errno;
  }

  struct zsock_timeval tv = {.tv_sec = 5, .tv_usec = 0};
  zsock_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  zsock_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  int ret = zsock_connect(sock, (struct sockaddr *)&addr, sizeof(addr));
  if (ret < 0) {
    LOG_ERR("Connect to %s:%d failed: %d", HA_HOST, HA_PORT, errno);
    zsock_close(sock);
    return -errno;
  }

  return sock;
}

static int ha_post(const char *url, const char *body, int body_len,
                   bool use_auth) {
  int sock = ha_connect();
  if (sock < 0) {
    return sock;
  }

  static const char *auth_hdr = "Authorization: Bearer " HA_TOKEN "\r\n";
  const char *headers_auth[] = {auth_hdr, NULL};
  const char *headers_none[] = {NULL};

  struct http_request req = {0};
  req.method = HTTP_POST;
  req.url = url;
  req.host = HA_HOST;
  req.protocol = "HTTP/1.1";
  req.payload = body;
  req.payload_len = body_len;
  req.content_type_value = "application/json";
  req.header_fields = use_auth ? headers_auth : headers_none;
  req.response = response_cb;
  req.recv_buf = recv_buf;
  req.recv_buf_len = sizeof(recv_buf);

  int ret = http_client_req(sock, &req, 5000, NULL);
  int status = req.internal.response.http_status_code;
  size_t data_len = req.internal.response.data_len;
  zsock_close(sock);

  /* Let TCP stack reclaim resources before next connection */
  k_msleep(50);

  /* Ensure recv_buf is null-terminated for string operations */
  if (data_len < RECV_BUF_SIZE) {
    recv_buf[data_len] = '\0';
  } else {
    recv_buf[RECV_BUF_SIZE - 1] = '\0';
  }

  if (ret < 0) {
    LOG_ERR("HTTP POST %s failed: %d", url, ret);
    return ret;
  }

  return status;
}

/* ── Parse webhook_id from registration response ───────────────────── */

static int parse_webhook_id(const uint8_t *buf, size_t len) {
  const char *key = "\"webhook_id\"";
  const char *p = strstr((const char *)buf, key);
  if (!p) {
    return -ENOENT;
  }

  /* Find the value: skip key, colon, optional whitespace, opening quote */
  p += strlen(key);
  while (*p == ' ' || *p == ':' || *p == '\t') {
    p++;
  }
  if (*p != '"') {
    return -EINVAL;
  }
  p++; /* skip opening quote */

  const char *end = strchr(p, '"');
  if (!end || (end - p) >= WEBHOOK_ID_SIZE) {
    return -EINVAL;
  }

  memcpy(webhook_id, p, end - p);
  webhook_id[end - p] = '\0';
  return 0;
}

/* ── Device registration ────────────────────────────────────────────── */

static int ha_register_device(void) {
  char body[BODY_BUF_SIZE];

  int body_len = snprintf(body, sizeof(body),
                          "{"
                          "\"device_id\":\"tado_cc1101_receiver\","
                          "\"app_id\":\"tado_cc1101\","
                          "\"app_name\":\"Tado CC1101 Receiver\","
                          "\"app_version\":\"1.0.0\","
                          "\"device_name\":\"Tado RF Gateway\","
                          "\"manufacturer\":\"DIY\","
                          "\"model\":\"BlackPill F411CE + CC1101\","
                          "\"os_name\":\"Zephyr\","
                          "\"os_version\":\"4.4.0\","
                          "\"supports_encryption\":false"
                          "}");

  int status = ha_post("/api/mobile_app/registrations", body, body_len, true);
  if (status < 0) {
    return status;
  }

  if (status != 200 && status != 201) {
    LOG_ERR("Device registration failed: HTTP %d", status);
    return -EIO;
  }

  /* Parse webhook_id from response body */
  LOG_INF("Registration response (%d): %s", status, (char *)recv_buf);
  int ret = parse_webhook_id(recv_buf, sizeof(recv_buf));
  if (ret < 0) {
    LOG_ERR("Failed to parse webhook_id from response");
    LOG_HEXDUMP_DBG(recv_buf, strlen((char *)recv_buf), "response");
    return ret;
  }

  LOG_INF("Registered device, webhook_id: %s", webhook_id);
  return 0;
}

/* ── Sensor registration via webhook ────────────────────────────────── */

static int ha_register_sensor(const char *unique_id, const char *name,
                              const char *device_class) {
  char url[URL_BUF_SIZE];
  char body[BODY_BUF_SIZE];

  snprintf(url, sizeof(url), "/api/webhook/%s", webhook_id);

  int body_len = snprintf(body, sizeof(body),
                          "{"
                          "\"type\":\"register_sensor\","
                          "\"data\":{"
                          "\"unique_id\":\"%s\","
                          "\"name\":\"%s\","
                          "\"type\":\"binary_sensor\","
                          "\"device_class\":\"%s\","
                          "\"state\":false"
                          "}"
                          "}",
                          unique_id, name, device_class);

  int status = ha_post(url, body, body_len, false);
  if (status < 0) {
    return status;
  }

  LOG_INF("Registered sensor: %s -> HTTP %d", unique_id, status);
  return 0;
}

static int ha_register_sensor_analog(const char *unique_id, const char *name,
                                     const char *device_class,
                                     const char *unit) {
  char url[URL_BUF_SIZE];
  char body[BODY_BUF_SIZE];

  snprintf(url, sizeof(url), "/api/webhook/%s", webhook_id);

  int body_len = snprintf(body, sizeof(body),
                          "{"
                          "\"type\":\"register_sensor\","
                          "\"data\":{"
                          "\"unique_id\":\"%s\","
                          "\"name\":\"%s\","
                          "\"type\":\"sensor\","
                          "\"device_class\":\"%s\","
                          "\"unit_of_measurement\":\"%s\","
                          "\"state\":0"
                          "}"
                          "}",
                          unique_id, name, device_class, unit);

  int status = ha_post(url, body, body_len, false);
  if (status < 0) {
    return status;
  }

  LOG_INF("Registered analog sensor: %s -> HTTP %d", unique_id, status);
  return 0;
}

/* ── Public API ─────────────────────────────────────────────────────── */

int ha_init(void) {
  int ret = ha_register_device();
  if (ret < 0) {
    return ret;
  }
  registered = true;
  return 0;
}

int ha_update_sensor(uint32_t dev_id, const char *suffix, bool state, int rssi,
                     uint8_t lqi) {
  if (!registered) {
    return -EAGAIN;
  }

  char unique_id[32];
  char name[48];
  char url[URL_BUF_SIZE];
  char body[BODY_BUF_SIZE];

  snprintf(unique_id, sizeof(unique_id), "tado_%08x_%s", dev_id, suffix);
  snprintf(name, sizeof(name), "Tado %08x %s", dev_id, suffix);
  snprintf(url, sizeof(url), "/api/webhook/%s", webhook_id);

  /* Try to update; if sensor is not registered yet, register first */
  int body_len = snprintf(body, sizeof(body),
                          "{"
                          "\"type\":\"update_sensor_states\","
                          "\"data\":[{"
                          "\"unique_id\":\"%s\","
                          "\"type\":\"binary_sensor\","
                          "\"state\":%s,"
                          "\"attributes\":{"
                          "\"rssi\":%d,"
                          "\"lqi\":%u"
                          "}"
                          "}]"
                          "}",
                          unique_id, state ? "true" : "false", rssi, lqi);

  int status = ha_post(url, body, body_len, false);
  if (status < 0) {
    return status;
  }

  /* If we get an error about unregistered sensor, register and retry */
  if (strstr((char *)recv_buf, "not_registered")) {
    const char *device_class =
        strcmp(suffix, "tamper") == 0 ? "tamper" : "door";
    ha_register_sensor(unique_id, name, device_class);

    /* Retry the update */
    status = ha_post(url, body, body_len, false);
    if (status < 0) {
      return status;
    }
  }

  LOG_INF("Updated %s=%s -> HTTP %d", unique_id, state ? "on" : "off", status);
  return 0;
}

int ha_update_battery(uint32_t dev_id, uint16_t battery_mv, int rssi,
                      uint8_t lqi) {
  if (!registered) {
    return -EAGAIN;
  }

  char unique_id[32];
  char name[48];
  char url[URL_BUF_SIZE];
  char body[BODY_BUF_SIZE];

  snprintf(unique_id, sizeof(unique_id), "tado_%08x_battery", dev_id);
  snprintf(name, sizeof(name), "Tado %08x battery", dev_id);
  snprintf(url, sizeof(url), "/api/webhook/%s", webhook_id);

  /* Convert mV to V with one decimal: e.g. 3200 -> "3.2" */
  int body_len = snprintf(body, sizeof(body),
                          "{"
                          "\"type\":\"update_sensor_states\","
                          "\"data\":[{"
                          "\"unique_id\":\"%s\","
                          "\"type\":\"sensor\","
                          "\"state\":%u.%u,"
                          "\"attributes\":{"
                          "\"rssi\":%d,"
                          "\"lqi\":%u"
                          "}"
                          "}]"
                          "}",
                          unique_id, battery_mv / 1000,
                          (battery_mv % 1000) / 100, rssi, lqi);

  int status = ha_post(url, body, body_len, false);
  if (status < 0) {
    return status;
  }

  if (strstr((char *)recv_buf, "not_registered")) {
    ha_register_sensor_analog(unique_id, name, "voltage", "V");

    status = ha_post(url, body, body_len, false);
    if (status < 0) {
      return status;
    }
  }

  LOG_INF("Updated %s=%u.%uV -> HTTP %d", unique_id, battery_mv / 1000,
          (battery_mv % 1000) / 100, status);
  return 0;
}
