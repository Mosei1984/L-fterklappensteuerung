# Firmware Release Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Pico firmware for the 80-mm fan-flap controller
release-ready by adding deterministic fault diagnostics, safer startup and
motion supervision, robust persisted settings, hardware acceptance checks and a
repeatable final release gate.

**Architecture:** Keep the controller logic MCU-independent in
`lib/Luefterklappe/src/`. Append diagnostics to the existing Modbus register
map instead of changing existing register numbers. Keep the Arduino/Pico HAL in
`src/main.cpp` thin: pins, UARTs, flash, watchdog, status LED and service-port
plumbing only.

**Tech Stack:** PlatformIO, Arduino Mbed RP2040, AccelStepper, TMC2209 UART,
Unity native tests, Modbus RTU, sigrok-cli LA workflow, PowerShell release
scripts.

**Research Input:** See
`docs/superpowers/research/2026-05-23-firmware-release-hardening-research.md`.
The release plan incorporates four independent research tracks: RP2040
watchdog/FlashIAP behavior, TMC2209 UART/StallGuard diagnostics, Modbus RTU and
Loxone integration rules, and residential-ventilation product-safety wording.

---

## Current Baseline

- Firmware core already has homing, soft endstops, safe-position persistence,
  Modbus RTU, text service commands, TMC2209 UART, StallGuard fallback,
  fault recovery via `REFRESH` and 45 native tests.
- Current hard gate is `.\tools\run_hard_checks.ps1`.
- Existing release-critical gaps are:
  - fault state has no explicit fault reason or fault counter;
  - TMC UART communication failure is indistinguishable from "not stalled";
  - raw endstop reads have no shared debounce/filter unit;
  - normal moves do not have a no-progress timeout independent of StallGuard;
  - flash settings are single-record; release firmware must use two physical
    flash sectors before claiming power-loss-safe settings commits;
  - boot/watchdog reason is not exposed;
  - no firmware-specific factory/self-test mode or acceptance report exists;
  - product documentation does not yet clearly exclude fire, smoke, CO,
    combustion-appliance and life-safety claims.

## File Map

- Modify `lib/Luefterklappe/src/FanFlapController.h`: add fault reason API,
  diagnostic counters, motion-supervision config fields and external-fault
  reporting.
- Modify `lib/Luefterklappe/src/FanFlapController.cpp`: record exact fault
  causes, supervise no-progress movement, expose `DIAG?` and `FAULT?`.
- Create `lib/Luefterklappe/src/FaultReason.h`: stable public enum values for
  Modbus, text diagnostics, tests and documentation.
- Create `lib/Luefterklappe/src/InputDebouncer.h`.
- Create `lib/Luefterklappe/src/InputDebouncer.cpp`: MCU-independent input
  debounce and both-endstop sanity filtering.
- Modify `lib/Luefterklappe/src/ModbusRtuServer.h`: append diagnostic
  registers after `SafePositionPermille = 16U`.
- Modify `lib/Luefterklappe/src/ModbusRtuServer.cpp`: implement read-only
  diagnostic registers and reject writes to them.
- Modify `lib/Luefterklappe/src/PersistentSettings.h`: add load/save status,
  generation and dual-sector storage contract.
- Modify `lib/Luefterklappe/src/PersistentSettings.cpp`: implement two-sector
  journaled settings with CRC and highest-generation selection.
- Modify `lib/Luefterklappe/src/Tmc2209Driver.h`: add explicit TMC poll result
  enum and communication verification API.
- Modify `lib/Luefterklappe/src/Tmc2209Driver.cpp`: return communication
  errors separately from no-stall status.
- Modify `src/main.cpp`: safe output init, input debouncer, watchdog service,
  TMC health mapping, boot reason and service self-test wiring.
- Modify `test/test_controller/test_main.cpp`: add release-hardening tests
  beside existing controller, Modbus, persistence and TMC tests.
- Create `tools/firmware_release_check.ps1`: release acceptance runner with
  no-hardware and full-hardware modes.
- Modify `README.md`, `test/README`, `tools/la/README.md` and
  `docs/diagrams/architecture.mmd`: document new diagnostics and final
  inspection path.

## Stable Diagnostic Register Additions

Existing registers `0..16` stay unchanged.

| Register | Access | Meaning |
| --- | --- | --- |
| 17 | R | Last fault reason, see `FaultReason` enum |
| 18 | R | Fault counter, saturating `0..65535` |
| 19 | R | Last settings status: `0` unknown, `1` default, `2` loaded, `3` saved, `4` save failed |
| 20 | R | TMC health: `0` unknown, `1` ok, `2` communication error, `3` disabled by build |
| 21 | R | Boot reason: `0` unknown, `1` power-on, `2` watchdog, `3` software reset |
| 22 | R | Firmware protocol version, start with `2` |

