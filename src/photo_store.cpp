#include "photo_store.h"
#include <esp_partition.h>

// Layout in the partition: a small header in the first sector, the planes from 4 KB on.
// The header is written last, so an interrupted upload never looks like a valid photo.
struct Header {
  uint32_t magic;
  uint32_t version;
  uint32_t size;
};
constexpr uint32_t MAGIC = 0x50484F54;  // "PHOT"
constexpr size_t DATA_AT = 4096;

static const esp_partition_t *part() {
  static const esp_partition_t *p =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, nullptr);
  return p;
}

static bool readHeader(Header &h) {
  return part() && esp_partition_read(part(), 0, &h, sizeof(h)) == ESP_OK && h.magic == MAGIC &&
         h.size == PHOTO_BYTES;
}

uint32_t photoVersion() {
  Header h;
  return readHeader(h) ? h.version : 0;
}

bool photoLoad(uint8_t *black, uint8_t *red) {
  Header h;
  if (!readHeader(h)) return false;
  return esp_partition_read(part(), DATA_AT, black, PHOTO_PLANE) == ESP_OK &&
         esp_partition_read(part(), DATA_AT + PHOTO_PLANE, red, PHOTO_PLANE) == ESP_OK;
}

static size_t written = 0;
static uint32_t nextVersion = 1;

bool photoUploadBegin() {
  if (!part() || part()->size < DATA_AT + PHOTO_BYTES) return false;
  nextVersion = photoVersion() + 1;
  written = 0;
  // Erase everything first, header included, so a partial upload leaves no photo
  return esp_partition_erase_range(part(), 0, part()->size) == ESP_OK;
}

bool photoUploadWrite(const uint8_t *data, size_t len) {
  if (written + len > PHOTO_BYTES) return false;
  if (esp_partition_write(part(), DATA_AT + written, data, len) != ESP_OK) return false;
  written += len;
  return true;
}

bool photoUploadFinish() {
  if (written != PHOTO_BYTES) return false;
  Header h = {MAGIC, nextVersion, PHOTO_BYTES};
  return esp_partition_write(part(), 0, &h, sizeof(h)) == ESP_OK;
}
