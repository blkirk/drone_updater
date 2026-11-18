#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace config {

static Config s_current;

Config& current() { return s_current; }

void apply_defaults(Config* c) {
  memset(c, 0, sizeof(*c));
  c->ble_name[0]     = '\0';
  c->prn             = 8;
  c->high_mtu        = true;
  c->retries         = 3;
  c->retry_cooldown  = 5;
  c->wedge_cooldown  = 60;
  c->min_rssi        = -90;
  c->tx_power        = 9;
  c->scan_timeout    = 0;
  c->scan_debug      = false;
  c->autostart_delay = 240;
  c->wifi_ssid[0]    = '\0';
  snprintf(c->wifi_pass, sizeof(c->wifi_pass), "%s", "dronedfu");
}

static void trim(char* s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
    s[--n] = '\0';
  }
  size_t lead = 0;
  while (s[lead] == ' ' || s[lead] == '\t') lead++;
  if (lead) memmove(s, s + lead, n - lead + 1);
}

static bool parse_bool(const char* v) {
  return v[0] == '1' || v[0] == 't' || v[0] == 'T' || v[0] == 'y' || v[0] == 'Y';
}

// Strict integer parse: whole string must be a (signed) decimal number.
static bool parse_int(const char* v, long lo, long hi, long* out) {
  if (!*v) return false;
  char* end = nullptr;
  long n = strtol(v, &end, 10);
  if (*end != '\0' || n < lo || n > hi) return false;
  *out = n;
  return true;
}

bool apply_kv(Config* c, const char* key, const char* val) {
  long n;
  if (!strcmp(key, "ble_name")) {
    snprintf(c->ble_name, sizeof(c->ble_name), "%s", val);
  } else if (!strcmp(key, "wifi_ssid")) {
    snprintf(c->wifi_ssid, sizeof(c->wifi_ssid), "%s", val);
  } else if (!strcmp(key, "wifi_pass")) {
    snprintf(c->wifi_pass, sizeof(c->wifi_pass), "%s", val);
  } else if (!strcmp(key, "high_mtu")) {
    c->high_mtu = parse_bool(val);
  } else if (!strcmp(key, "scan_debug")) {
    c->scan_debug = parse_bool(val);
  } else if (!strcmp(key, "prn")) {
    if (!parse_int(val, 0, 65535, &n)) return false;
    c->prn = (uint16_t)n;
  } else if (!strcmp(key, "retries")) {
    if (!parse_int(val, 1, 255, &n)) return false;
    c->retries = (uint8_t)n;
  } else if (!strcmp(key, "retry_cooldown")) {
    if (!parse_int(val, 0, 600, &n)) return false;
    c->retry_cooldown = (uint16_t)n;
  } else if (!strcmp(key, "wedge_cooldown")) {
    if (!parse_int(val, 0, 600, &n)) return false;
    c->wedge_cooldown = (uint16_t)n;
  } else if (!strcmp(key, "min_rssi")) {
    if (!parse_int(val, -127, 0, &n)) return false;
    c->min_rssi = (int8_t)n;
  } else if (!strcmp(key, "tx_power")) {
    if (!parse_int(val, -24, 20, &n)) return false;
    c->tx_power = (int8_t)n;
  } else if (!strcmp(key, "scan_timeout")) {
    if (!parse_int(val, 0, 65535, &n)) return false;
    c->scan_timeout = (uint16_t)n;
  } else if (!strcmp(key, "autostart_delay")) {
    if (!parse_int(val, 0, 65535, &n)) return false;
    c->autostart_delay = (uint16_t)n;
  } else {
    return false;
  }
  return true;
}

int parse(Config* c, const char* text) {
  int applied = 0;
  const char* p = text;
  while (p && *p) {
    const char* eol = strchr(p, '\n');
    size_t len = eol ? (size_t)(eol - p) : strlen(p);

    char line[128];
    if (len >= sizeof(line)) len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = '\0';
    p = eol ? eol + 1 : nullptr;

    trim(line);
    if (line[0] == '\0' || line[0] == '#' || line[0] == ';') continue;
    char* eq = strchr(line, '=');
    if (!eq) continue;
    *eq = '\0';
    char* key = line;
    char* val = eq + 1;
    trim(key);
    trim(val);
    if (apply_kv(c, key, val)) applied++;
  }
  return applied;
}

size_t serialize(const Config& c, char* out, size_t len) {
  int n = snprintf(out, len,
                   "ble_name=%s\n"
                   "prn=%u\n"
                   "high_mtu=%d\n"
                   "retries=%u\n"
                   "retry_cooldown=%u\n"
                   "wedge_cooldown=%u\n"
                   "min_rssi=%d\n"
                   "tx_power=%d\n"
                   "scan_timeout=%u\n"
                   "scan_debug=%d\n"
                   "autostart_delay=%u\n"
                   "wifi_ssid=%s\n"
                   "wifi_pass=%s\n",
                   c.ble_name, c.prn, (int)c.high_mtu, c.retries, c.retry_cooldown,
                   c.wedge_cooldown, (int)c.min_rssi, (int)c.tx_power, c.scan_timeout,
                   (int)c.scan_debug, c.autostart_delay, c.wifi_ssid, c.wifi_pass);
  if (n < 0) return 0;
  return (size_t)n < len ? (size_t)n : (len ? len - 1 : 0);
}

}  // namespace config
