#include "remote.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include <bootloader_random.h>
#include <esp_app_desc.h>
#include <esp_app_format.h>
#include <esp_console.h>
#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/logging/LogV.h>

#include <remote/log_ring.h>
#include <remote/token.h>

#include "settings.h"

namespace remote {

static const char *TAG = "remote";

// Time for Wi-Fi to come up after the restart into new firmware.
static constexpr int PROBATION_S = 60;
// Console replies longer than this are cut off.
static constexpr size_t CONSOLE_REPLY_BYTES = 8 * 1024;
// The image starts with its header, one segment header and the app
// description, whose project name tells this firmware from any other.
static constexpr size_t IMAGE_HEAD =
    sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t);

static Token token;
static char firmware[32];  // X-Firmware value
static std::atomic<bool> wifiUp{false};
static std::atomic<bool> serverUp{false};

// The HTTP server runs one request at a time, so all handlers share this
// buffer. Aligned for reading the app description in place.
alignas(4) static char body[4096];

// ---- Log ring

static LogRing<16 * 1024> logRing;
static SemaphoreHandle_t logMutex;
static char logLine[256];  // used under logMutex
static vprintf_like_t forwardLog = vprintf;

// Formats each log call once more into the ring, then hands it on unchanged.
// A call that finds the ring busy (another task logging, or /log reading)
// skips the ring instead of waiting, so logging never blocks.
static int teeLog(const char *fmt, va_list args) {
  if (!xPortInIsrContext() && xSemaphoreTake(logMutex, 0) == pdTRUE) {
    va_list copy;
    va_copy(copy, args);
    const int n = vsnprintf(logLine, sizeof(logLine), fmt, copy);
    va_end(copy);
    if (n > 0) {
      size_t len = (size_t)n;
      if (len >= sizeof(logLine)) {
        len = sizeof(logLine) - 1;
        logLine[len - 1] = '\n';  // a cut-off line still ends its line
      }
      logRing.append(logLine, len);
    }
    xSemaphoreGive(logMutex);
  }
  return forwardLog(fmt, args);
}

// The Matter SDK prints each line's prefix and newline with printf and only
// the message through esp_log, so the ring would get bare messages. This
// redirect writes the whole line, in the same format, with one esp_log call.
static void chipLog(const char *module, uint8_t category, const char *msg, va_list args) {
  using namespace chip::Logging;
  esp_log_level_t level = ESP_LOG_INFO;
  const char *prefix = LOG_COLOR_I "I";
  if (category == kLogCategory_Error) {
    level = ESP_LOG_ERROR;
    prefix = LOG_COLOR_E "E";
  } else if (category == kLogCategory_Detail) {
    level = ESP_LOG_DEBUG;
    prefix = LOG_COLOR_D "D";
  }
  char tag[11];
  snprintf(tag, sizeof(tag), "chip[%s]", module);
  // The message is a format string, so the prefix becomes part of it.
  char fmt[160];
  const int n = snprintf(fmt, sizeof(fmt), "%s (%" PRIu32 ") %s: %s" LOG_RESET_COLOR "\n", prefix,
                         esp_log_timestamp(), tag, msg);
  if (n < 0 || (size_t)n >= sizeof(fmt)) {
    Platform::LogV(module, category, msg, args);
    return;
  }
  // The message layer's per-message traces are three quarters of all log
  // text, enough to flush the ring within a second at boot. They go to USB
  // only; its errors still reach the ring.
  if (category != kLogCategory_Error && strcmp(module, "EM") == 0) {
    if (esp_log_level_get(tag) >= level) vprintf(fmt, args);
    return;
  }
  esp_log_writev(level, tag, fmt, args);
}

void captureLog() {
  logMutex = xSemaphoreCreateMutex();
  if (!logMutex) return;
  forwardLog = esp_log_set_vprintf(teeLog);
  chip::Logging::SetLogRedirectCallback(chipLog);
}

// ---- HTTP helpers

static esp_err_t reply(httpd_req_t *req, const char *status, const char *text) {
  httpd_resp_set_status(req, status);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_sendstr(req, text);
}

// Answers 401 unless the request carries the token.
static bool admit(httpd_req_t *req) {
  char header[64];
  if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) == ESP_OK &&
      authorized(header, token.data())) {
    httpd_resp_set_hdr(req, "X-Firmware", firmware);
    return true;
  }
  reply(req, "401 Unauthorized", "missing or wrong token\n");
  return false;
}

// Receives exactly `len` bytes; false if the client disconnects or stalls
// for three receive timeouts in a row.
static bool receive(httpd_req_t *req, char *buf, size_t len) {
  int timeouts = 0;
  while (len > 0) {
    const int n = httpd_req_recv(req, buf, len);
    if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 3) continue;
    if (n <= 0) return false;
    timeouts = 0;
    buf += n;
    len -= (size_t)n;
  }
  return true;
}

