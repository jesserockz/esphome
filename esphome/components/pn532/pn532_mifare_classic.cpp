#include "pn532.h"
#include "esphome/core/log.h"

namespace esphome {
namespace pn532 {

static const char *TAG = "pn532.mifare_classic";

nfc::NfcTag *PN532::read_mifare_classic_tag_(std::vector<uint8_t> &uid) {
  uint8_t current_block = 4;
  uint8_t message_start_index = 0;
  uint32_t message_length = 0;

  if (this->auth_mifare_classic_block_(uid, current_block, nfc::MIFARE_CMD_AUTH_A, nfc::NDEF_KEY)) {
    std::vector<uint8_t> data;
    if (!this->read_mifare_classic_block_(current_block, data)) {
      if (!nfc::decode_mifare_classic_tlv(data, message_length, message_start_index)) {
        return new nfc::NfcTag(uid, nfc::ERROR);
      }
    } else {
      ESP_LOGE(TAG, "Failed to read block %d", current_block);
      return new nfc::NfcTag(uid, nfc::MIFARE_CLASSIC);
    }
  } else {
    ESP_LOGV(TAG, "Tag is not NDEF formatted");
    return new nfc::NfcTag(uid, nfc::MIFARE_CLASSIC);
  }

  uint32_t index = 0;
  uint32_t buffer_size = nfc::get_mifare_classic_buffer_size(message_length);
  std::vector<uint8_t> buffer;

  while (index < buffer_size) {
    if (nfc::mifare_classic_is_first_block(current_block)) {
      if (!this->auth_mifare_classic_block_(uid, current_block, nfc::MIFARE_CMD_AUTH_A, nfc::NDEF_KEY)) {
        ESP_LOGE(TAG, "Error, Block authentication failed for %d", current_block);
      }
    }
    std::vector<uint8_t> block_data;
    if (this->read_mifare_classic_block_(current_block, block_data)) {
      buffer.insert(buffer.end(), block_data.begin(), block_data.end());
    } else {
      ESP_LOGE(TAG, "Error reading block %d", current_block);
    }

    index += nfc::MIFARE_CLASSIC_BLOCK_SIZE;
    current_block++;

    if (nfc::mifare_classic_is_trailer_block(current_block)) {
      current_block++;
    }
  }
  buffer.erase(buffer.begin(), buffer.begin() + message_start_index);
  return new nfc::NfcTag(uid, nfc::MIFARE_CLASSIC, buffer);
}

bool PN532::read_mifare_classic_block_(uint8_t block_num, std::vector<uint8_t> &data) {
  if (!this->write_command_({
          PN532_COMMAND_INDATAEXCHANGE,
          0x01,  // One card
          nfc::MIFARE_CMD_READ,
          block_num,
      })) {
    return false;
  }

  if (!this->read_response_(PN532_COMMAND_INDATAEXCHANGE, data) || data[0] != 0x00) {
    return false;
  }
  data.erase(data.begin());

  ESP_LOGVV(TAG, " Block %d: %s", block_num, nfc::format_bytes(data).c_str());
  return true;
}

bool PN532::auth_mifare_classic_block_(std::vector<uint8_t> &uid, uint8_t block_num, uint8_t key_num,
                                       const uint8_t *key) {
  std::vector<uint8_t> data({
      PN532_COMMAND_INDATAEXCHANGE,
      0x01,       // One card
      key_num,    // Mifare Key slot
      block_num,  // Block number
  });
  data.insert(data.end(), key, key + 6);
  data.insert(data.end(), uid.begin(), uid.end());
  if (!this->write_command_(data)) {
    ESP_LOGE(TAG, "Authentication failed - Block %d", block_num);
    return false;
  }

  std::vector<uint8_t> response;
  if (!this->read_response_(PN532_COMMAND_INDATAEXCHANGE, response) || response[0] != 0x00) {
    ESP_LOGE(TAG, "Authentication failed - Block %d", block_num);
    return false;
  }

  return true;
}

bool PN532::format_mifare_classic_mifare_(std::vector<uint8_t> &uid) {
  std::vector<uint8_t> blank_buffer(
      {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  std::vector<uint8_t> trailer_buffer(
      {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x07, 0x80, 0x69, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});

  bool error = false;

  for (int idx = 0; idx < 16; idx++) {
    if (!this->auth_mifare_classic_block_(uid, (4 * idx) + 3, nfc::MIFARE_CMD_AUTH_B, nfc::DEFAULT_KEY)) {
      ESP_LOGE(TAG, "No keys work!!! sector %d", idx);
      continue;
    }

    if (idx == 0) {
      if (!this->write_mifare_classic_block_((4 * idx) + 1, blank_buffer)) {
        ESP_LOGE(TAG, "Unable to write sector %d-%d", idx, (4 * idx) + 1);
        error = true;
      }
    } else {
      if (!this->write_mifare_classic_block_((4 * idx), blank_buffer)) {
        ESP_LOGE(TAG, "Unable to write sector %d-%d", idx, (4 * idx));
        error = true;
      }
      if (!this->write_mifare_classic_block_((4 * idx) + 1, blank_buffer)) {
        ESP_LOGE(TAG, "Unable to write sector %d-%d", idx, (4 * idx) + 1);
        error = true;
      }
    }

    if (!this->write_mifare_classic_block_((4 * idx) + 2, blank_buffer)) {
      ESP_LOGE(TAG, "Unable to write sector %d-%d", idx, (4 * idx) + 2);
      error = true;
    }

    if (!this->write_mifare_classic_block_((4 * idx) + 3, trailer_buffer)) {
      ESP_LOGE(TAG, "Unable to write trailer of sector %d-%d", idx, (4 * idx) + 3);
      error = true;
    }
  }

  return !error;
}

bool PN532::format_mifare_classic_ndef_(std::vector<uint8_t> &uid) {
  std::vector<uint8_t> empty_ndef_message(
      {0x03, 0x03, 0xD0, 0x00, 0x00, 0xFE, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  std::vector<uint8_t> sector_buffer_0(
      {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
  std::vector<uint8_t> sector_buffer_1(
      {0x14, 0x01, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1});
  std::vector<uint8_t> sector_buffer_2(
      {0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1, 0x03, 0xE1});
  std::vector<uint8_t> sector_buffer_3(
      {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0x78, 0x77, 0x88, 0xC1, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
  std::vector<uint8_t> sector_buffer_4(
      {0xD3, 0xF7, 0xD3, 0xF7, 0xD3, 0xF7, 0x7F, 0x07, 0x88, 0x40, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});

  if (!this->auth_mifare_classic_block_(uid, 0, nfc::MIFARE_CMD_AUTH_B, nfc::DEFAULT_KEY)) {
    ESP_LOGE(TAG, "Unable to authenticate block 0 for formatting!");
    return false;
  }
  if (!this->write_mifare_classic_block_(1, sector_buffer_1))
    return false;
  if (!this->write_mifare_classic_block_(2, sector_buffer_2))
    return false;
  if (!this->write_mifare_classic_block_(3, sector_buffer_3))
    return false;

  for (int i = 4; i < 64; i += 4) {
    if (!this->auth_mifare_classic_block_(uid, i, nfc::MIFARE_CMD_AUTH_B, nfc::DEFAULT_KEY)) {
      ESP_LOGE(TAG, "Failed to authenticate with block %d", i);
      continue;
    }
    if (i == 4) {
      if (!this->write_mifare_classic_block_(i, empty_ndef_message))
        ESP_LOGE(TAG, "Unable to write block %d", i);
    } else {
      if (!this->write_mifare_classic_block_(i, sector_buffer_0))
        ESP_LOGE(TAG, "Unable to write block %d", i);
    }
    if (!this->write_mifare_classic_block_(i + 1, sector_buffer_0))
      ESP_LOGE(TAG, "Unable to write block %d", i + 1);
    if (!this->write_mifare_classic_block_(i + 2, sector_buffer_0))
      ESP_LOGE(TAG, "Unable to write block %d", i + 2);
    if (!this->write_mifare_classic_block_(i + 3, sector_buffer_4))
      ESP_LOGE(TAG, "Unable to write block %d", i + 3);
  }
  return true;
}

bool PN532::write_mifare_classic_block_(uint8_t block_num, std::vector<uint8_t> &write_data) {
  std::vector<uint8_t> data({
      PN532_COMMAND_INDATAEXCHANGE,
      0x01,  // One card
      nfc::MIFARE_CMD_WRITE,
      block_num,
  });
  data.insert(data.end(), write_data.begin(), write_data.end());
  if (!this->write_command_(data)) {
    ESP_LOGE(TAG, "Error writing block %d", block_num);
    return false;
  }

  std::vector<uint8_t> response;
  if (!this->read_response_(PN532_COMMAND_INDATAEXCHANGE, response)) {
    ESP_LOGE(TAG, "Error writing block %d", block_num);
    return false;
  }

  return true;
}

bool PN532::write_mifare_classic_tag_(std::vector<uint8_t> &uid, nfc::NdefMessage *message) {
  auto encoded = message->encode();

  uint32_t message_length = encoded.size();
  uint32_t buffer_length = nfc::get_mifare_classic_buffer_size(message_length);

  encoded.insert(encoded.begin(), 0x03);
  if (message_length < 255) {
    encoded.insert(encoded.begin() + 1, message_length);
  } else {
    encoded.insert(encoded.begin() + 1, 0xFF);
    encoded.insert(encoded.begin() + 2, (message_length >> 8) & 0xFF);
    encoded.insert(encoded.begin() + 3, message_length & 0xFF);
  }
  encoded.push_back(0xFE);

  encoded.resize(buffer_length, 0);

  uint32_t index = 0;
  uint8_t current_block = 4;

  while (index < buffer_length) {
    if (nfc::mifare_classic_is_first_block(current_block)) {
      if (!this->auth_mifare_classic_block_(uid, current_block, nfc::MIFARE_CMD_AUTH_A, nfc::NDEF_KEY)) {
        return false;
      }
    }

    std::vector<uint8_t> data(encoded.begin() + index, encoded.begin() + index + nfc::MIFARE_CLASSIC_BLOCK_SIZE);
    if (!this->write_mifare_classic_block_(current_block, data)) {
      return false;
    }
    index += nfc::MIFARE_CLASSIC_BLOCK_SIZE;
    current_block++;

    if (nfc::mifare_classic_is_trailer_block(current_block)) {
      // Skipping as cannot write to trailer
      current_block++;
    }
  }
  return true;
}

}  // namespace pn532
}  // namespace esphome
