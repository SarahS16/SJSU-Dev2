// Relevant Datasheet:
// https://www.sdcard.org/downloads/pls/pdf/index.php?p=Part1_Physical_Layer_Simplified_Specification_Ver6.00.jpg&f=Part1_Physical_Layer_Simplified_Specification_Ver6.00.pdf&e=EN_SS1
#pragma once

#include <cstdint>
#include <type_traits>

#include "L1_Peripheral/gpio.hpp"
#include "L1_Peripheral/spi.hpp"
#include "utility/log.hpp"
#include "utility/crc.hpp"

namespace sjsu
{
// TODO(#348):  Write a class to represent a single Sd Card Block to make use
//              of block-related functions easier.

/// SD card controller utilizing the SPI peripheral
class Sd
{
 public:
  /// This value was found through experimentation. The suggested value (found
  /// via research) is to wait at least 1-8 bytes after sending a command's CRC
  /// before expecting a response from the card.
  /// See https://luckyresistor.me/cat-protector/software/sdcard-2/
  static constexpr uint8_t kBusTimeout = 250;

  /// Enforcing block-size cross-compatibility
  static constexpr uint16_t kBlockSize = 512;

  /// Table holding CRC8 tokens used to verify SD card transactions.
  static constexpr sjsu::crc::CrcTableConfig_t<uint8_t> kCrcTable8 =
      sjsu::crc::GenerateCrc7Table<uint8_t>();

  /// Table holding CRC16 tokens used to verify SD card transactions.
  static constexpr sjsu::crc::CrcTableConfig_t<uint16_t> kCrcTable16 =
      sjsu::crc::GenerateCrc16Table();

  /// SD Card constructor
  ///
  /// @param spi - spi peripheral connected to SD card
  /// @param chip_select - gpio connected to SD card's chip select
  explicit constexpr Sd(const Spi & spi, const Gpio & chip_select)
      : spi_(spi), chip_select_(chip_select)
  {
  }

  /// A response frame struct to contain the various responses sent by the card
  /// after commands are issued (response type and length depend on the command
  /// sent)
  struct Response_t
  {
    /// Holds the contents of the response
    union {
      uint64_t qWord;
      struct
      {
        uint32_t hi;
        uint32_t lo;
      } dWord;
      uint8_t byte[8];
    } data;
    /// Length of the response
    uint32_t length;
  };

  /// Enumerator for response type codes (See Physical Layer V6.00 p.225)
  enum class ResponseType
  {
    kR1,
    kR1b,
    kR2,
    kR3,
    kR4,
    kR5,
    kR6,
    kR7
  };

  /// Enumerator for SD Card Capacity type codes
  enum class Type
  {
    kSDSC,  // up to 2GB
    kSDHC,  // up to 32GB
    kSDXC   // up to 256GB
  };

  /// bit shift counts provided to allow easy checking of the OCR flag bits for
  /// the card's operating conditions
  enum class Ocr
  {
    kIsDualVoltage = 7,  // OCR bit 7: Supports both Low and High
                         // Voltage Ranges
    kIs27vTo28v = 15,    // OCR bit 15: Supports 2.7V-2.8V
    kIs28vTo29v = 16,    // OCR bit 16: Supports 2.8V-2.9V
    kIs29vTo30v = 17,    // OCR bit 17: Supports 2.9V-3.0V
    kIs30vTo31v = 18,    // OCR bit 18: Supports 3.0V-3.1V
    kIs31vTo32v = 19,    // OCR bit 19: Supports 3.1V-3.2V
    kIs32vTo33v = 20,    // OCR bit 20: Supports 3.2V-3.3V
    kIs33vTo34v = 21,    // OCR bit 21: Supports 3.3V-3.4V
    kIs34vTo35v = 22,    // OCR bit 22: Supports 3.4V-3.5V
    kIs35vTo36v = 23,    // OCR bit 23: Supports 3.5V-3.6V
    kIs18vOk    = 24,    // OCR bit 24: Switching to 1.8V is
                         // accepted (S18A)
    kIsUhs2 = 29,        // OCR bit 29: Card is UHS-II Compatible
    kCcs    = 30,        // OCR bit 30: Card Capacity Status (used)
                         // to determine if a card is SDSC or not
    kPwrUpComplete = 31  // OCR bit 31: Card Power Up Status (i.e.
                         // if card has finished the power cycle
                         // process)
  };

