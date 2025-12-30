#include <Arduino.h>

/* 1-Wire pin: Arduino D2 = PA10 on many Nucleo-64 variants */
static const uint8_t ONEWIRE_PIN = D2;

/* Timing (standard speed 1-Wire) */
static inline void delay_us(uint32_t us) { delayMicroseconds(us); }

/* Safety rule:
   - LOW: drive low (OUTPUT, LOW)
   - HIGH: release line (INPUT)  -> never drive high
*/
static inline void ow_drive_low() {
  pinMode(ONEWIRE_PIN, OUTPUT);
  digitalWrite(ONEWIRE_PIN, LOW);
}

static inline void ow_release() {
  pinMode(ONEWIRE_PIN, INPUT);   // Hi-Z, external pull-ups on shifter will pull it up
}

static inline uint8_t ow_read_pin() {
  return (digitalRead(ONEWIRE_PIN) == HIGH) ? 1 : 0;
}

/* Reset + presence detect */
static bool ow_reset_presence() {
  ow_drive_low();
  delay_us(480);

  ow_release();
  delay_us(70);

  bool presence = (ow_read_pin() == 0);  // device pulls low if present

  delay_us(410); // finish reset window
  return presence;
}

static void ow_write_bit(uint8_t bit) {
  if (bit) {
    // write 1: low 6us, release for rest of slot
    ow_drive_low();
    delay_us(6);
    ow_release();
    delay_us(64);
  } else {
    // write 0: low 60us, release
    ow_drive_low();
    delay_us(60);
    ow_release();
    delay_us(10);
  }
}

static uint8_t ow_read_bit() {
  uint8_t bit;
  ow_drive_low();
  delay_us(6);
  ow_release();
  delay_us(9);
  bit = ow_read_pin();
  delay_us(55);
  return bit;
}

static void ow_write_byte(uint8_t b) {
  for (int i = 0; i < 8; i++) {
    ow_write_bit((b >> i) & 0x01);
  }
}

static uint8_t ow_read_byte() {
  uint8_t b = 0;
  for (int i = 0; i < 8; i++) {
    b |= (ow_read_bit() << i);
  }
  return b;
}

/* Dallas/Maxim CRC8 */
static uint8_t ow_crc8(const uint8_t *data, uint8_t len) {
  uint8_t crc = 0;
  for (uint8_t i = 0; i < len; i++) {
    uint8_t inbyte = data[i];
    for (uint8_t j = 0; j < 8; j++) {
      uint8_t mix = (crc ^ inbyte) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      inbyte >>= 1;
    }
  }
  return crc;
}

static void print_rom(const uint8_t rom[8]) {
  for (int i = 0; i < 8; i++) {
    if (rom[i] < 16) Serial.print('0');
    Serial.print(rom[i], HEX);
    if (i != 7) Serial.print(' ');
  }
}

/* Search ROM (0xF0) - returns number of ROMs found (up to max_roms) */
static int ow_search_rom(uint8_t (*roms)[8], int max_roms) {
  int found = 0;

  uint8_t last_discrepancy = 0;
  uint8_t last_device_flag = 0;

  while (!last_device_flag && found < max_roms) {
    uint8_t rom[8] = {0};

    uint8_t id_bit_number = 1;
    uint8_t last_zero = 0;
    uint8_t rom_byte_number = 0;
    uint8_t rom_byte_mask = 1;

    if (!ow_reset_presence()) {
      return found;
    }

    ow_write_byte(0xF0); // Search ROM

    do {
      uint8_t id_bit = ow_read_bit();
      uint8_t cmp_id_bit = ow_read_bit();

      if (id_bit == 1 && cmp_id_bit == 1) {
        // no devices
        break;
      }

      uint8_t search_direction;
      if (id_bit != cmp_id_bit) {
        search_direction = id_bit;
      } else {
        if (id_bit_number < last_discrepancy) {
          search_direction = ((rom[rom_byte_number] & rom_byte_mask) > 0);
        } else {
          search_direction = (id_bit_number == last_discrepancy);
        }
        if (search_direction == 0) last_zero = id_bit_number;
      }

      if (search_direction == 1) rom[rom_byte_number] |= rom_byte_mask;
      else rom[rom_byte_number] &= (uint8_t)~rom_byte_mask;

      ow_write_bit(search_direction);

      id_bit_number++;
      rom_byte_mask <<= 1;
      if (rom_byte_mask == 0) {
        rom_byte_number++;
        rom_byte_mask = 1;
      }
    } while (rom_byte_number < 8);

    last_discrepancy = last_zero;
    if (last_discrepancy == 0) last_device_flag = 1;

    uint8_t crc = ow_crc8(rom, 7);
    if (crc == rom[7]) {
      memcpy(roms[found], rom, 8);
      found++;
    } else {
      Serial.println("SEARCH: CRC FAIL (timing/noise?)");
    }
  }

  return found;
}

