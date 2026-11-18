#include <string.h>
#include <unity.h>

#include "../fixtures/app_zip.h"  // real `adafruit-nrfutil dfu genpkg` output
#include "config.h"
#include "firmware_zip.h"
#include "zip_reader.h"

void setUp() {}
void tearDown() {}

// ---------------- zip_reader ----------------

static void test_zip_lists_entries() {
  zip_reader::MemSource src(app_zip, app_zip_len);
  zip_reader::Reader zip(&src);
  zip_reader::Entry e;
  int count = 0;
  while (zip.next(&e)) count++;
  TEST_ASSERT_EQUAL(3, count);
}

static void test_zip_reads_bin() {
  zip_reader::MemSource src(app_zip, app_zip_len);
  zip_reader::Reader zip(&src);
  zip_reader::Entry e;
  TEST_ASSERT_TRUE(zip.find("app.bin", &e));
  TEST_ASSERT_EQUAL_UINT32(64, e.size);
  uint8_t buf[64];
  TEST_ASSERT_EQUAL(64, zip.read(e, 0, buf, sizeof(buf)));
  for (int i = 0; i < 64; i++) TEST_ASSERT_EQUAL_UINT8(i, buf[i]);
  // Partial read clamps at entry end.
  TEST_ASSERT_EQUAL(4, zip.read(e, 60, buf, 32));
  TEST_ASSERT_EQUAL(0, zip.read(e, 64, buf, 32));
}

static void test_zip_rejects_garbage() {
  const uint8_t junk[] = "not a zip at all";
  zip_reader::MemSource src(junk, sizeof(junk));
  zip_reader::Reader zip(&src);
  zip_reader::Entry e;
  TEST_ASSERT_FALSE(zip.next(&e));
}

static void test_zip_rejects_deflated() {
  static uint8_t copy[2048];
  memcpy(copy, app_zip, app_zip_len);
  copy[8] = 8;  // first LFH method -> DEFLATE
  zip_reader::MemSource src(copy, app_zip_len);
  zip_reader::Reader zip(&src);
  zip_reader::Entry e;
  TEST_ASSERT_FALSE(zip.next(&e));
}

// ---------------- firmware_zip ----------------

static void test_manifest_parses_application() {
  zip_reader::MemSource src(app_zip, app_zip_len);
  zip_reader::Reader zip(&src);
  firmware_zip::Parsed p;
  char err[96];
  TEST_ASSERT_TRUE_MESSAGE(firmware_zip::parse(zip, &p, err, sizeof(err)), err);
  TEST_ASSERT_EQUAL_STRING("app.bin", p.bin.name);
  TEST_ASSERT_EQUAL_STRING("app.dat", p.dat.name);
  TEST_ASSERT_EQUAL_UINT32(64, p.bin.size);
  TEST_ASSERT_EQUAL_UINT32(14, p.dat.size);
}

static void test_manifest_missing_fails_with_reason() {
  const uint8_t junk[] = "xxxx";
  zip_reader::MemSource src(junk, sizeof(junk));
  zip_reader::Reader zip(&src);
  firmware_zip::Parsed p;
  char err[96];
  TEST_ASSERT_FALSE(firmware_zip::parse(zip, &p, err, sizeof(err)));
  TEST_ASSERT_TRUE(strlen(err) > 0);
}

// ---------------- config ----------------

static void test_config_defaults() {
  config::Config c;
  config::apply_defaults(&c);
  TEST_ASSERT_EQUAL(8, c.prn);
  TEST_ASSERT_EQUAL(240, c.autostart_delay);
  TEST_ASSERT_EQUAL_STRING("dronedfu", c.wifi_pass);
}

static void test_config_parse() {
  config::Config c;
  config::apply_defaults(&c);
  const char* text =
      "# comment\r\n"
      "; other comment\n"
      "  ble_name = XIAO_OTA  \n"
      "prn=12\n"
      "high_mtu=0\n"
      "min_rssi=-70\n"
      "tx_power=99\n"        // out of range -> ignored
      "retries=abc\n"        // not a number -> ignored
      "autostart_delay=30\n"
      "bogus=1\n"
      "wifi_ssid=MyAP";      // no trailing newline
  int n = config::parse(&c, text);
  TEST_ASSERT_EQUAL(6, n);
  TEST_ASSERT_EQUAL_STRING("XIAO_OTA", c.ble_name);
  TEST_ASSERT_EQUAL(12, c.prn);
  TEST_ASSERT_FALSE(c.high_mtu);
  TEST_ASSERT_EQUAL(-70, c.min_rssi);
  TEST_ASSERT_EQUAL(9, c.tx_power);
  TEST_ASSERT_EQUAL(3, c.retries);
  TEST_ASSERT_EQUAL(30, c.autostart_delay);
  TEST_ASSERT_EQUAL_STRING("MyAP", c.wifi_ssid);
}

static void test_config_roundtrip() {
  config::Config a, b;
  config::apply_defaults(&a);
  config::parse(&a, "ble_name=Foo\nprn=3\nmin_rssi=-55\nscan_debug=1\n");
  char buf[512];
  config::serialize(a, buf, sizeof(buf));
  config::apply_defaults(&b);
  config::parse(&b, buf);
  TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof(a));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_zip_lists_entries);
  RUN_TEST(test_zip_reads_bin);
  RUN_TEST(test_zip_rejects_garbage);
  RUN_TEST(test_zip_rejects_deflated);
  RUN_TEST(test_manifest_parses_application);
  RUN_TEST(test_manifest_missing_fails_with_reason);
  RUN_TEST(test_config_defaults);
  RUN_TEST(test_config_parse);
  RUN_TEST(test_config_roundtrip);
  return UNITY_END();
}