// ---- POST /ota

static esp_err_t handleOta(httpd_req_t *req) {
  if (!admit(req)) return ESP_OK;
  const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
  if (!target) return reply(req, "500 Internal Server Error", "no update partition\n");
  if (req->content_len < IMAGE_HEAD || req->content_len > target->size) {
    return reply(req, "400 Bad Request", "body must be the app image, build/matter-shutter-bridge.bin\n");
  }

  if (!receive(req, body, IMAGE_HEAD)) return ESP_FAIL;
  const auto *desc =
      reinterpret_cast<const esp_app_desc_t *>(body + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t));
  if (desc->magic_word != ESP_APP_DESC_MAGIC_WORD ||
      strncmp(desc->project_name, esp_app_get_description()->project_name, sizeof(desc->project_name)) != 0) {
    return reply(req, "400 Bad Request", "not an image of this firmware\n");
  }

  // Sequential writes erase each sector just before writing it, instead of
  // erasing the whole partition up front.
  esp_ota_handle_t ota;
  if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &ota) != ESP_OK) {
    return reply(req, "500 Internal Server Error", "cannot start the update\n");
  }
  ESP_LOGI(TAG, "receiving %u bytes into %s", (unsigned)req->content_len, target->label);
  esp_err_t err = esp_ota_write(ota, body, IMAGE_HEAD);
  for (size_t done = IMAGE_HEAD; err == ESP_OK && done < req->content_len;) {
    const size_t n = req->content_len - done < sizeof(body) ? req->content_len - done : sizeof(body);
    if (!receive(req, body, n)) {
      esp_ota_abort(ota);
      ESP_LOGW(TAG, "update aborted: upload interrupted");
      return ESP_FAIL;
    }
    err = esp_ota_write(ota, body, n);
    done += n;
  }
  // esp_ota_end() checks the whole image and frees the handle either way.
  if (err == ESP_OK) {
    err = esp_ota_end(ota);
  } else {
    esp_ota_abort(ota);
  }
  if (err == ESP_OK) err = esp_ota_set_boot_partition(target);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "update failed: %s", esp_err_to_name(err));
    return reply(req, "400 Bad Request", "update failed; the running firmware stays\n");
  }

  ESP_LOGW(TAG, "update written to %s; restarting", target->label);
  snprintf(body, sizeof(body), "installed in %s, restarting\n", target->label);
  reply(req, "200 OK", body);
  vTaskDelay(pdMS_TO_TICKS(500));  // lets the reply reach the client
  esp_restart();
}

// ---- POST /console

// USB console lines run on the Matter task too (see the CHIP shell's
// MainLoopESP32.cpp), so commands never run concurrently and stdout is
// redirected for one command at a time.
static struct {
  char *out;
  size_t len;
  SemaphoreHandle_t done;
} job;

// Runs the line in `body` with stdout captured into job.out.
static void runConsoleLine(intptr_t) {
  FILE *capture = fmemopen(job.out, CONSOLE_REPLY_BYTES, "w");
  if (capture) {
    FILE *usb = stdout;
    fflush(usb);
    stdout = capture;
    int ret = 0;
    const esp_err_t err = esp_console_run(body, &ret);
    if (err == ESP_ERR_NOT_FOUND) {
      printf("unknown command\n");
    } else if (err != ESP_OK || ret != 0) {
      printf("Error: %d\n", err != ESP_OK ? (int)err : ret);
    }
    fflush(capture);
    const long n = ftell(capture);
    stdout = usb;
    fclose(capture);
    job.len = n > 0 ? (size_t)n : 0;
  }
  xSemaphoreGive(job.done);
}

static esp_err_t handleConsole(httpd_req_t *req) {
  if (!admit(req)) return ESP_OK;
  if (req->content_len == 0 || req->content_len > CONFIG_CHIP_SHELL_CMD_LINE_BUF_MAX_LENGTH) {
    return reply(req, "400 Bad Request", "body must be one console line\n");
  }
  if (!receive(req, body, req->content_len)) return ESP_FAIL;
  body[req->content_len] = '\0';
  body[strcspn(body, "\r\n")] = '\0';

  job.out = static_cast<char *>(malloc(CONSOLE_REPLY_BYTES));
  if (!job.out) return reply(req, "503 Service Unavailable", "out of memory\n");
  job.len = 0;
  ESP_LOGI(TAG, "console: %s", body);
  esp_err_t err = ESP_FAIL;
  if (chip::DeviceLayer::PlatformMgr().ScheduleWork(runConsoleLine) == CHIP_NO_ERROR) {
    xSemaphoreTake(job.done, portMAX_DELAY);
    httpd_resp_set_type(req, "text/plain");
    err = httpd_resp_send(req, job.out, (ssize_t)job.len);
  } else {
    err = reply(req, "503 Service Unavailable", "Matter task busy\n");
  }
  free(job.out);
  return err;
}