## Task 1: Fault Reason Telemetry

**Files:**

- Create: `lib/Luefterklappe/src/FaultReason.h`
- Modify: `lib/Luefterklappe/src/FanFlapController.h`
- Modify: `lib/Luefterklappe/src/FanFlapController.cpp`
- Test: `test/test_controller/test_main.cpp`

- [x] **Step 1: Add failing controller tests**

Add these tests near the existing fault tests:

```cpp
void test_fault_reason_records_homing_min_timeout() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  controller.tick(kNoInputs, 0U);
  stepper.position_ = -1000;
  controller.tick(kNoInputs, 1U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_EQUAL(
      static_cast<std::uint16_t>(FaultReason::HomingMinTravelExceeded),
      static_cast<std::uint16_t>(controller.lastFaultReason()));
  TEST_ASSERT_EQUAL_UINT16(1U, controller.faultCount());
}

void test_fault_reason_records_unexpected_switch_direction() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;
  controller.tick(DigitalInputs{true, false, false}, 20U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_EQUAL(FaultReason::UnexpectedMinSwitch,
                    controller.lastFaultReason());
  TEST_ASSERT_EQUAL_UINT16(1U, controller.faultCount());
}
```

Register both tests in `main()`.

- [x] **Step 2: Run and confirm failure**

Run:

```powershell
platformio test -e native
```

Expected: compile failure because `FaultReason`, `lastFaultReason()` and
`faultCount()` do not exist.

- [x] **Step 3: Add the public fault enum**

Create `lib/Luefterklappe/src/FaultReason.h`:

```cpp
#ifndef LUEFTERKLAPPE_FAULT_REASON_H
#define LUEFTERKLAPPE_FAULT_REASON_H

#include <cstdint>

namespace luefterklappe {

enum class FaultReason : std::uint16_t {
  None = 0U,
  HomingRangeInvalid = 1U,
  HomingMinTravelExceeded = 2U,
  HomingMaxTravelExceeded = 3U,
  UnexpectedMinSwitch = 4U,
  UnexpectedMaxSwitch = 5U,
  StallGuardDuringMove = 6U,
  ValveBlockedDuringFreeCheck = 7U,
  MotionNoProgress = 8U,
  TmcCommunicationLost = 9U,
  SettingsWriteFailed = 10U,
  BothEndstopsActiveAtBoot = 11U,
  WatchdogRestart = 12U
};

}  // namespace luefterklappe

#endif  // LUEFTERKLAPPE_FAULT_REASON_H
```

- [x] **Step 4: Extend the controller API**

In `FanFlapController.h`, include `FaultReason.h`, add:

```cpp
FaultReason lastFaultReason() const;
std::uint16_t faultCount() const;
void reportExternalFault(FaultReason reason);
```

Add private members:

```cpp
FaultReason lastFaultReason_;
std::uint16_t faultCount_;
void enterError(FaultReason reason);
```

Replace the old `void enterError();` declaration with the reason-taking
version.

- [x] **Step 5: Implement fault recording**

In the constructor initialize:

```cpp
lastFaultReason_(FaultReason::None),
faultCount_(0U),
```

In `begin()` reset `lastFaultReason_` to `FaultReason::None`, but keep
`faultCount_` because it is a runtime diagnostic counter.

Implement:

```cpp
FaultReason FanFlapController::lastFaultReason() const {
  return lastFaultReason_;
}

std::uint16_t FanFlapController::faultCount() const { return faultCount_; }

void FanFlapController::reportExternalFault(const FaultReason reason) {
  enterError(reason);
}

void FanFlapController::enterError(const FaultReason reason) {
  motor_.stop();
  motor_.setDriverEnabled(false);
  valveFreeCheckActive_ = false;
  lastFaultReason_ = reason;
  if (faultCount_ < 0xFFFFU) {
    ++faultCount_;
  }
  state_ = ControllerState::ErrorDetected;
}
```

Update all current `enterError()` call sites with exact reasons.

- [ ] **Step 6: Verify and commit**

Run:

```powershell
platformio test -e native
```

Expected: all native tests pass, count increases by 2.

Commit:

```powershell
git add lib/Luefterklappe/src/FaultReason.h lib/Luefterklappe/src/FanFlapController.h lib/Luefterklappe/src/FanFlapController.cpp test/test_controller/test_main.cpp
git -c user.name=Airhog-Fpv -c user.email=116745046+Mosei1984@users.noreply.github.com commit --author="Airhog-Fpv <116745046+Mosei1984@users.noreply.github.com>" -m "Add firmware fault diagnostics"
```

## Task 2: Modbus and Text Diagnostics

**Files:**