  /// A collection of SPI-supported command codes to be used with SendCmd()
  ///
  /// @note  OR of b0100_0000 is meant to conform with the SD protocol
  ///        (i.e. first bits must be b01)
  enum class Command
  {
    kGarbage = 0xFF,           // "SendGarbage" command; instructs
                               // SendCmd() to send 0xFFFFFFFF as data
                               // and even set the checksum as 0xFF
    kReset = 0x40 | 0,         // CMD0: reset the sd card (force it to go
                               // to the idle state)
    kInit  = 0x40 | 1,         // CMD1: starts an initiation of the card
    kGetOp = 0x40 | 8,         // CMD8: request the sd card's support of
                               // the provided host's voltage ranges
    kGetCsd = 0x40 | 9,        // CMD9: request the sd card's CSD
                               // (card-specific data) register
    kStopTrans = 0x40 | 12,    // CMD12: terminates a multi-block read or
                               // write operation
    kGetStatus = 0x40 | 13,    // CMD13: get status register
    kChgBlkLen = 0x40 | 16,    // CMD16: change block length (only
                               // effective in SDSC cards; SDHC/SDXC
                               // cards are locked to 512-byte blocks)
    kReadSingle = 0x40 | 17,   // CMD17: read a single block of data
    kReadMulti  = 0x40 | 18,   // CMD18: read many blocks of data until
                               // a "CMD12" frame is sent
    kWriteSingle = 0x40 | 24,  // CMD24: write a single block of data
    kWriteMulti  = 0x40 | 25,  // CMD25: write many blocks of data until
                               // a "CMD12" frame is sent
    kDelFrom = 0x40 | 32,      // CMD32: set address of the start block
                               // for deletion
    kDelTo = 0x40 | 33,        // CMD33: set address of the end block for
                               // deletion
    kDel = 0x40 | 38,          // CMD38: begin deletion from the block
                               // range specified by the vector
                               // [DEL_FROM : DEL_TO]
    kAcBegin = 0x40 | 55,      // CMD55: signals the start of an
                               // application-specific command
    kGetOcr = 0x40 | 58,       // CMD58: request data from the
                               // operational conditions register
    kAcInit = 0x40 | 41        // CMD41: application-specific version of
                               // CMD1 (must precede with CMD55)
  };

  /// Enumerations to allow CS keep-alive during command byte exchanges
  enum class KeepAlive : bool
  {
    kYes = true,
    kNo  = false
  };

  // TODO(#350): Add support for CID/CSD Registers
  /// Structure for recording information about the SD Card
  struct CardInfo_t
  {
    /// Operation conditions register, such as operating voltage.
    union {
      uint32_t dWord;
      uint16_t word[2];
      uint8_t byte[4];
    } ocr;
    /// Card Capacity type
    Type type;
    /// Buffer for responses
    Response_t response;
  };

  /// Initialize peripherals
  virtual void Initialize()
  {
    sjsu::LogDebug("Begin initialization:");
    sjsu::LogDebug("Setting CS as output...");
    chip_select_.SetDirection(Gpio::Direction::kOutput);
    chip_select_.Set(Gpio::State::kHigh);

    sjsu::LogDebug("Initializing SPI Clock Speed...");
    spi_.Initialize();
    sjsu::LogDebug("Setting SPI Clock Speed...");
    spi_.SetClock(12_MHz);
    sjsu::LogDebug("Setting Peripheral Mode...");
    spi_.SetDataSize(Spi::DataSize::kEight);
    sjsu::LogDebug("Starting SPI Peripheral...");
  }