// ---- GET /log

static esp_err_t handleLog(httpd_req_t *req) {
  if (!admit(req)) return ESP_OK;
  char query[48];
  char value[24];
  uint64_t from = 0;
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK &&
      httpd_query_key_value(query, "since", value, sizeof(value)) == ESP_OK) {
    from = strtoull(value, nullptr, 10);
  }

  xSemaphoreTake(logMutex, portMAX_DELAY);
  const uint64_t end = logRing.end();
  // An offset beyond the end comes from before a restart.
  if (from < logRing.begin() || from > end) from = logRing.begin();
  xSemaphoreGive(logMutex);

  char start[24];
  char next[24];
  snprintf(start, sizeof(start), "%" PRIu64, from);
  snprintf(next, sizeof(next), "%" PRIu64, end);
  httpd_resp_set_type(req, "text/plain");
  httpd_resp_set_hdr(req, "X-Log-Start", start);
  httpd_resp_set_hdr(req, "X-Log-Next", next);

  while (from < end) {
    xSemaphoreTake(logMutex, portMAX_DELAY);
    if (from < logRing.begin()) from = logRing.begin();
    const uint64_t left = from < end ? end - from : 0;
    const size_t n = logRing.read(from, body, left < sizeof(body) ? (size_t)left : sizeof(body));
    xSemaphoreGive(logMutex);
    if (n == 0) break;
    from += n;
    if (httpd_resp_send_chunk(req, body, (ssize_t)n) != ESP_OK) return ESP_FAIL;
  }
  return httpd_resp_send_chunk(req, nullptr, 0);
}

// ---- Probation of new firmware

static void probation(void *) {
  for (int s = 0; s < PROBATION_S; s++) {
    if (wifiUp && serverUp) {
      esp_ota_mark_app_valid_cancel_rollback();
      ESP_LOGI(TAG, "new firmware confirmed");
      vTaskDelete(nullptr);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  ESP_LOGE(TAG, "no Wi-Fi or no server %d s after the update; returning to the previous firmware", PROBATION_S);
  esp_ota_mark_app_invalid_rollback_and_reboot();
  vTaskDelete(nullptr);
}

static void startProbation() {
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
      state != ESP_OTA_IMG_PENDING_VERIFY) {
    return;
  }
  ESP_LOGW(TAG, "new firmware on probation for %d s", PROBATION_S);
  if (xTaskCreate(probation, "ota_probation", 3072, nullptr, 1, nullptr) != pdPASS) {
    // Without the task the image is never confirmed; the next restart
    // returns to the previous firmware.
    ESP_LOGE(TAG, "cannot start the probation task");
  }
}

// ---- Public functions

void init() {
  if (settings::loadRemoteToken(token)) return;
  // Wi-Fi and Bluetooth are not running yet, so the hardware RNG needs this
  // entropy source to produce true random numbers.
  bootloader_random_enable();
  token = makeToken([] { return esp_random(); });
  bootloader_random_disable();
  settings::saveRemoteToken(token);
  ESP_LOGI(TAG, "generated this device's remote access token");
}

esp_err_t start() {
  char sha[9];
  esp_app_get_elf_sha256(sha, sizeof(sha));
  const esp_partition_t *running = esp_ota_get_running_partition();
  snprintf(firmware, sizeof(firmware), "%s %s", sha, running ? running->label : "?");

  job.done = xSemaphoreCreateBinary();
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.stack_size = 6144;
  cfg.max_open_sockets = 3;
  cfg.lru_purge_enable = true;
  httpd_handle_t server = nullptr;
  esp_err_t err = job.done && logMutex ? httpd_start(&server, &cfg) : ESP_ERR_NO_MEM;
  if (err == ESP_OK) {
    static const httpd_uri_t handlers[] = {
        {.uri = "/ota", .method = HTTP_POST, .handler = handleOta, .user_ctx = nullptr},
        {.uri = "/console", .method = HTTP_POST, .handler = handleConsole, .user_ctx = nullptr},
        {.uri = "/log", .method = HTTP_GET, .handler = handleLog, .user_ctx = nullptr},
    };
    for (const httpd_uri_t &h : handlers) httpd_register_uri_handler(server, &h);
    serverUp = true;
  }
  startProbation();
  return err;
}

void setWifiConnected(bool connected) { wifiUp = connected; }

void print() {
  esp_netif_ip_info_t ip = {};
  esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (sta && esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr != 0) {
    printf("Remote:       http://%s (" IPSTR ")\n", CONFIG_LWIP_LOCAL_HOSTNAME, IP2STR(&ip.ip));
  } else {
    printf("Remote:       http://%s (no Wi-Fi yet)\n", CONFIG_LWIP_LOCAL_HOSTNAME);
  }
  printf("Token:        %s\n", token.data());
  printf("Firmware:     %s\n", firmware);
}

}  // namespace remote