- Modify: `lib/Luefterklappe/src/ModbusRtuServer.h`
- Modify: `lib/Luefterklappe/src/ModbusRtuServer.cpp`
- Modify: `lib/Luefterklappe/src/FanFlapController.cpp`
- Test: `test/test_controller/test_main.cpp`

- [x] **Step 1: Add failing Modbus diagnostic tests**

Add:

```cpp
void test_modbus_reads_release_diagnostic_registers() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);
  FakeUart uart;
  ModbusRtuServer server(controller, uart);

  bringControllerToReady(controller, stepper, events);
  stepper.speed_ = 1.0F;
  controller.tick(DigitalInputs{true, false, false}, 20U);

  std::uint8_t frame[8]{1U, 0x03U, 0U, 17U, 0U, 6U, 0U, 0U};
  feedModbusFrame(server, frame, 6U);

  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(12U, uart.written_[2]);
  TEST_ASSERT_EQUAL_UINT16(
      static_cast<std::uint16_t>(FaultReason::UnexpectedMinSwitch),
      responseUint16(uart, 3U));
  TEST_ASSERT_EQUAL_UINT16(1U, responseUint16(uart, 5U));
}

void test_modbus_rejects_writes_to_release_diagnostic_registers() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);
  FakeUart uart;
  ModbusRtuServer server(controller, uart);

  std::uint8_t frame[8]{1U, 0x06U, 0U, 17U, 0U, 1U, 0U, 0U};
  feedModbusFrame(server, frame, 6U);

  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x86U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02U, uart.written_[2]);
}

void test_modbus_rejects_multiple_writes_to_release_diagnostic_registers() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);
  FakeUart uart;
  ModbusRtuServer server(controller, uart);

  std::uint8_t frame[11]{1U, 0x10U, 0U, 17U, 0U, 1U,
                         2U, 0U,    1U, 0U, 0U};
  feedModbusFrame(server, frame, 9U);

  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x90U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02U, uart.written_[2]);
}

void test_modbus_reads_full_release_register_block() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);
  FakeUart uart;
  ModbusRtuServer server(controller, uart);

  std::uint8_t frame[8]{1U, 0x03U, 0U, 0U, 0U, 23U, 0U, 0U};
  feedModbusFrame(server, frame, 6U);

  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x03U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(46U, uart.written_[2]);
}

void test_modbus_rejects_read_past_release_register_block() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);
  FakeUart uart;
  ModbusRtuServer server(controller, uart);

  std::uint8_t frame[8]{1U, 0x03U, 0U, 22U, 0U, 2U, 0U, 0U};
  feedModbusFrame(server, frame, 6U);

  TEST_ASSERT_EQUAL_UINT8(1U, uart.written_[0]);
  TEST_ASSERT_EQUAL_UINT8(0x83U, uart.written_[1]);
  TEST_ASSERT_EQUAL_UINT8(0x02U, uart.written_[2]);
}
```

- [x] **Step 2: Add appended register enum values**

In `ModbusRtuServer.h` append:

```cpp
LastFaultReason = 17U,
FaultCount = 18U,
SettingsStatus = 19U,
TmcHealth = 20U,
BootReason = 21U,
FirmwareProtocolVersion = 22U
```

- [x] **Step 3: Implement read-only diagnostics**

In `readRegister()` return:

```cpp
case ModbusRegister::LastFaultReason:
  value = static_cast<std::uint16_t>(controller_.lastFaultReason());
  return true;
case ModbusRegister::FaultCount:
  value = controller_.faultCount();
  return true;
case ModbusRegister::FirmwareProtocolVersion:
  value = 2U;
  return true;
```

For `SettingsStatus`, `TmcHealth` and `BootReason`, return `0U` until later
tasks wire runtime values through a diagnostic provider.

In `validateRegisterWrite()` classify registers `17..22` as
`kIllegalDataAddress`.

Read-range validation must accept `17/6` and `0/23`, and must reject
`22/2` or any request whose final register exceeds `22` with
`kIllegalDataAddress`.

- [x] **Step 4: Add text diagnostics**

In `FanFlapController::handleCommandText()` support:

```cpp
} else if (equals(text, "FAULT?")) {
  emit(EventId::FaultReported,
       static_cast<std::int32_t>(lastFaultReason_), faultCount_);
} else if (equals(text, "DIAG?")) {
  emit(EventId::DiagnosticsReported,
       static_cast<std::int32_t>(lastFaultReason_), faultCount_);
```

Add matching `EventId` values and event rendering in `src/main.cpp`.

- [ ] **Step 5: Verify and commit**

Run:

```powershell
platformio test -e native
platformio check -e native --skip-packages
```

Commit the touched files with message:

```text
Expose firmware diagnostics over Modbus
```

## Task 3: Debounced Inputs and Startup Sanity