  // TODO(#349): Split method into smaller piece with single jobs
  /// Initialize and enable SD Card
  virtual bool Mount(CardInfo_t * sd)
  {
    uint8_t tries     = 0;
    bool card_is_idle = false;

    // Reset the card and force it to go to idle state at <400kHz with a
    // CMD0 + (active-low) CS
    sjsu::LogDebug("Sending SD Card to Idle State...");
    sd->response.length = SendCmd(
        Command::kReset, 0x00000000, sd->response.data.byte, KeepAlive::kYes);

    // Reset the card again to trigger SPI mode
    sjsu::LogDebug("Initializing SPI mode...");
    do
    {
      tries++;
      sd->response.length = SendCmd(
          Command::kReset, 0x00, sd->response.data.byte, KeepAlive::kYes);

      // Check if R1 response frame's bit 1 is set (to ensure that
      // card is in idle state)
      if (sd->response.data.byte[0] != 0xFF &&
          (sd->response.data.byte[0] & 0x01) == 0x01)
      {
        // If it is, we can move on; otherwise, keep trying for a set
        // amount of tries
        card_is_idle = true;
      }
      Delay(10ms);
    } while (tries < kBusTimeout && !card_is_idle);
    sjsu::LogDebug("%d tries", tries);
    if (tries >= kBusTimeout)
    {
      sjsu::LogError("Failed to initiate SPI mode within timeout. Aborting!");
      sd->response.length = SendCmd(Command::kGarbage,
                                    0xFFFFFFFF,
                                    sd->response.data.byte,
                                    KeepAlive::kNo);
      return false;
    }

    // Send the host's supported voltage (3.3V) and ask if the card
    // supports it
    sjsu::LogDebug("Checking Current SD Card Voltage Level...");
    constexpr uint8_t kCheckPattern = 0xAB;
    uint64_t supported_voltage      = 0x00000001;
    sd->response.length =
        SendCmd(Command::kGetOp,
                static_cast<uint32_t>((supported_voltage << 8) | kCheckPattern),
                sd->response.data.byte,
                KeepAlive::kYes);
    if (sd->response.data.byte[4] != kCheckPattern)
    {
      // If the last byte is not an exact echo of the LSB of the kGetOp
      // command's argument, this response is invalid
      sjsu::LogError("Response integrity check failed. Aborting!");
      sd->response.length = SendCmd(Command::kGarbage,
                                    0xFFFFFFFF,
                                    sd->response.data.byte,
                                    KeepAlive::kNo);
      return false;
    }
    else if (sd->response.data.byte[3] &
             (supported_voltage == static_cast<uint64_t>(0x00)))
    {
      // If the 2nd-to-last byte of the reponse AND with our host device's
      // supported voltage range is 0x00, the SD card doesn't support our
      // device's operating voltage
      sjsu::LogError("Unsupported voltage in use. Aborting!");
      sd->response.length = SendCmd(Command::kGarbage,
                                    0xFFFFFFFF,
                                    sd->response.data.byte,
                                    KeepAlive::kNo);
      return false;
    }

    // Indicate that the host supports SDHC/SDXC and wait for card to
    // shift out of idle state
    sjsu::LogDebug("Expressing High-Capacity SD Card Support...");
    tries = 0;
    do
    {
      // Send host's operating conditions
      sd->response.length = SendCmd(
          Command::kInit, 0x40000000, sd->response.data.byte, KeepAlive::kYes);
      tries++;
    } while (tries < kBusTimeout && sd->response.data.byte[0] & 0x01);
    if (tries == kBusTimeout)
    {
      sjsu::LogError("SD Card timed out. Aborting!");
      sd->response.length = SendCmd(Command::kGarbage,
                                    0xFFFFFFFF,
                                    sd->response.data.byte,
                                    KeepAlive::kNo);
      return false;
    }

    // After card is ready, acquire card capacity info using kGetOcr a
    // second time
    sjsu::LogDebug("Reading Card Capacity Information...");
    // Read CCS
    sd->response.length = SendCmd(
        Command::kGetOcr, 0x00, sd->response.data.byte, KeepAlive::kYes);
    if (sd->response.data.byte[1] & 0x40)
    {
      // The card is either high or extended capacity
      sjsu::LogDebug("SD Card is HC/XC");
      sd->type = Type::kSDHC;
    }
    else
    {
      // The card is standard capacity
      sjsu::LogDebug("SD Card is SC");
      sd->type = Type::kSDSC;
    }

    // Store OCR information
    for (int i = 0; i < 4; i++)
    {
      // ensure OCR doesn't capture the R1 section of the response
      sd->ocr.byte[i] = sd->response.data.byte[i + 1];
    }

    // If card is SDSC, manually set its block size
    if (sd->type == Type::kSDSC)
    {
      // Send requested byte size
      sd->response.length = SendCmd(Command::kChgBlkLen,
                                    static_cast<uint32_t>(kBlockSize),
                                    sd->response.data.byte,
                                    KeepAlive::kYes);
    }

    return true;
  }

