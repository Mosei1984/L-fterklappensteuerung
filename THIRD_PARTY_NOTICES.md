# Third-Party Notices

This repository contains project code under the root `LICENSE`. Third-party
software, SDKs, frameworks, packages, tools, trademarks and documentation remain
under their own license terms and are not relicensed by this project.

## Runtime and build dependencies

- Microsoft .NET, ASP.NET Core, WebView2, System.IO.Ports and Microsoft test
  packages are provided by Microsoft under their applicable license terms.
- Raspberry Pi Pico, RP2040, Arduino Mbed and PlatformIO packages are provided
  by their respective projects under their applicable license terms.
- GCC Arm Embedded, newlib and related toolchain components are provided under
  their respective license terms, including runtime/library exceptions where
  applicable.
- xUnit, coverlet, Mermaid CLI, markdownlint-cli2 and related packages are
  development or test dependencies and remain under their own license terms.

Before distributing source, firmware, installer packages or other binaries,
verify the exact dependency versions included in that release and keep all
required third-party license texts and notices with the release artifacts.

## Stepper firmware dependency decision

The production Pico firmware intentionally does not use the AccelStepper
library. AccelStepper is GPLv3 or commercially licensed by AirSpayce/Mike
McCauley, and linking it into this firmware would conflict with the repository's
commercial-use restriction unless a suitable separate commercial license is in
place or the firmware distribution model is changed accordingly.

Do not reintroduce AccelStepper or another copyleft runtime dependency into
shipping firmware without a fresh license review and matching notices.
