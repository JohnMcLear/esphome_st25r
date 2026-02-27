#include "st25r3916.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cinttypes>

namespace esphome {
namespace st25r3916 {

static const char *const TAG = "st25r3916";

// SPI Protocol constants
static constexpr uint8_t ST25R3916_READ_REG = 0x40;
static constexpr uint8_t ST25R3916_WRITE_REG = 0x00;
static constexpr uint8_t ST25R3916_READ_FIFO = 0xBF;
static constexpr uint8_t ST25R3916_WRITE_FIFO = 0x80;
static constexpr uint8_t ST25R3916_DIRECT_CMD = 0xC0;

void ST25R3916::setup() {
  ESP_LOGCONFIG(TAG, "Setting up ST25R3916...");

  // Setup reset pin
  if (this->reset_pin_ != nullptr) {
    this->reset_pin_->setup();
    this->reset_pin_->digital_write(true);
  }

  // Setup IRQ pin
  if (this->irq_pin_ != nullptr) {
    this->irq_pin_->setup();
  }

  // Initialize SPI
  this->spi_setup();

  // Reset and initialize chip
  if (!this->reset_chip_()) {
    ESP_LOGE(TAG, "Failed to reset chip");
    this->mark_failed();
    return;
  }

  if (!this->init_chip_()) {
    ESP_LOGE(TAG, "Failed to initialize chip");
    this->mark_failed();
    return;
  }

  if (!this->configure_chip_()) {
    ESP_LOGE(TAG, "Failed to configure chip");
    this->mark_failed();
    return;
  }

  ESP_LOGCONFIG(TAG, "ST25R3916 setup complete");
}

void ST25R3916::dump_config() {
  ESP_LOGCONFIG(TAG, "ST25R3916:");
  LOG_PIN("  Reset Pin: ", this->reset_pin_);
  LOG_PIN("  IRQ Pin: ", this->irq_pin_);
  LOG_UPDATE_INTERVAL(this);

  if (this->is_failed()) {
    ESP_LOGE(TAG, "  Setup failed!");
    return;
  }

  // Read and log chip identity
  uint8_t ic_identity = this->read_register_(IC_IDENTITY);
  ESP_LOGCONFIG(TAG, "  IC Identity: 0x%02X", ic_identity);

  // Expected values for ST25R3916/B
  if (ic_identity == 0x05) {
    ESP_LOGCONFIG(TAG, "  Chip: ST25R3916");
  } else if (ic_identity == 0x0A) {
    ESP_LOGCONFIG(TAG, "  Chip: ST25R3916B");
  } else {
    ESP_LOGW(TAG, "  Unknown chip identity!");
  }
}

void ST25R3916::update() {
  this->check_for_tag_();
}

bool ST25R3916::reset_chip_() {
  // Hardware reset if pin is available
  if (this->reset_pin_ != nullptr) {
    ESP_LOGD(TAG, "Performing hardware reset");
    this->reset_pin_->digital_write(false);
    delay(10);
    this->reset_pin_->digital_write(true);
    delay(10);
  }

  // Software reset via SET_DEFAULT command
  this->send_command_(CMD_SET_DEFAULT);
  delay(5);

  // Verify chip is responding
  uint8_t ic_identity = this->read_register_(IC_IDENTITY);
  if (ic_identity != 0x05 && ic_identity != 0x0A) {
    ESP_LOGE(TAG, "Chip not responding correctly (ID: 0x%02X)", ic_identity);
    return false;
  }

  return true;
}

bool ST25R3916::init_chip_() {
  ESP_LOGD(TAG, "Initializing chip");

  // Clear all IRQs
  this->clear_irq_();

  // Set default configuration
  this->send_command_(CMD_SET_DEFAULT);
  delay(5);

  return true;
}

bool ST25R3916::configure_chip_() {
  ESP_LOGD(TAG, "Configuring chip for ISO14443A");

  // Configure I/O - Enable transmitter and receiver
  this->write_register_(IO_CONF1, 0x00);
  this->write_register_(IO_CONF2, 0x00);

  // Configure operation mode for NFC-A
  this->write_register_(MODE, 0x08);  // NFC Active mode

  // Configure ISO14443A settings
  this->write_register_(ISO14443A_NFC, 0x88);  // Enable ISO14443A

  // Set bit rate to 106 kbps
  this->write_register_(BIT_RATE, 0x00);

  // Configure receiver
  this->write_register_(RX_CONF1, 0x04);
  this->write_register_(RX_CONF2, 0x2D);
  this->write_register_(RX_CONF3, 0x00);
  this->write_register_(RX_CONF4, 0x00);

  // Configure timers
  this->write_register_(NO_RESPONSE_TIMER1, 0xFF);
  this->write_register_(NO_RESPONSE_TIMER2, 0x0F);

  // Configure IRQ masks
  this->write_register_(IRQ_MASK_MAIN, 0xFE);      // Enable most interrupts
  this->write_register_(IRQ_MASK_TIMER_NFC, 0xFF);  // Enable timer interrupts
  this->write_register_(IRQ_MASK_ERROR_WUP, 0xFF);  // Enable error interrupts

  // Configure field threshold
  this->write_register_(FIELD_THRESHOLD, 0x0F);

  // Enable regulator
  this->write_register_(REGULATOR_CONTROL, 0x03);

  delay(10);

  return true;
}

void ST25R3916::write_register_(ST25R3916Register reg, uint8_t value) {
  this->enable();
  this->write_byte(ST25R3916_WRITE_REG | (reg & 0x3F));
  this->write_byte(value);
  this->disable();
}

uint8_t ST25R3916::read_register_(ST25R3916Register reg) {
  this->enable();
  this->write_byte(ST25R3916_READ_REG | (reg & 0x3F));
  uint8_t value = this->read_byte();
  this->disable();
  return value;
}

void ST25R3916::modify_register_(ST25R3916Register reg, uint8_t mask, uint8_t value) {
  uint8_t current = this->read_register_(reg);
  uint8_t new_value = (current & ~mask) | (value & mask);
  this->write_register_(reg, new_value);
}

void ST25R3916::read_registers_(ST25R3916Register start_reg, uint8_t *data, size_t length) {
  this->enable();
  this->write_byte(ST25R3916_READ_REG | (start_reg & 0x3F));
  for (size_t i = 0; i < length; i++) {
    data[i] = this->read_byte();
  }
  this->disable();
}

void ST25R3916::send_command_(ST25R3916Command cmd) {
  this->enable();
  this->write_byte(cmd);
  this->disable();
}

void ST25R3916::write_fifo_(const uint8_t *data, size_t length) {
  this->enable();
  this->write_byte(ST25R3916_WRITE_FIFO);
  for (size_t i = 0; i < length; i++) {
    this->write_byte(data[i]);
  }
  this->disable();
}

size_t ST25R3916::read_fifo_(uint8_t *data, size_t max_length) {
  // Read FIFO status to get number of bytes
  uint8_t fifo_status1 = this->read_register_(FIFO_STATUS1);
  uint8_t fifo_status2 = this->read_register_(FIFO_STATUS2);
  
  size_t bytes_in_fifo = ((fifo_status2 & 0x03) << 8) | fifo_status1;
  size_t bytes_to_read = (bytes_in_fifo < max_length) ? bytes_in_fifo : max_length;

  if (bytes_to_read == 0) {
    return 0;
  }

  this->enable();
  this->write_byte(ST25R3916_READ_FIFO);
  for (size_t i = 0; i < bytes_to_read; i++) {
    data[i] = this->read_byte();
  }
  this->disable();

  return bytes_to_read;
}

void ST25R3916::clear_fifo_() {
  this->send_command_(CMD_CLEAR_FIFO);
}

uint32_t ST25R3916::read_irq_status_() {
  uint32_t irq_status = 0;
  
  uint8_t main_irq = this->read_register_(IRQ_MAIN);
  uint8_t timer_irq = this->read_register_(IRQ_TIMER_NFC);
  uint8_t error_irq = this->read_register_(IRQ_ERROR_WUP);

  irq_status = main_irq | (timer_irq << 8) | (error_irq << 16);

  return irq_status;
}

void ST25R3916::clear_irq_() {
  // Reading the IRQ registers clears them
  this->read_irq_status_();
}

void ST25R3916::wait_for_irq_(uint32_t mask, uint32_t timeout_ms) {
  uint32_t start = millis();
  
  while ((millis() - start) < timeout_ms) {
    uint32_t irq_status = this->read_irq_status_();
    if (irq_status & mask) {
      return;
    }
    delay(1);
  }
  
  ESP_LOGW(TAG, "IRQ timeout waiting for mask 0x%08" PRIx32, mask);
}

void ST25R3916::field_on_() {
  ESP_LOGD(TAG, "Turning field ON");
  
  // Enable transmitter and field
  this->modify_register_(OP_CONTROL, 0x80, 0x80);  // Enable TX
  
  // Send field on command
  this->send_command_(CMD_NFC_INITIAL_FIELD_ON);
  
  delay(5);  // Allow field to stabilize
}

void ST25R3916::field_off_() {
  ESP_LOGD(TAG, "Turning field OFF");
  
  // Disable transmitter
  this->modify_register_(OP_CONTROL, 0x80, 0x00);  // Disable TX
  
  delay(5);
}

bool ST25R3916::iso14443a_wakeup_() {
  this->field_on_();
  delay(5);  // ISO14443 requires 5ms minimum
  return true;
}

bool ST25R3916::iso14443a_reqa_(uint8_t *atqa) {
  ESP_LOGD(TAG, "Sending REQA");

  // Clear FIFO
  this->clear_fifo_();

  // Clear IRQs
  this->clear_irq_();

  // Send REQA command (0x26, 7 bits)
  uint8_t reqa_cmd = 0x26;
  this->write_fifo_(&reqa_cmd, 1);

  // Configure for 7-bit transmission
  this->modify_register_(NUM_TX_BYTES2, 0x80, 0x80);  // Set 7-bit mode
  this->write_register_(NUM_TX_BYTES1, 0x00);

  // Transmit without CRC
  this->send_command_(CMD_TRANSMIT_WITHOUT_CRC);

  // Wait for transmission complete
  this->wait_for_irq_(IRQ_TXE, 10);

  // Wait for response
  this->wait_for_irq_(IRQ_RXE | IRQ_NRE, 50);

  // Check for errors
  uint32_t irq_status = this->read_irq_status_();
  if (irq_status & IRQ_NRE) {
    ESP_LOGD(TAG, "No response to REQA");
    return false;
  }

  if (irq_status & (IRQ_CRC | IRQ_PAR | IRQ_ERR1 | IRQ_ERR2)) {
    ESP_LOGD(TAG, "Error in REQA response");
    return false;
  }

  // Read ATQA (2 bytes)
  uint8_t response[2];
  size_t bytes_read = this->read_fifo_(response, 2);
  
  if (bytes_read != 2) {
    ESP_LOGD(TAG, "Invalid ATQA length: %zu", bytes_read);
    return false;
  }

  atqa[0] = response[0];
  atqa[1] = response[1];

  ESP_LOGD(TAG, "ATQA: %02X %02X", atqa[0], atqa[1]);
  return true;
}

bool ST25R3916::iso14443a_anticollision_(uint8_t cascade_level, uint8_t *uid, uint8_t *uid_length) {
  ESP_LOGD(TAG, "Anticollision cascade level %d", cascade_level);

  uint8_t sel_cmd[2];
  sel_cmd[0] = 0x93 + (cascade_level * 2);  // SEL command (0x93, 0x95, 0x97)
  sel_cmd[1] = 0x20;  // NVB (Number of Valid Bits) = 0x20 (full anticollision)

  // Clear FIFO and IRQs
  this->clear_fifo_();
  this->clear_irq_();

  // Write anticollision command to FIFO
  this->write_fifo_(sel_cmd, 2);

  // Configure for normal 8-bit transmission
  this->write_register_(NUM_TX_BYTES2, 0x00);
  this->write_register_(NUM_TX_BYTES1, 0x02);

  // Transmit without CRC
  this->send_command_(CMD_TRANSMIT_WITHOUT_CRC);

  // Wait for transmission and response
  this->wait_for_irq_(IRQ_TXE, 10);
  this->wait_for_irq_(IRQ_RXE | IRQ_NRE, 50);

  // Check for errors
  uint32_t irq_status = this->read_irq_status_();
  if (irq_status & IRQ_NRE) {
    ESP_LOGD(TAG, "No response to anticollision");
    return false;
  }

  // Read UID response (5 bytes: 4 UID bytes + BCC)
  uint8_t response[5];
  size_t bytes_read = this->read_fifo_(response, 5);

  if (bytes_read != 5) {
    ESP_LOGD(TAG, "Invalid anticollision response length: %zu", bytes_read);
    return false;
  }

  // Verify BCC (XOR of 4 UID bytes should equal BCC)
  uint8_t bcc = response[0] ^ response[1] ^ response[2] ^ response[3];
  if (bcc != response[4]) {
    ESP_LOGD(TAG, "BCC mismatch: calculated %02X, received %02X", bcc, response[4]);
    return false;
  }

  // Copy UID bytes
  for (int i = 0; i < 4; i++) {
    uid[i] = response[i];
  }
  *uid_length = 4;

  ESP_LOGD(TAG, "UID: %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3]);
  return true;
}

bool ST25R3916::iso14443a_select_(uint8_t cascade_level, const uint8_t *uid) {
  ESP_LOGD(TAG, "Select cascade level %d", cascade_level);

  uint8_t sel_cmd[7];
  sel_cmd[0] = 0x93 + (cascade_level * 2);  // SEL command
  sel_cmd[1] = 0x70;  // NVB = 0x70 (full select)
  
  // Copy UID and BCC
  for (int i = 0; i < 4; i++) {
    sel_cmd[2 + i] = uid[i];
  }
  sel_cmd[6] = uid[0] ^ uid[1] ^ uid[2] ^ uid[3];  // BCC

  // Clear FIFO and IRQs
  this->clear_fifo_();
  this->clear_irq_();

  // Write select command to FIFO
  this->write_fifo_(sel_cmd, 7);

  // Configure transmission
  this->write_register_(NUM_TX_BYTES2, 0x00);
  this->write_register_(NUM_TX_BYTES1, 0x07);

  // Transmit with CRC
  this->send_command_(CMD_TRANSMIT_WITH_CRC);

  // Wait for transmission and response
  this->wait_for_irq_(IRQ_TXE, 10);
  this->wait_for_irq_(IRQ_RXE | IRQ_NRE, 50);

  // Check for errors
  uint32_t irq_status = this->read_irq_status_();
  if (irq_status & IRQ_NRE) {
    ESP_LOGD(TAG, "No response to select");
    return false;
  }

  // Read SAK
  uint8_t sak;
  size_t bytes_read = this->read_fifo_(&sak, 1);

  if (bytes_read != 1) {
    ESP_LOGD(TAG, "Invalid SAK length: %zu", bytes_read);
    return false;
  }

  ESP_LOGD(TAG, "SAK: %02X", sak);

  // Check if cascade bit is set (UID not complete)
  if (sak & 0x04) {
    ESP_LOGD(TAG, "Cascade bit set, UID not complete");
    return false;
  }

  return true;
}

bool ST25R3916::iso14443a_read_uid_(std::string &uid_string) {
  uint8_t uid[10];  // Maximum UID length (3 cascade levels * 4 bytes, but minus cascade tags)
  uint8_t uid_length = 0;
  
  // Wakeup field
  if (!this->iso14443a_wakeup_()) {
    return false;
  }

  // Send REQA
  uint8_t atqa[2];
  if (!this->iso14443a_reqa_(atqa)) {
    return false;
  }

  // Determine number of cascade levels from ATQA
  // For simplicity, we'll try up to 3 cascade levels
  for (uint8_t cascade = 0; cascade < 3; cascade++) {
    uint8_t cascade_uid[4];
    uint8_t cascade_uid_length;

    // Perform anticollision
    if (!this->iso14443a_anticollision_(cascade, cascade_uid, &cascade_uid_length)) {
      if (cascade == 0) {
        return false;  // Failed on first cascade
      }
      break;  // No more cascades
    }

    // Check for cascade tag (CT = 0x88)
    if (cascade_uid[0] == 0x88) {
      // This is a cascade level, copy remaining 3 bytes
      for (int i = 1; i < 4; i++) {
        uid[uid_length++] = cascade_uid[i];
      }
    } else {
      // Final UID bytes
      for (int i = 0; i < 4; i++) {
        uid[uid_length++] = cascade_uid[i];
      }
      
      // Select this level (which should be final)
      if (!this->iso14443a_select_(cascade, cascade_uid)) {
        return false;
      }
      break;  // UID complete
    }

    // Select this cascade level to continue
    if (!this->iso14443a_select_(cascade, cascade_uid)) {
      return false;
    }
  }

  // Convert UID to hex string
  char uid_hex[21];  // Max 10 bytes * 2 + null terminator
  for (uint8_t i = 0; i < uid_length; i++) {
    snprintf(&uid_hex[i * 2], 3, "%02X", uid[i]);
  }
  uid_string = std::string(uid_hex, uid_length * 2);

  ESP_LOGD(TAG, "Complete UID: %s (length: %d)", uid_string.c_str(), uid_length);
  return true;
}

void ST25R3916::check_for_tag_() {
  std::string uid;
  
  if (this->iso14443a_read_uid_(uid)) {
    // Tag detected
    if (!this->tag_present_ || this->current_uid_ != uid) {
      // New tag or different tag
      ESP_LOGI(TAG, "Tag detected: %s", uid.c_str());
      this->tag_present_ = true;
      this->current_uid_ = uid;
      this->last_tag_detection_time_ = millis();

      // Trigger on_tag callbacks
      for (auto *trigger : this->on_tag_triggers_) {
        trigger->trigger(uid);
      }
    } else {
      // Same tag still present
      this->last_tag_detection_time_ = millis();
    }
  } else {
    // No tag detected
    if (this->tag_present_) {
      // Check if tag has been gone long enough
      if ((millis() - this->last_tag_detection_time_) > TAG_REMOVAL_TIMEOUT_MS) {
        ESP_LOGI(TAG, "Tag removed: %s", this->current_uid_.c_str());
        
        // Trigger on_tag_removed callbacks
        for (auto *trigger : this->on_tag_removed_triggers_) {
          trigger->trigger(this->current_uid_);
        }
        
        this->tag_present_ = false;
        this->current_uid_ = "";
      }
    }
  }

  // Turn off field between reads to save power
  this->field_off_();
}

}  // namespace st25r3916
}  // namespace esphome