  /// Waits for the card to respond after a single or multi block read cmd is
  /// sent.
  virtual void WaitToReadBlock()
  {
    // Since the command encountered no errors, we can now begin to
    // read data. The card will enter "BUSY" mode following reception
    // of the read command and sending of a response soon after; we
    // must wait until either the data token "OxFE" or the error token
    // "b000X_XXXX" is received. The error token's flags have the
    // following meanings:
    // MSB   -->  0 (irrelevant)
    // Bit 6 -->  0 (irrelevant)
    // Bit 5 -->  0 (irrelevant)
    // Bit 4 -->  If set, card is locked
    // Bit 3 -->  If set, address is out or range
    // Bit 2 -->  If set, card ECC failed
    // Bit 1 -->  If set, CC error occurred
    // Bit 0 -->  If set, a generic error occurred
    uint8_t wait_byte = 0x00;
    do
    {
      wait_byte = static_cast<uint8_t>(spi_.Transfer(0xFF));
    } while (wait_byte != 0xFE && (wait_byte & 0xE0) != 0x00);

    // DEBUG: Check the value of the wait byte
    if (wait_byte == 0xFE)
    {
      sjsu::LogDebug(
          "Received GO Byte 0xFE; SD Card is now sending block payload...");
    }
    else if ((wait_byte & 0xE0) == 0x00)
    {
      sjsu::LogDebug("Error: SD Card Rejected Read Cmd [Response: 0x%02X]",
                     wait_byte);
      sjsu::LogDebug("Card Locked?: %s", ToBool(wait_byte & 0x10));
      sjsu::LogDebug("Addr Out of Range?: %s", ToBool(wait_byte & 0x08));
      sjsu::LogDebug("Card ECC Failed?: %s", ToBool(wait_byte & 0x04));
      sjsu::LogDebug("CC Error?: %s", ToBool(wait_byte & 0x02));
      sjsu::LogDebug("Error?: %s", ToBool(wait_byte & 0x01));
    }
  }

  /// Waits for the card to be ready to receive a new block after one has
  /// been written or erased
  virtual void WaitWhileBusy()
  {
    // Wait for the card to finish programming (i.e. when the
    // bytes return to 0xFF)
    uint8_t busy_byte = 0x00;
    sjsu::LogDebug("Card is busy. Waiting for it to finish...");
    do
    {
      busy_byte = static_cast<uint8_t>(spi_.Transfer(0xFF));
    } while (busy_byte != 0xFF);
    sjsu::LogDebug("Card finished!");
  }