**Files:**

- Create: `lib/Luefterklappe/src/InputDebouncer.h`
- Create: `lib/Luefterklappe/src/InputDebouncer.cpp`
- Modify: `src/main.cpp`
- Modify: `lib/Luefterklappe/src/FanFlapController.h`
- Modify: `lib/Luefterklappe/src/FanFlapController.cpp`
- Test: `test/test_controller/test_main.cpp`

- [x] **Step 1: Add failing debounce tests**

Add:

```cpp
void test_input_debouncer_requires_stable_switch_state() {
  InputDebouncer debouncer(5U);

  DigitalInputs stable = debouncer.tick(kNoInputs, 0U);
  TEST_ASSERT_FALSE(stable.minSwitchActive);

  stable = debouncer.tick(DigitalInputs{true, false, false}, 1U);
  TEST_ASSERT_FALSE(stable.minSwitchActive);

  stable = debouncer.tick(DigitalInputs{true, false, false}, 6U);
  TEST_ASSERT_TRUE(stable.minSwitchActive);
}

void test_both_endstops_active_at_boot_enters_fault() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.begin();
  controller.tick(DigitalInputs{true, true, false}, 0U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_EQUAL(FaultReason::BothEndstopsActiveAtBoot,
                    controller.lastFaultReason());
}
```

- [x] **Step 2: Implement `InputDebouncer`**

Use this public shape:

```cpp
class InputDebouncer {
 public:
  explicit InputDebouncer(std::uint32_t debounceMs);
  DigitalInputs tick(DigitalInputs raw, std::uint32_t nowMs);

 private:
  DigitalInputs stable_;
  DigitalInputs candidate_;
  std::uint32_t candidateSinceMs_;
  std::uint32_t debounceMs_;
  bool initialized_;
};
```

Use wrap-safe elapsed math:

```cpp
static bool elapsed(std::uint32_t nowMs, std::uint32_t sinceMs,
                    std::uint32_t durationMs) {
  return static_cast<std::uint32_t>(nowMs - sinceMs) >= durationMs;
}
```

- [x] **Step 3: Add boot sanity to controller**

In `handleInitState()` accept current inputs by changing the signature to:

```cpp
void handleInitState(const DigitalInputs& inputs);
```

If both endstops are active at boot, call:

```cpp
enterError(FaultReason::BothEndstopsActiveAtBoot);
return;
```

- [x] **Step 4: Wire debouncer in `src/main.cpp`**

Add:

```cpp
constexpr std::uint32_t kInputDebounceMs = 10UL;
InputDebouncer inputDebouncer(kInputDebounceMs);
```

Replace direct controller input usage with:

```cpp
const DigitalInputs rawInputs{readSwitch(kMinSwitchPin),
                              readSwitch(kMaxSwitchPin),
                              stallGuardActive};
const DigitalInputs inputs = inputDebouncer.tick(rawInputs, nowMs);
```

- [ ] **Step 5: Verify and commit**

Run:

```powershell
platformio test -e native
platformio run -e pico
```

Commit with message:

```text
Debounce firmware inputs before control loop
```

## Task 4: Motion No-Progress and Free-Check Timeout

**Files:**

- Modify: `lib/Luefterklappe/src/FanFlapController.h`
- Modify: `lib/Luefterklappe/src/FanFlapController.cpp`
- Test: `test/test_controller/test_main.cpp`

- [x] **Step 1: Add failing motion-supervision tests**

Add:

```cpp
void test_ready_move_faults_when_position_does_not_progress() {
  FakeStepper stepper;
  CapturingEvents events;
  ControllerConfig config = kFastTestConfig;
  config.motionNoProgressTimeoutMs = 25U;
  config.motionProgressMinSteps = 2L;
  FanFlapController controller(stepper, events, config);

  bringControllerToReady(controller, stepper, events);
  std::int32_t accepted = 0;
  TEST_ASSERT_TRUE(controller.requestMoveTo(700, accepted));
  stepper.speed_ = 1.0F;
  stepper.position_ = 400;

  controller.tick(kNoInputs, 20U);
  controller.tick(kNoInputs, 46U);

  TEST_ASSERT_EQUAL(ControllerState::ErrorDetected, controller.state());
  TEST_ASSERT_EQUAL(FaultReason::MotionNoProgress,
                    controller.lastFaultReason());
}

void test_free_check_times_out_before_required_travel() {
  FakeStepper stepper;
  CapturingEvents events;
  ControllerConfig config = kFastTestConfig;
  config.freeCheckTimeoutMs = 15U;
  FanFlapController controller(stepper, events, config);

  bringControllerToReady(controller, stepper, events);
  static_cast<void>(controller.moveTo(700));
  stepper.speed_ = 1.0F;

  controller.tick(kNoInputs, 20U);
  controller.tick(kNoInputs, 36U);

  TEST_ASSERT_EQUAL(FaultReason::ValveBlockedDuringFreeCheck,
                    controller.lastFaultReason());
}
```

