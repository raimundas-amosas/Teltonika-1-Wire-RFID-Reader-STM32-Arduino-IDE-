# Teltonika 1-Wire RFID Reader → STM32 (Arduino IDE)

## Overview

This repository documents a **safe, deterministic, and production-ready integration of a Teltonika 1-Wire RFID reader with an STM32 Nucleo board**, implemented using Arduino IDE (STM32duino).

The project focuses on:
- understanding real-world behavior of Teltonika 1-Wire RFID readers,
- handling automotive voltage domains safely,
- implementing a correct 1-Wire master (open-drain discipline),
- extracting the **actual RFID card UID** without undocumented assumptions.

This is not a generic tutorial.  
It is an engineering write-up based on **measurement, debugging, and protocol analysis**.

---

## Background / Motivation

Teltonika 1-Wire RFID readers are designed primarily for telematics and vehicle tracking devices.  
The official documentation states that the reader:
- uses the 1-Wire protocol,
- emulates an iButton device,
- supports `Read ROM (0x33)` and `Search ROM (0xF0)`.

What is **not explicitly documented**:
- whether the ROM represents the reader or the card,
- when the reader joins the 1-Wire bus,
- how presence behaves when a card is removed,
- what voltage level is actually present on the DATA line.

This project answers those questions through **controlled experiments** instead of speculation.

---

## Hardware Used

- **Teltonika 1-Wire RFID Reader**
  - Supply voltage: 6.5–30 V (tested with 12 V)
  - 1-Wire DATA interface (iButton emulation)
- **STM32 Nucleo-64** (MB1136, STM32 family MCU)
- **BSS138 bidirectional logic level shifter**
- Passive components:
  - 1 kΩ series resistor on DATA line
- Development environment:
  - Arduino IDE
  - STM32duino core

---

## Why a Level Shifter Is Required

Although the RFID reader is powered from **12 V**, direct measurement showed that:
- the 1-Wire DATA line operates at approximately **5 V**,
- it never rises to 12 V.

However:
- STM32 GPIOs are **3.3 V tolerant only**,
- 1-Wire is an **open-drain bus**,
- driving HIGH is forbidden.

The chosen solution:
- **BSS138 bidirectional level shifter**
- HV = 5 V
- LV = 3.3 V
- common GND

This ensures:
- voltage domain separation,
- correct open-drain behavior,
- no current injection into STM32 protection diodes.

---

## Electrical Connection

### Power Connections

- RFID Brown → +12 V
- RFID Green → GND
- STM32 GND → common ground
- Level shifter:
  - HV → STM32 5V
  - LV → STM32 3V3
  - GND → common ground

### Data Path

```
RFID DATA (White)
        |
      [1 kΩ]
        |
     HV1 (level shifter)
        |
     BSS138 MOSFET
        |
     LV1
        |
   STM32 D2 (PA10)
```

### Critical Electrical Rules

- STM32 GPIO **never drives HIGH**
- HIGH state = GPIO configured as INPUT (Hi-Z)
- LOW state = GPIO OUTPUT LOW
- HV must **never** be connected to 12 V
- All grounds must be common

---

## Architecture Overview

The system is intentionally split into three layers:

### 1. Power & Signal Safety Layer
- Handles voltage domain separation
- Protects MCU GPIOs
- Ensures correct electrical behavior

### 2. Protocol Layer (1-Wire Master)
- Bit-banged implementation
- Strict open-drain discipline
- Timing validated against real hardware

### 3. Application Layer
- Card presence detection
- UID change detection
- Human-readable ID extraction

### Data Flow

RFID Card  
→ Teltonika Reader (iButton emulation)  
→ 1-Wire bus  
→ Level Shifter (5 V → 3.3 V)  
→ STM32 GPIO  
→ UID processing  
→ Serial output / application logic

---

## Software Environment

- Arduino IDE
- STM32duino core
- No external 1-Wire libraries
- Fully custom bit-banged 1-Wire master

GPIO assignment:
- **D2 (PA10)** → 1-Wire DATA

---

## Testing Strategy

### Electrical Validation
- Verified:
  - HV ≈ 5.0 V
  - LV ≈ 3.3 V
- Confirmed DATA never reaches 12 V

### GPIO Health Verification
A previously used GPIO pin was tested to ensure no damage:
- INPUT_PULLUP / INPUT_PULLDOWN tests passed
- Floating ADC readings behaved normally
- Conclusion: GPIO intact

### 1-Wire Functional Testing
- No card present:
  - No presence pulse
- Card presented:
  - Presence detected
  - Read ROM succeeds
- Card removed:
  - Presence disappears

This confirmed that the reader **only joins the 1-Wire bus while a card is present**.

---

## Key Finding

**The Teltonika reader exposes the actual RFID card UID via the 1-Wire ROM.**

This was confirmed by:
- testing multiple different cards,
- observing ROM changes,
- validating CRC on every read.

Example:

```
Card A:
01 A1 4E 4F 00 38 00 3C

Card B:
01 AF 9A 4A 00 38 00 EF
```

Therefore:
- `Read ROM (0x33)` is sufficient,
- no additional memory commands are required.

---

## Card Number Interpretation

Printed numbers on 125 kHz RFID cards are typically decimal representations.

Testing showed that:
- the printed card number corresponds to **ROM bytes 1..4 interpreted as little-endian (`DEC32_LE`)**.

The code intentionally keeps multiple representations available:
- HEX ROM
- DEC32 little-endian
- DEC32 big-endian
- DEC40 variants

This allows future support for different card formats without hardware changes.

---

## Challenges & Solutions

### Unknown Reader Behavior
Solved by:
- systematic ROM and endian analysis,
- CRC validation,
- multi-card comparison.

### Automotive Voltage Environment
Solved by:
- series resistor,
- level shifter,
- strict open-drain discipline.

### Initial Presence Instability
Solved by:
- reducing redundant bus resets,
- implementing event-based state handling.

---

## Result

The final solution is:
- electrically safe,
- protocol-correct,
- deterministic,
- suitable for production embedded systems.

---

## Commit History Draft

Suggested commit progression for this repository:

```
commit 1: initial hardware investigation
- reader power requirements
- 1-Wire DATA voltage measurements
- confirmation of ~5 V signaling

commit 2: safe GPIO interface design
- added BSS138 level shifter
- introduced series resistor on DATA line
- defined open-drain rules

commit 3: basic 1-Wire bit-banging
- reset and presence detection
- Read ROM (0x33) implementation
- CRC8 validation

commit 4: stability improvements
- reduced redundant bus resets
- improved timing margins
- eliminated false no-presence events

commit 5: card presence state machine
- CARD_PRESENT / CARD_REMOVED events
- ROM change detection

commit 6: card ID analysis
- endian experiments
- decimal ID correlation
- support for multiple interpretations

commit 7: documentation
- wiring explanation
- electrical reasoning
- protocol behavior documentation
```

---

## License

MIT License. See LICENSE file for details.
