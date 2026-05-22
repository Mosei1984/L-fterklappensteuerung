# Firmware Release Hardening Research

Date: 2026-05-23

Scope: information gathering for making the Pico firmware release-ready for the
80-mm fan-flap controller.

## Sources Checked

- Raspberry Pi Pico SDK hardware APIs:
  <https://www.raspberrypi.com/documentation/pico-sdk/hardware.html>
- Arduino Mbed core:
  <https://github.com/arduino/ArduinoCore-mbed>
- Mbed OS Watchdog:
  <https://os.mbed.com/docs/mbed-os/v6.16/apis/watchdog.html>
- Mbed OS ResetReason:
  <https://os.mbed.com/docs/mbed-os/v6.16/apis/resetreason.html>
- Mbed OS FlashIAP:
  <https://os.mbed.com/docs/mbed-os/v6.16/apis/flash-iap.html>
- Mbed OS FlashIAP class reference:
  <https://os.mbed.com/docs/mbed-os/v6.16/mbed-os-api-doxy/classmbed_1_1_flash_i_a_p.html>
- Analog Devices TMC2209 product page:
  <https://www.analog.com/en/products/TMC2209.html>
- Analog Devices TMC2209 datasheet Rev. 1.09:
  <https://www.analog.com/media/en/technical-documentation/data-sheets/tmc2209_datasheet_rev1.09.pdf>
- BIGTREETECH TMC2209 documentation:
  <https://neo.bttwiki.com/en/docs/accessories-docs/tmc-driver/tmc-2209/>
- Modbus Application Protocol V1.1b3:
  <https://modbus.org/docs/Modbus_Application_Protocol_V1_1b3.pdf>
- Modbus over Serial Line specification:
  <https://www.modbus.org/docs/Modbus_over_serial_line_V1.pdf>
- Loxone Modbus Extension:
  <https://www.loxone.com/enus/kb/modbus-extension/>
- Loxone Modbus communication:
  <https://www.loxone.com/enen/kb/communication-with-modbus/>
- German Federal Environment Ministry indoor ventilation guidance:
  <https://www.bundesumweltministerium.de/themen/gesundheit/innenraumluft/richtiges-lueften-und-heizen>
- German Environment Agency indoor-air ventilation guidance:
  <https://www.umweltbundesamt.de/themen/gesundheit/umwelteinfluesse-auf-den-menschen/innenraumluft/lueften-wegen-dicker-luft>
- DIN FAQ for DIN 1946-6 ventilation planning:
  <https://www.din.de/de/service-fuer-anwender/normungsportale/normungsportal-klima-und-lueftungstechnik/faqs-zur-din-1946-6-2019-12-901724>
- CDC carbon monoxide basics:
  <https://www.cdc.gov/carbon-monoxide/about/index.html>
- EPA home ventilation guidance:
  <https://www.epa.gov/indoor-air-quality-iaq/how-much-ventilation-do-i-need-my-home-improve-indoor-air-quality>
- EU general product safety harmonised standards page:
  <https://single-market-economy.ec.europa.eu/single-market/goods/european-standards/harmonised-standards/general-product-safety_en>

## Decisions for Firmware Plan

- Keep mechanical endstops as the primary homing reference. StallGuard remains
  redundant fallback during homing and overload plausibility during movement.
- Treat TMC2209 UART communication failure as its own fault. A failed read is
  not equivalent to "not stalled".
- Treat `DIAG` as a multiplexed diagnostic signal, not as a standalone root
  cause. Pair it with register readback where possible.
- Use a real two-sector A/B flash journal if power-loss robustness is claimed.
  Two records in one erase sector are only corruption-tolerant, not power-loss
  safe during sector erase.
- Initialize the active-low stepper enable pin to the disabled level before any
  slow setup step. The release implementation should latch disabled, set output
  direction, then write disabled again.
- Use Mbed `Watchdog` and `ResetReason` first in Arduino-Mbed builds. Pico SDK
  watchdog functions can be used only after confirming the symbols are
  available in this PlatformIO environment.
- Append Modbus diagnostics after register `16`; do not renumber existing
  registers.
- Keep the product rule that Modbus broadcast address `0` is ignored for writes.
  This is stricter than standard Modbus broadcast behavior and must be
  documented as a home-safety choice.
- Do not market the firmware or 80-mm valve as a fire, smoke, CO, combustion
  appliance, or life-safety device.
- Default safe position remains `1000` permille open. Closed default would block
  ventilation and would imply a smoke/fire behavior this product does not
  provide.

## Plan Adjustments Required

- Task 2 must add tests for reading `17..22`, reading `0..22`, write-single and
  write-multiple rejection on diagnostic registers, and read overrun rejection.
- Task 5 must change from single-sector dual record to dual physical sectors or
  explicitly downgrade the claim. Release target is dual physical sectors.
- Task 6 must expose TMC health with explicit communication error, not boolean
  stall state only.
- Task 7 must use safe output order and must not overstate boot-reason accuracy
  unless the exact platform API is verified.
- Task 9 must include Modbus acceptance cases for wrong ID, broadcast ID, CRC
  error, illegal diagnostic writes and read-overrun exception frames.
- Task 10 must add a product-safety warning block and installation language:
  no fire/smoke/CO safety function, no DIN/ASHRAE compliance claim without
  building-level planning and measurement.