- [x] **Step 2: Extend config**

Add fields to `ControllerConfig`:

```cpp
std::uint32_t freeCheckTimeoutMs;
std::uint32_t motionNoProgressTimeoutMs;
std::int32_t motionProgressMinSteps;
```

Set defaults:

```cpp
2000UL,
3000UL,
2L
```

- [x] **Step 3: Track movement progress**

Add private members:

```cpp
std::int32_t lastProgressPosition_;
std::uint32_t lastProgressMs_;
std::uint32_t freeCheckStartMs_;
void updateMotionSupervision(std::uint32_t nowMs);
```

Call `updateMotionSupervision(nowMs)` from `handleReadyState`, so change its
signature to:

```cpp
void handleReadyState(const DigitalInputs& inputs, std::uint32_t nowMs);
```

When progress exceeds `motionProgressMinSteps`, update `lastProgressPosition_`
and `lastProgressMs_`. If moving and elapsed exceeds
`motionNoProgressTimeoutMs`, enter `FaultReason::MotionNoProgress`.

- [x] **Step 4: Add free-check timeout**

In `beginValveFreeCheck()` record `freeCheckStartMs_` through a new
`nowMs` argument. If `valveFreeCheckActive_` and elapsed exceeds
`freeCheckTimeoutMs`, enter `FaultReason::ValveBlockedDuringFreeCheck`.

- [ ] **Step 5: Verify and commit**

Run:

```powershell
platformio test -e native
platformio check -e native --skip-packages
```

Commit with message:

```text
Add motion progress supervision
```

## Task 5: Dual-Sector Journaled Persistent Settings

**Files:**

- Modify: `lib/Luefterklappe/src/PersistentSettings.h`
- Modify: `lib/Luefterklappe/src/PersistentSettings.cpp`
- Modify: `src/main.cpp`
- Test: `test/test_controller/test_main.cpp`

- [x] **Step 1: Add failing persistence tests**

Add:

```cpp
void test_persistent_settings_loads_newest_valid_record() {
  FakeSettingsStorage storage;
  PersistentSettingsStore store(storage);

  TEST_ASSERT_TRUE(store.save(PersistentSettings{7U, 750U}));
  TEST_ASSERT_TRUE(store.save(PersistentSettings{8U, 250U}));

  PersistentSettings loaded = store.load();
  TEST_ASSERT_EQUAL_UINT8(8U, loaded.deviceId);
  TEST_ASSERT_EQUAL_UINT16(250U, loaded.safePositionPermille);
}

void test_persistent_settings_falls_back_to_previous_slot_after_corruption() {
  FakeSettingsStorage storage;
  PersistentSettingsStore store(storage);

  TEST_ASSERT_TRUE(store.save(PersistentSettings{7U, 750U}));
  TEST_ASSERT_TRUE(store.save(PersistentSettings{8U, 250U}));
  storage.corruptByte(16U);

  PersistentSettings loaded = store.load();
  TEST_ASSERT_EQUAL_UINT8(7U, loaded.deviceId);
  TEST_ASSERT_EQUAL_UINT16(750U, loaded.safePositionPermille);
}

void test_persistent_settings_survives_target_slot_write_failure() {
  FakeSettingsStorage storage;
  PersistentSettingsStore store(storage);

  TEST_ASSERT_TRUE(store.save(PersistentSettings{7U, 750U}));
  storage.failNextWriteForSlot(1U);
  TEST_ASSERT_FALSE(store.save(PersistentSettings{8U, 250U}));

  PersistentSettings loaded = store.load();
  TEST_ASSERT_EQUAL_UINT8(7U, loaded.deviceId);
  TEST_ASSERT_EQUAL_UINT16(750U, loaded.safePositionPermille);
}
```

Update the fake storage to model two independently erasable slots, per-slot
corruption and one-shot target write failure.

- [x] **Step 2: Define record format**

Use two 16-byte records:

```cpp
struct StoredRecord {
  std::uint8_t magic0;
  std::uint8_t magic1;
  std::uint8_t version;
  std::uint8_t deviceId;
  std::uint16_t safePositionPermille;
  std::uint32_t generation;
  std::uint16_t reserved;
  std::uint16_t crc;
};
```

Keep manual byte serialization instead of relying on struct layout.

- [x] **Step 3: Extend the storage abstraction**

Replace the single-buffer storage port with slot-aware operations:

