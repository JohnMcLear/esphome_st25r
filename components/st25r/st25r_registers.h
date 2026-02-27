#pragma once

#include <cstdint>

namespace esphome {
namespace st25r {

enum ST25R3916Register {
  ST25R3916_REG_IO_CONF1 = 0x00,
  ST25R3916_REG_IO_CONF2 = 0x01,
  ST25R3916_REG_OP_CONTROL = 0x02,
  ST25R3916_REG_MODE = 0x03,
  ST25R3916_REG_BIT_RATE = 0x04,
  // ... other registers as needed
  ST25R3916_REG_IC_IDENTITY = 0x3F,
};

enum ST25R3916DirectCommand {
  ST25R3916_CMD_SET_DEFAULT = 0xC1,
  ST25R3916_CMD_CLEAR_FIFO = 0xC2,
  ST25R3916_CMD_TRANSMIT_WITH_CRC = 0xC4,
  ST25R3916_CMD_TRANSMIT_WITHOUT_CRC = 0xC5,
  // ... other commands as needed
};

}  // namespace st25r
}  // namespace esphome