  /// Read any number of blocks from the SD card
  virtual uint8_t ReadBlock(uint32_t address,
                            uint8_t * array,
                            uint32_t blocks = 1)
  {
    sjsu::LogDebug("Block %" PRId32 " :: 0x%" PRIX32 " for %" PRId32 " blocks",
                   address,
                   address,
                   blocks);
    // Wait for a previous command to finish
    WaitWhileBusy();

    // Create a temporary storage location to store sd command responses
    Sd::CardInfo_t sd;
    bool payload_had_bad_crc = false;

    // Create a temporary place to store blocks of 512 bytes (this is now
    // possible because the constructor enforces block size cross-
    // compatibility)
    uint8_t block_store[512] = { 0 };

    // Determine appropriate command to send
    Command read_cmd;
    if (blocks > 1)
    {
      read_cmd = Command::kReadMulti;
    }
    else
    {
      read_cmd = Command::kReadSingle;
    }

    // Send initial read command
    sd.response.length =
        SendCmd(read_cmd, address, sd.response.data.byte, KeepAlive::kYes);
    sjsu::LogDebug("Sent Read Cmd");
    sjsu::LogDebug("[R1 Response:0x%02X]", sd.response.data.byte[0]);

    // Check if the command was acknowledged properly
    if (sd.response.data.byte[0] == 0x00)
    {
      // For every block requested
      uint16_t block_count;
      for (block_count = 0; block_count < blocks; block_count++)
      {
        // Wait for the card to respond with a ready signal
        WaitToReadBlock();

        // Calculate the block address offset
        uint16_t block_addr_offset =
            static_cast<uint16_t>(block_count * kBlockSize);

        // Read all the bytes of a single block
        for (uint16_t byte_count = 0; byte_count < kBlockSize; byte_count++)
        {
          // Calculate the current storage index
          uint16_t storage_index =
              static_cast<uint16_t>(block_addr_offset + byte_count);

          // Transfer a byte to read a block from the SD card
          array[storage_index] = static_cast<uint8_t>(spi_.Transfer(0xFF));

          // Copy that byte into our temporary block store
          block_store[byte_count] = array[storage_index];
        }

        // Then read the block's 16-bit CRC (i.e. read two bytes)
        uint16_t block_crc = static_cast<uint16_t>((spi_.Transfer(0xFF) << 8) |
                                                   spi_.Transfer(0xFF));

        // Run a CRC-16 calculation on the message to determine if the
        // received CRCs match (i.e. checks if the block data is
        // valid).
        uint16_t expected_block_crc = GetCrc16(block_store, 512);

        sjsu::LogDebug(
            "Block #%d @ 0x%" PRIX32 " acquired", block_count, address);
        sjsu::LogDebug("Expecting block crc16 '0x%04X'", expected_block_crc);
        sjsu::LogDebug("Got '0x%04X'", block_crc);
        if (expected_block_crc != block_crc)
        {
          // If they do not match, set the bad crc status
          payload_had_bad_crc = true;
          sjsu::LogError("While Reading Block #%d CRC16:", block_count);
          sjsu::LogError("Expected '0x%04X'", expected_block_crc);
          sjsu::LogError("Got '0x%04X'", block_crc);
        }
      }

      // After reading multiple blocks, send the stop command to terminate the
      // transaction (i.e. no keep-alive)
      if (blocks > 1)
      {
        sd.response.length = SendCmd(Command::kStopTrans,
                                     0xFFFFFFFF,
                                     sd.response.data.byte,
                                     KeepAlive::kNo);
      }

      // DEBUG: Print out the latest response byte
      sjsu::LogDebug("Response Byte");
      sjsu::LogDebug("0x%02X", sd.response.data.byte[0]);

      // If there was a bad crc from the payload, manually set the
      // CRC error flag in the command response byte
      if (payload_had_bad_crc)
      {
        sd.response.data.byte[0] |= 0x08;
      }
    }
    else
    {
      sjsu::LogError("Read Cmd was not acknowledged properly!");
      sjsu::LogError("Parameter Err: %s",
                     ToBool(sd.response.data.byte[0] & 0x40));
      sjsu::LogError("Addr Err: %s", ToBool(sd.response.data.byte[0] & 0x20));
      sjsu::LogError("Erase Seq Err: %s",
                     ToBool(sd.response.data.byte[0] & 0x10));
      sjsu::LogError("Com CRC Err: %s",
                     ToBool(sd.response.data.byte[0] & 0x08));
      sjsu::LogError("Illegal Cmd Err: %s",
                     ToBool(sd.response.data.byte[0] & 0x04));
      sjsu::LogError("Erase Reset: %s",
                     ToBool(sd.response.data.byte[0] & 0x02));
      sjsu::LogError("In Idle: %s", ToBool(sd.response.data.byte[0] & 0x01));
    }

    // Wait for the SD card to go out of idle state
    sjsu::LogDebug("Now waiting for SD Card to exit Idle Mode...");
    do
    {
      // Query the status register
      sd.response.length = SendCmd(
          Command::kGetStatus, 32, sd.response.data.byte, KeepAlive::kNo);
    } while (sd.response.data.byte[0] & 0x01);
    sjsu::LogDebug("SD Card is out of Idle Mode!");

    // Return the status
    sjsu::LogDebug("Read Complete! [R1 Response: 0x%02X]",
                   sd.response.data.byte[0]);
    return sd.response.data.byte[0];
  }