```cpp
class SettingsStoragePort {
 public:
  virtual ~SettingsStoragePort() = default;
  virtual std::size_t slotCount() const = 0;
  virtual std::size_t slotSize() const = 0;
  virtual bool readSlot(std::size_t slot, std::uint8_t* data,
                        std::size_t size) = 0;
  virtual bool writeSlot(std::size_t slot, const std::uint8_t* data,
                         std::size_t size) = 0;
};
```

Require at least two slots. If fewer than two are available, load defaults and
report save failure.

- [x] **Step 4: Save alternating physical slots**

On `save()` load both records, choose the next slot after the highest valid
generation, increment generation with wrap-safe unsigned math and write only
the target physical slot. The previous slot must remain untouched until the new
slot has been written and validates.

- [x] **Step 5: Load highest valid slot**

On `load()` validate both records and return the highest-generation valid
record. If neither record is valid, return defaults.

- [x] **Step 6: Update the Pico Flash HAL**

In `src/main.cpp`, back `SettingsStoragePort` with the last two RP2040 flash
sectors. `writeSlot()` must erase and program only the selected sector, never
both sectors in one commit.

- [ ] **Step 7: Verify and commit**

Run:

```powershell
platformio test -e native
platformio run -e pico
```

Commit with message:

```text
Journal persistent controller settings across sectors
```

## Task 6: TMC2209 Health as Explicit State

**Files:**

- Modify: `lib/Luefterklappe/src/Tmc2209Driver.h`
- Modify: `lib/Luefterklappe/src/Tmc2209Driver.cpp`
- Modify: `src/main.cpp`
- Test: `test/test_controller/test_main.cpp`

- [x] **Step 1: Add failing TMC tests**

Add:

```cpp
void test_tmc_poll_reports_communication_error_without_response() {
  FakeUart uart;
  FakeDelay delay;
  CapturingEvents events;
  Tmc2209Driver driver(uart, delay, events);

  Tmc2209PollResult result = driver.pollStallGuardStatus();

  TEST_ASSERT_EQUAL(Tmc2209PollResult::CommunicationError, result);
}

void test_tmc_poll_reports_not_stalled_above_threshold() {
  FakeUart uart;
  FakeDelay delay;
  CapturingEvents events;
  Tmc2209Driver driver(uart, delay, events);
  queueTmcResponse(uart, 0x41U, 0x000003FFUL);

  Tmc2209PollResult result = driver.pollStallGuardStatus();

  TEST_ASSERT_EQUAL(Tmc2209PollResult::NotStalled, result);
}

void test_tmc_verify_communication_requires_valid_readback() {
  FakeUart uart;
  FakeDelay delay;
  CapturingEvents events;
  Tmc2209Driver driver(uart, delay, events);

  TEST_ASSERT_FALSE(driver.verifyCommunication());

  queueTmcResponse(uart, 0x00U, 0x000000C0UL);
  TEST_ASSERT_TRUE(driver.verifyCommunication());
}
```

- [x] **Step 2: Add explicit result enum**

In `Tmc2209Driver.h`:

```cpp
enum class Tmc2209PollResult : std::uint8_t {
  NotStalled,
  Stalled,
  CommunicationError
};

Tmc2209PollResult pollStallGuardStatus();
bool verifyCommunication();
```

Keep `bool pollStallGuard()` as a compatibility wrapper:

```cpp
bool pollStallGuard() {
  return pollStallGuardStatus() == Tmc2209PollResult::Stalled;
}
```

- [x] **Step 3: Wire main health handling**

In `src/main.cpp`, map `CommunicationError` to:

```cpp
controller.reportExternalFault(FaultReason::TmcCommunicationLost);
```

Only do this while the motor is moving and release build flag
`LUEFTERKLAPPE_REQUIRE_TMC2209_UART` is `1`. Service builds can set the flag to
`0` for bench testing without a driver.

Do not use the DIAG pin alone as the root cause. Combine movement context with
`GCONF`, `DRV_STATUS` and `SG_RESULT` readback where available. StallGuard
checks count only during a defined movement window, never at standstill.

- [ ] **Step 4: Verify and commit**

Run:

```powershell
platformio test -e native
platformio run -e pico
```

Commit with message:

```text
Report TMC2209 communication health explicitly
```

## Task 7: Safe Startup, Watchdog and Boot Reason

**Files:**

- Modify: `src/main.cpp`
- Modify: `lib/Luefterklappe/src/ModbusRtuServer.cpp`
- Test: native coverage stays focused on exposed Modbus diagnostic values;
  hardware behavior is checked by the release script in Task 9.

- [x] **Step 1: Safe output order**

Add a helper near the HAL globals:

```cpp
void prepareSafeOutputs() {
  digitalWrite(kEnablePin, HIGH);
  pinMode(kEnablePin, OUTPUT);
  digitalWrite(kEnablePin, HIGH);
  pinMode(kStatusLedPin, OUTPUT);
  digitalWrite(kStatusLedPin, LOW);
}
```