/* Read ROM (0x33) - only valid when exactly one device is on the bus */
static bool ow_read_rom(uint8_t rom[8]) {
  if (!ow_reset_presence()) return false;
  ow_write_byte(0x33);
  for (int i = 0; i < 8; i++) rom[i] = ow_read_byte();
  return true;
}

static bool rom_equal(const uint8_t a[8], const uint8_t b[8]) {
  for (int i = 0; i < 8; i++) if (a[i] != b[i]) return false;
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(400);

  Serial.println("BOOT: 1-Wire RFID via BSS138 shifter");
  Serial.println("BOOT: D2 (PA10) is 1-Wire line");
  Serial.println("SAFETY: line HIGH is INPUT (Hi-Z), never drive HIGH");
  ow_release();

  // Initial discovery
  uint8_t roms[4][8];
  int n = ow_search_rom(roms, 4);

  Serial.print("SEARCH: found ");
  Serial.print(n);
  Serial.println(" device(s)");

  for (int i = 0; i < n; i++) {
    Serial.print("ROM[");
    Serial.print(i);
    Serial.print("]: ");
    print_rom(roms[i]);
    Serial.println();
  }

  Serial.println("LOOP: present? ReadROM + Search on changes");
}

void loop() {
  static bool cardPresent = false;
  static uint8_t lastRom[8] = {0};
  static bool lastRomValid = false;

  bool presence = ow_reset_presence();

  if (!presence) {
    if (cardPresent) {
      cardPresent = false;
      Serial.println("EVENT: CARD_REMOVED");
    }
    delay(50);
    return;
  }

  if (!cardPresent) {
    cardPresent = true;
    Serial.println("EVENT: CARD_PRESENT");
  }

  // Read ROM without extra reset
  ow_write_byte(0x33);
  uint8_t rom[8];
  for (int i = 0; i < 8; i++) rom[i] = ow_read_byte();

  uint8_t crc = ow_crc8(rom, 7);
  if (crc != rom[7]) {
    Serial.print("READROM: CRC BAD calc=");
    Serial.println(crc, HEX);
    delay(50);
    return;
  }

  bool changed = !lastRomValid;
  if (lastRomValid) {
    for (int i = 0; i < 8; i++) {
      if (rom[i] != lastRom[i]) { changed = true; break; }
    }
  }

  if (changed) {
    memcpy(lastRom, rom, 8);
    lastRomValid = true;

    // Print full ROM hex (8 bytes)
    Serial.print("EVENT: CARD_UID_HEX=");
    for (int i = 0; i < 8; i++) {
      if (rom[i] < 16) Serial.print('0');
      Serial.print(rom[i], HEX);
    }
    Serial.println();

    // Print middle 6 bytes (often the "payload" without family+crc)
    Serial.print("DEBUG: CARD_ID_HEX6=");
    for (int i = 1; i <= 6; i++) {
      if (rom[i] < 16) Serial.print('0');
      Serial.print(rom[i], HEX);
    }
    Serial.println();

    // 32-bit interpretations from bytes 1..4
    uint32_t be32 = ((uint32_t)rom[1] << 24) | ((uint32_t)rom[2] << 16) | ((uint32_t)rom[3] << 8) | (uint32_t)rom[4];
    uint32_t le32 = ((uint32_t)rom[4] << 24) | ((uint32_t)rom[3] << 16) | ((uint32_t)rom[2] << 8) | (uint32_t)rom[1];

    Serial.print("DEBUG: CARD_ID_DEC32_BE=");
    Serial.println(be32);
    Serial.print("DEBUG: CARD_ID_DEC32_LE=");
    Serial.println(le32);

    // 40-bit interpretations from bytes 1..5
    uint64_t be40 = ((uint64_t)rom[1] << 32) | ((uint64_t)rom[2] << 24) | ((uint64_t)rom[3] << 16) | ((uint64_t)rom[4] << 8) | (uint64_t)rom[5];
    uint64_t le40 = ((uint64_t)rom[5] << 32) | ((uint64_t)rom[4] << 24) | ((uint64_t)rom[3] << 16) | ((uint64_t)rom[2] << 8) | (uint64_t)rom[1];

    Serial.print("DEBUG: CARD_ID_DEC40_BE=");
    Serial.println((unsigned long long)be40);
    Serial.print("DEBUG: CARD_ID_DEC40_LE=");
    Serial.println((unsigned long long)le40);

    // Also show "facility,card" style guess from 40-bit: facility=upper 8 bits, card=lower 16 bits (very rough)
    uint8_t facility_guess = rom[1]; // rough
    uint16_t card_guess = ((uint16_t)rom[2] << 8) | rom[3]; // rough

    Serial.print("DEBUG: GUESS_FACILITY=");
    Serial.print(facility_guess);
    Serial.print(" GUESS_CARD=");
    Serial.println(card_guess);
  }

  delay(100);
}