  /// Writes any number of 512-byte blocks to the SD Card
  virtual uint8_t WriteBlock(uint32_t address,
                             const uint8_t * array,
                             uint32_t blocks = 1)
  {
    // Wait for a previous command to finish
    WaitWhileBusy();

    // Create a temporary storage location to store sd command responses
    Sd::CardInfo_t sd;

    // Determine appropriate command and start token to send
    Command write_cmd;
    uint8_t write_start_tkn;
    if (blocks > 1)
    {
      write_cmd       = Command::kWriteMulti;
      write_start_tkn = 0xFC;
    }
    else
    {
      write_cmd       = Command::kWriteSingle;
      write_start_tkn = 0xFE;
    }

    // Send initial write command
    sd.response.length =
        SendCmd(write_cmd, address, sd.response.data.byte, KeepAlive::kYes);
    sjsu::LogDebug("Sent Write Cmd");
    sjsu::LogDebug("[R1 Response:0x%02X]", sd.response.data.byte[0]);

    // Check if the response was acknowledged properly
    if (sd.response.data.byte[0] == 0x00)
    {
      // If no errors, proceed write given block(s) to SD Card
      // For each given block
      for (uint16_t current_block_num = 0; current_block_num < blocks;
           current_block_num++)
      {
        // Calculate array offset
        uint64_t arr_offset = current_block_num * kBlockSize;

        // Send the start token for the current block
        spi_.Transfer(write_start_tkn);

        // Write all 512-bytes of the given block
        sjsu::LogDebug("Writing block #%d", current_block_num);
        for (uint16_t current_byte = 0; current_byte < kBlockSize;
             current_byte++)
        {
          spi_.Transfer(array[arr_offset + current_byte]);
        }

        // Read the data response token after writing the block
        uint8_t data_response_tkn = static_cast<uint8_t>(spi_.Transfer(0xFF));
        sjsu::LogDebug("Response Byte");
        sjsu::LogDebug("[Data Response Token: 0x%02X]", data_response_tkn);
        sjsu::LogDebug("Data Accepted?: %s", ToBool(data_response_tkn & 0x05));
        sjsu::LogDebug("Data Rejected (bad crc)?: %s",
                       ToBool(data_response_tkn & 0x0B));
        sjsu::LogDebug("Data Rejected (write err)?: %s",
                       ToBool(data_response_tkn & 0x0D));

        // If writing multiple blocks and the previous block was rejected
        if (blocks > 1 && !(data_response_tkn & 0x05))
        {
          // Send an immediate stop (CMD12)
          sd.response.length = SendCmd(Command::kStopTrans,
                                       0xFFFFFFFF,
                                       sd.response.data.byte,
                                       KeepAlive::kYes);
          sjsu::LogDebug("Stopped Transmission due to rejection...");
          sjsu::LogDebug("[R1 Response: 0x%02X]", sd.response.data.byte[0]);

          // In the case of a write error, ask for the reason why
          if (data_response_tkn & 0x0D)
          {
            sd.response.length = SendCmd(Command::kGetStatus,
                                         0xFFFFFFFF,
                                         sd.response.data.byte,
                                         KeepAlive::kYes);
            sjsu::LogDebug(
                "Checking Status Register to see cause of Write Error...");
            sjsu::LogDebug("[R2 Response: 0x%04" PRIX32 "]",
                           sd.response.data.dWord.lo);
          }
        }
        WaitWhileBusy();
      }

      if (blocks > 1)
      {
        constexpr uint8_t kStopToken = 0xFD;
        spi_.Transfer(kStopToken);

        // Wait for the card's programming to complete before
        // reselecting it (i.e. to prevent corruption)
        WaitWhileBusy();
      }
    }
    else
    {
      sjsu::LogDebug("Error: Write Cmd was not acknowledged properly!");
      sjsu::LogDebug("Parameter Err: %s",
                     ToBool(sd.response.data.byte[0] & 0x40));
      sjsu::LogDebug("Addr Err: %s", ToBool(sd.response.data.byte[0] & 0x20));
      sjsu::LogDebug("Erase Seq Err: %s",
                     ToBool(sd.response.data.byte[0] & 0x10));
      sjsu::LogDebug("Com CRC Err: %s",
                     ToBool(sd.response.data.byte[0] & 0x08));
      sjsu::LogDebug("Illegal Cmd Err: %s",
                     ToBool(sd.response.data.byte[0] & 0x04));
      sjsu::LogDebug("Erase Reset: %s",
                     ToBool(sd.response.data.byte[0] & 0x02));
      sjsu::LogDebug("In Idle: %s", ToBool(sd.response.data.byte[0] & 0x01));
    }

    return sd.response.data.byte[0];
  }