Call it as the first line of `setup()`, before UARTs, flash load, TMC init or
`controller.begin()`.

- [x] **Step 2: Watchdog feature flag**

Add:

```cpp
#ifndef LUEFTERKLAPPE_ENABLE_WATCHDOG
#define LUEFTERKLAPPE_ENABLE_WATCHDOG 1
#endif

constexpr std::uint32_t kWatchdogTimeoutMs = 2000UL;
```

Use the RP2040/Arduino-supported watchdog API in one small helper:

```cpp
void initializeWatchdog();
void kickWatchdog();
```

Use Mbed first in Arduino-Mbed builds:

```cpp
mbed::Watchdog& watchdog = mbed::Watchdog::get_instance();
watchdog.start(kWatchdogTimeoutMs);
watchdog.kick();
```

Check `watchdog.get_max_timeout()` before starting it. If the board package
does not expose Mbed watchdog symbols in this environment, keep the helpers
compiled out with `LUEFTERKLAPPE_ENABLE_WATCHDOG=0` and document that release
builds require a toolchain with watchdog support.

- [x] **Step 3: Expose boot reason**

Track:

```cpp
enum class BootReason : std::uint16_t {
  Unknown = 0U,
  PowerOn = 1U,
  Watchdog = 2U,
  SoftwareReset = 3U
};
```

Return it in Modbus register `21`. In no-watchdog test builds return
`Unknown` if the platform cannot distinguish reset causes. In Arduino-Mbed
release builds, use `mbed::ResetReason::get()` where available.

- [ ] **Step 4: Verify and commit**

Run:

```powershell
platformio run -e pico
platformio test -e native
```

Commit with message:

```text
Harden firmware startup and watchdog service
```

## Task 8: Service Self-Test and Factory Acceptance Commands

**Files:**

- Modify: `lib/Luefterklappe/src/FanFlapController.h`
- Modify: `lib/Luefterklappe/src/FanFlapController.cpp`
- Modify: `src/main.cpp`
- Modify: `README.md`
- Test: `test/test_controller/test_main.cpp`

- [x] **Step 1: Add command tests**

Add:

```cpp
void test_service_selftest_command_reports_without_moving_motor() {
  FakeStepper stepper;
  CapturingEvents events;
  FanFlapController controller(stepper, events, kFastTestConfig);

  controller.handleCommand("SELFTEST?");

  TEST_ASSERT_TRUE(events.contains(EventId::SelfTestReported));
  TEST_ASSERT_EQUAL_INT32(0, stepper.target_);
  TEST_ASSERT_FALSE(stepper.enabled_);
}
```

- [x] **Step 2: Add non-moving self-test event**

Add `EventId::SelfTestReported`. The command must not move the motor. It only
reports:

- current state;
- last fault reason;
- fault counter;
- device ID;
- safe position;
- soft-endstop status.

- [x] **Step 3: Add HAL-backed self-test details**

In `src/main.cpp`, extend the serial event rendering for `SelfTestReported`
with these additional lines:

```text
SELFTEST STATE=<state>
SELFTEST ID=<id>
SELFTEST SAFE=<safe>
SELFTEST FAULT=<reason>
SELFTEST FAULTCOUNT=<count>
SELFTEST ENDSTOPS MIN=<0|1> MAX=<0|1>
```

Keep the format line-oriented so the PC configurator and release script can
parse it.

- [ ] **Step 4: Verify and commit**

Run:

```powershell
platformio test -e native
platformio run -e pico
```

Commit with message:

```text
Add firmware self-test command
```

## Task 9: Firmware Final Acceptance Script

**Files:**

- Create: `tools/firmware_release_check.ps1`
- Modify: `README.md`
- Modify: `tools/la/README.md`
- Modify: `AGENTS.md`

- [x] **Step 1: Create release-check script**

Create `tools/firmware_release_check.ps1` with parameters:

```powershell
param(
  [switch] $NoHardware,
  [switch] $RequireLogicAnalyzer,
  [string] $SerialPort = "",
  [string] $ExpectedDeviceId = "1",
  [string] $ArtifactRoot = "artifacts\release\firmware"
)
```

The script must:

1. run `.\tools\run_hard_checks.ps1`;
2. copy `.pio\build\pico\firmware.uf2` into `$ArtifactRoot`;
3. write SHA256 for the UF2;
4. if `$NoHardware` is not set, open the serial port and send:
   - `ID<id> DIAG?`
   - `ID<id> FAULT?`
   - `ID<id> SELFTEST?`
5. if `$RequireLogicAnalyzer` is set, invoke
   `.\tools\la\capture_luefterklappe.ps1 -ExpectedId <id>`;