  /// Deletes any number of blocks (inclusively) within a range of address.
  virtual uint8_t DeleteBlock(uint32_t start, uint32_t end)
  {
    // Wait for a previous command to finish
    WaitWhileBusy();

    // Create a temporary storage location to store sd command responses
    Sd::CardInfo_t sd;
    bool delete_failed = false;

    // Set the delete start address
    sjsu::LogDebug("Setting Delete Start Address...");
    sd.response.length = SendCmd(
        Command::kDelFrom, start, sd.response.data.byte, KeepAlive::kYes);

    // Wait while the writing the start address
    WaitWhileBusy();

    // Force return if an error occurred
    if (sd.response.data.byte[0] != 0x00)
    {
      sjsu::LogError("Failed to set Start Address!");
      delete_failed = true;
    }

    // Set the delete end address
    if (!delete_failed)
    {
      sjsu::LogDebug("Setting Delete End Address...");
      sd.response.length =
          SendCmd(Command::kDelTo, end, sd.response.data.byte, KeepAlive::kYes);
    }

    // Wait while the writing the end address
    WaitWhileBusy();

    // Force return if an error occurred
    if (sd.response.data.byte[0] != 0x00)
    {
      sjsu::LogError("Failed to set End Address!");
      delete_failed = true;
    }

    // Finally, attempt a delete
    if (!delete_failed)
    {
      // Issue the delete command to delete from our from:to range
      sjsu::LogDebug("Issuing Delete Command...");
      sd.response.length = SendCmd(
          Command::kDel, 0xFFFFFFFF, sd.response.data.byte, KeepAlive::kYes);

      // Wait while the deletion occurs
      WaitWhileBusy();

      // Check response
      sjsu::LogDebug("[R1 Response: 0x%02X]", sd.response.data.byte[0]);
      sjsu::LogDebug("Deletion Complete...");
    }

    // Return status
    return sd.response.data.byte[0];
  }

  /// Send a SD Card command
  ///
  /// @param sdc - the type of SD card command
  /// @param arg - value to send with the command
  /// @param response_buffer - buffer to contain the response returned.
  /// @param keep_alive - set to true in order to keep the connection with SD
  ///                     card alive.
  /// @return uint32_t response length
  virtual uint32_t SendCmd(Command sdc,
                           uint32_t arg,
                           uint8_t response_buffer[],
                           KeepAlive keep_alive)
  {
    ResponseType res_type;
    uint8_t res_len    = 0;
    uint8_t crc        = 0;
    uint8_t tries      = 0;
    uint8_t bit_offset = 0;  // determines the distance of the response's
                             // 0 bit from the MSB place
    uint8_t temp_byte = 0;

    // Determine the response type of the set command
    switch (sdc)
    {
      case Command::kGarbage: res_type = ResponseType::kR1; break;
      case Command::kReset: res_type = ResponseType::kR1; break;
      case Command::kInit: res_type = ResponseType::kR1; break;
      case Command::kGetOp: res_type = ResponseType::kR7; break;
      case Command::kStopTrans: res_type = ResponseType::kR1; break;
      case Command::kGetStatus: res_type = ResponseType::kR2; break;
      case Command::kAcBegin: res_type = ResponseType::kR1; break;
      case Command::kAcInit: res_type = ResponseType::kR1; break;
      case Command::kGetOcr: res_type = ResponseType::kR3; break;
      case Command::kChgBlkLen: res_type = ResponseType::kR1; break;
      case Command::kReadSingle: res_type = ResponseType::kR1; break;
      case Command::kReadMulti: res_type = ResponseType::kR1; break;
      case Command::kWriteSingle: res_type = ResponseType::kR1; break;
      case Command::kWriteMulti: res_type = ResponseType::kR1; break;
      case Command::kDelFrom: res_type = ResponseType::kR1; break;
      case Command::kDelTo: res_type = ResponseType::kR1; break;
      case Command::kDel: res_type = ResponseType::kR1b; break;
      default:
        sjsu::LogError("Unknown response type. Aborting!");
        return -1;
        break;
    }

    // Calculate the 7-bit CRC (i.e. CRC7) using the SD card standard's
    // algorithm
    uint8_t msg[5] = { static_cast<uint8_t>(sdc),
                       static_cast<uint8_t>(arg >> 24),
                       static_cast<uint8_t>(arg >> 16),
                       static_cast<uint8_t>(arg >> 8),
                       static_cast<uint8_t>(arg >> 0) };

    crc = GetCrc7(msg, sizeof(msg));
    if (sdc == Command::kGarbage)
    {
      crc = 0xFF;
    }

    // Select the SD Card
    chip_select_.Set(Gpio::State::kLow);

    // Send the desired command frame to the SD card board
    // Begin by transfering the command byte
    spi_.Transfer(static_cast<uint16_t>(sdc));
    // Send arg byte [31:24]
    spi_.Transfer(static_cast<uint16_t>(arg >> 24) & 0xFF);
    // Send arg byte [23:16]
    spi_.Transfer(static_cast<uint16_t>(arg >> 16) & 0xFF);
    // Send arg byte [15:8]
    spi_.Transfer(static_cast<uint16_t>(arg >> 8) & 0xFF);
    // Send arg byte [7:0]
    spi_.Transfer(static_cast<uint16_t>(arg >> 0) & 0xFF);
    // Send 7-bit CRC and LSB stop addr (as b1)
    spi_.Transfer(static_cast<uint16_t>(crc << 1) | 0x01);

    // Write garbage while waiting for a response
    // Send at least 1 byte of garbage before checking for a response
    temp_byte = static_cast<uint8_t>(spi_.Transfer(0xFF));
    while (tries++ < kBusTimeout)
    {
      temp_byte = static_cast<uint8_t>(spi_.Transfer(0xFF));
      if (temp_byte != 0xFF)
      {
        // Determine the offset, since the first byte of a
        // response will always be 0.
        while (temp_byte & (0x80 >> bit_offset))
        {
          bit_offset++;
        }
        break;
      }
      tries++;
    }

    // Determine response length (in bytes) based on response type
    switch (res_type)
    {
      case ResponseType::kR1: res_len = 1; break;
      case ResponseType::kR1b: res_len = 1; break;
      case ResponseType::kR2: res_len = 2; break;
      case ResponseType::kR3: res_len = 5; break;
      case ResponseType::kR7: res_len = 5; break;
      default:
        sjsu::LogError("Response unsupported in SPI mode. Aborting!");
        return -1;
        break;
    }

    // Acquire the response
    uint64_t temp_response = 0;
    // Read an extra 8 bits since the response was offset
    uint8_t bytes_to_read =
        static_cast<uint8_t>((bit_offset > 0) ? res_len + 1 : res_len);
    while (bytes_to_read-- > 0)
    {
      // Make space for the next byte
      temp_response = temp_response << 8;
      temp_response |= temp_byte;
      temp_byte = static_cast<uint8_t>(spi_.Transfer(0xFF));
    }
    // Compensate for the bit offset
    temp_response = temp_response >> bit_offset;

    // Only write to the response buffer if it is provided
    if (response_buffer != nullptr)
    {
      for (int i = 0; i < res_len; i++)
      {
        response_buffer[i] =
            static_cast<uint8_t>(temp_response >> 8 * (res_len - 1 - i));
      }
    }

    // Only end the transaction if keep_alive isn't requested
    if (keep_alive == KeepAlive::kNo)
    {
      // Deselect the SPI comm board
      chip_select_.Set(Gpio::State::kHigh);
    }
    return res_len;
  }

 private:
  /// Adds a message byte to the current CRC-7 to get a the new CRC-7
  uint8_t Crc7Add(uint8_t crc, uint8_t message_byte)
  {
    return kCrcTable8.crc_table[(crc << 1) ^ message_byte];
  }

  /// Returns the CRC-7 for a message of "length" bytes
  uint8_t GetCrc7(uint8_t * message, uint8_t length)
  {
    uint8_t crc = 0;
    for (int i = 0; i < length; i++)
    {
      crc = Crc7Add(crc, message[i]);
    }
    return crc;
  }

  /// Returns CCITT CRC-16 for a message of "length" bytes
  uint16_t GetCrc16(uint8_t * message, uint16_t length)
  {
    uint64_t crc = 0x0000;
    uint64_t temp;
    uint64_t final_value = 0;

    for (uint64_t count = 0; count < length; ++count)
    {
      temp = (*message++ ^ (crc >> 8)) & 0xff;
      crc  = kCrcTable16.crc_table[temp] ^ (crc << 8);
    }

    return static_cast<uint16_t>(crc ^ final_value);
  }

  /// Returns string to represent a boolean value
  ///
  /// @param condition - bool to be converted into a string
  /// @return const char* - string representation of the bool.
  const char * ToBool(bool condition)
  {
    return (condition) ? "true" : "false";
  }

  /// the object reference to use when using SPI/SPI
  const Spi & spi_;
  /// the object reference to use when using CS (GPIO)
  const Gpio & chip_select_;
};
}  // namespace sjsu