6. verify docs do not market the firmware as fire, smoke, CO, combustion or
   life-safety equipment;
7. write `acceptance-report.md` with command outputs and pass/fail sections.

- [x] **Step 2: Add no-hardware mode behavior**

When `-NoHardware` is set, the script still succeeds only if:

- hard gate exits `0`;
- `firmware.uf2` exists;
- SHA256 was written;
- report file exists.

- [x] **Step 3: Add full-hardware mode behavior**

When hardware is connected, require:

- serial port opens;
- `DIAG?`, `FAULT?`, `SELFTEST?` each return at least one line;
- no line starts with `FEHLER:` during idle diagnostics;
- Modbus RTU read `0..22` and read `17..22` return valid CRC frames;
- Modbus RTU read past `22` returns an exception frame with valid CRC;
- Modbus RTU writes to diagnostic registers return illegal-address exceptions;
- broadcast write to ID `0` produces no response and no controller state
  change;
- optional LA analyzer returns no `FAIL:` line.

- [x] **Step 4: Verify and commit**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\firmware_release_check.ps1 -NoHardware
```

Expected artifacts:

```text
artifacts/release/firmware/firmware.uf2
artifacts/release/firmware/firmware.uf2.sha256
artifacts/release/firmware/acceptance-report.md
```

Commit with message:

```text
Add firmware release acceptance check
```

## Task 10: Final Documentation and Release Gate

**Files:**

- Modify: `README.md`
- Modify: `test/README`
- Modify: `tools/la/README.md`
- Modify: `docs/diagrams/architecture.mmd`
- Modify: `docs/diagrams/wiring.mmd` if any new LA channel is documented

- [x] **Step 1: Document release diagnostics**

Update the Modbus register table in `README.md` with registers `17..22`.
Document `DIAG?`, `FAULT?` and `SELFTEST?`. Recommend Loxone diagnostic
polling intervals of `2..5s` or slower; target-position writes should stay
event-driven.

Add this product-safety block:

```text
This controller is a comfort/home-automation fan-flap controller for an 80-mm
pipe valve. It is not a fire damper, smoke damper, CO detector, combustion
appliance safety device or life-safety product. Smoke alarms, CO alarms,
combustion-appliance maintenance and building-level ventilation planning remain
separate requirements.
```

- [x] **Step 2: Document acceptance modes**

Add:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\firmware_release_check.ps1 -NoHardware
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\firmware_release_check.ps1 -SerialPort COMx -ExpectedDeviceId 1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\firmware_release_check.ps1 -SerialPort COMx -ExpectedDeviceId 1 -RequireLogicAnalyzer
```

- [x] **Step 3: Render diagrams and lint docs**

Run:

```powershell
npm run diagrams
npm run markdownlint
```

Expected: both exit `0`.

- [x] **Step 4: Run final hard gate**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\run_hard_checks.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\firmware_release_check.ps1 -NoHardware
```

Expected:

- Configurator tests pass;
- native firmware tests pass;
- Pico build produces UF2;
- clang-tidy/cppcheck/MISRA pass;
- markdownlint passes;
- acceptance report exists.

- [x] **Step 5: Commit final docs**

Commit with message:

```text
Document firmware release acceptance process
```

## Final Release Checklist

- [x] Hard all-functions gate passed:
  `.\tools\run_hard_checks.ps1`
- [x] Firmware release check passed in no-hardware mode:
  `.\tools\firmware_release_check.ps1 -NoHardware`
- [ ] Hardware smoke passed with Pico connected:
  `.\tools\firmware_release_check.ps1 -SerialPort COMx -ExpectedDeviceId 1`
- [ ] Full hardware acceptance passed with motor, endstops, RS485 and LA:
  `.\tools\firmware_release_check.ps1 -SerialPort COMx -ExpectedDeviceId 1 -RequireLogicAnalyzer`
- [x] `artifacts/release/firmware/acceptance-report.md` inspected.
- [x] `firmware.uf2.sha256` stored with the release artifact.
- [x] Commit author verified as
  `Airhog-Fpv <116745046+Mosei1984@users.noreply.github.com>`.
- [x] No AI/co-author/generated-by metadata in commits.

## Execution Notes

- Implement tasks in order. Each task adds release value and keeps the firmware
  testable.
- Do not change existing Modbus register numbers. Append only.
- Do not allow Modbus broadcast ID `0` to execute writes.
- Keep `REFRESH` as the preferred recovery path after a controller fault.
- Keep the default safe position open at `1000` permille unless a product
  decision changes this explicitly.
- Full "auslieferungsfertig" status requires the final hardware acceptance run
  with assembled endstops, TMC2209 driver, motor, RS485 and logic analyzer.
