#!/usr/bin/env python3
"""Validate sigrok logic-analyzer captures for the fan flap controller."""

from __future__ import annotations

import argparse
import csv
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


@dataclass(frozen=True)
class Frame:
    offset: int
    data: bytes


@dataclass
class Report:
    checks: int = 0
    failures: list[str] | None = None

    def __post_init__(self) -> None:
        if self.failures is None:
            self.failures = []

    def ok(self, message: str) -> None:
        self.checks += 1
        print(f"OK: {message}")

    def fail(self, message: str) -> None:
        self.checks += 1
        assert self.failures is not None
        self.failures.append(message)
        print(f"FAIL: {message}")

    def exit_code(self) -> int:
        assert self.failures is not None
        return 1 if self.failures else 0


def modbus_crc(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
            crc &= 0xFFFF
    return crc


def tmc_crc(data: bytes) -> int:
    crc = 0
    for value in data:
        current = value
        for _ in range(8):
            xor_bit = ((crc >> 7) ^ (current & 0x01)) & 0x01
            crc = (crc << 1) & 0xFF
            if xor_bit:
                crc ^= 0x07
            current >>= 1
    return crc


def append_modbus_crc(payload: bytes) -> bytes:
    crc = modbus_crc(payload)
    return payload + bytes((crc & 0xFF, (crc >> 8) & 0xFF))


def frame_modbus_length(data: bytes, offset: int, response_stream: bool) -> int | None:
    if len(data) - offset < 2:
        return None
    function = data[offset + 1]
    if function & 0x80:
        return 5
    if function in (0x03, 0x04):
        if response_stream and len(data) - offset >= 3:
            return 5 + data[offset + 2]
        return 8
    if function in (0x06, 0x10):
        if function == 0x10 and not response_stream and len(data) - offset >= 7:
            return 9 + data[offset + 6]
        return 8
    return 5


def parse_modbus_stream(data: bytes, response_stream: bool) -> tuple[list[Frame], list[str]]:
    frames: list[Frame] = []
    errors: list[str] = []
    offset = 0

    while offset < len(data):
        length = frame_modbus_length(data, offset, response_stream)
        if length is None:
            break
        if len(data) - offset < length:
            errors.append(f"truncated Modbus frame at byte {offset}")
            break

        candidate = data[offset : offset + length]
        expected_crc = modbus_crc(candidate[:-2])
        received_crc = candidate[-2] | (candidate[-1] << 8)
        if expected_crc == received_crc:
            frames.append(Frame(offset, candidate))
            offset += length
        else:
            errors.append(f"Modbus CRC/resync failure at byte {offset}")
            offset += 1

    return frames, errors


def tmc_candidate_lengths(data: bytes, offset: int) -> tuple[int, ...]:
    if len(data) - offset >= 8:
        return (8, 4)
    return (4,)


def parse_tmc_stream(data: bytes) -> tuple[list[Frame], list[str]]:
    frames: list[Frame] = []
    errors: list[str] = []
    offset = 0

    while offset < len(data):
        if data[offset] != 0x05:
            errors.append(f"TMC resync skipped byte {offset}")
            offset += 1
            continue
        if len(data) - offset < 4:
            errors.append(f"truncated TMC frame at byte {offset}")
            break

        decoded = False
        for length in tmc_candidate_lengths(data, offset):
            candidate = data[offset : offset + length]
            expected_crc = tmc_crc(candidate[:-1])
            if expected_crc == candidate[-1]:
                frames.append(Frame(offset, candidate))
                offset += length
                decoded = True
                break

        if not decoded:
            errors.append(f"TMC CRC/resync failure at byte {offset}")
            offset += 1

    return frames, errors


def read_file(path: str | None) -> bytes:
    if path is None:
        return b""
    return Path(path).read_bytes()


def validate_modbus(path: str | None, expected_id: int, response_stream: bool, report: Report) -> None:
    data = read_file(path)
    if not data:
        return

    frames, errors = parse_modbus_stream(data, response_stream)
    label = "RS485 device TX" if response_stream else "RS485 device RX"
    for error in errors:
        report.fail(f"{label}: {error}")

    if frames:
        report.ok(f"{label}: decoded {len(frames)} Modbus frame(s)")
    else:
        report.fail(f"{label}: no valid Modbus frames decoded")

    for frame in frames:
        address = frame.data[0]
        function = frame.data[1]
        if response_stream and address != expected_id:
            report.fail(f"{label}: response at byte {frame.offset} uses address {address}")
        if response_stream and address == 0:
            report.fail(f"{label}: broadcast response at byte {frame.offset}")
        if response_stream and function not in (0x03, 0x06, 0x10, 0x83, 0x86, 0x90):
            report.fail(f"{label}: unexpected response function 0x{function:02X}")


def validate_tmc(path: str | None, label: str, report: Report) -> None:
    data = read_file(path)
    if not data:
        return

    frames, errors = parse_tmc_stream(data)
    for error in errors:
        report.fail(f"{label}: {error}")

    if frames:
        report.ok(f"{label}: decoded {len(frames)} TMC UART frame(s)")
    else:
        report.fail(f"{label}: no valid TMC UART frames decoded")


def parse_logic_value(value: str) -> int:
    normalized = value.strip().lower()
    if normalized in ("1", "h", "high", "true"):
        return 1
    if normalized in ("0", "l", "low", "false"):
        return 0
    raise ValueError(f"unsupported logic value {value!r}")


def detect_delimiter(sample: str) -> str:
    try:
        return csv.Sniffer().sniff(sample, delimiters=",;\t").delimiter
    except csv.Error:
        return ","


def load_csv_samples(path: str, samplerate_hz: float) -> tuple[list[str], list[tuple[float, dict[str, int]]]]:
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    lines = [line for line in text.splitlines() if line and not line.startswith("#")]
    if not lines:
        raise ValueError("CSV contains no sample rows")

    delimiter = detect_delimiter("\n".join(lines[:5]))
    reader = csv.DictReader(lines, delimiter=delimiter)
    if reader.fieldnames is None:
        raise ValueError("CSV has no header")

    samples: list[tuple[float, dict[str, int]]] = []
    for index, row in enumerate(reader):
        time_s = index / samplerate_hz
        for key in ("time", "Time", "TIME"):
            if key in row and row[key] not in (None, ""):
                time_s = float(row[key])
                break

        values: dict[str, int] = {}
        for channel, value in row.items():
            if channel is None or channel.lower() in ("time", "samplenum", "sample"):
                continue
            if value is None or value == "":
                continue
            values[channel] = parse_logic_value(value)
        samples.append((time_s, values))

    return list(reader.fieldnames), samples


def channel_edges(samples: Iterable[tuple[float, dict[str, int]]], channel: str) -> list[tuple[float, int, int]]:
    previous: int | None = None
    edges: list[tuple[float, int, int]] = []
    for time_s, values in samples:
        if channel not in values:
            continue
        current = values[channel]
        if previous is not None and current != previous:
            edges.append((time_s, previous, current))
        previous = current
    return edges


def value_at(samples: list[tuple[float, dict[str, int]]], channel: str, time_s: float) -> int | None:
    current: int | None = None
    for sample_time, values in samples:
        if sample_time > time_s:
            break
        if channel in values:
            current = values[channel]
    return current


def validate_timing(args: argparse.Namespace, report: Report) -> None:
    if args.csv is None:
        return

    _, samples = load_csv_samples(args.csv, args.samplerate_hz)
    step_edges = channel_edges(samples, args.step_channel)
    rising_steps = [edge for edge in step_edges if edge[1] == 0 and edge[2] == 1]
    falling_steps = [edge for edge in step_edges if edge[1] == 1 and edge[2] == 0]

    if not rising_steps:
        report.fail("STEP: no rising edges captured")
        return
    report.ok(f"STEP: captured {len(rising_steps)} rising edge(s)")

    min_high_s = args.min_step_high_us / 1_000_000.0
    for rise_time, _, _ in rising_steps:
        next_fall = next((edge for edge in falling_steps if edge[0] > rise_time), None)
        if next_fall is not None and (next_fall[0] - rise_time) < min_high_s:
            report.fail(f"STEP: high pulse shorter than {args.min_step_high_us} us at {rise_time:.6f}s")

    if args.enable_channel:
        disabled_value = 1 if args.enable_active_low else 0
        for rise_time, _, _ in rising_steps:
            enable_value = value_at(samples, args.enable_channel, rise_time)
            if enable_value == disabled_value:
                report.fail(f"STEP: pulse while driver disabled at {rise_time:.6f}s")
        report.ok("ENABLE: no step pulses while disabled")

    if args.dir_channel:
        dir_edges = channel_edges(samples, args.dir_channel)
        min_setup_s = args.min_dir_setup_us / 1_000_000.0
        min_hold_s = args.min_dir_hold_us / 1_000_000.0
        for rise_time, _, _ in rising_steps:
            previous_dir = [edge for edge in dir_edges if edge[0] <= rise_time]
            next_dir = next((edge for edge in dir_edges if edge[0] > rise_time), None)
            if previous_dir and (rise_time - previous_dir[-1][0]) < min_setup_s:
                report.fail(f"DIR: setup shorter than {args.min_dir_setup_us} us at {rise_time:.6f}s")
            if next_dir is not None and (next_dir[0] - rise_time) < min_hold_s:
                report.fail(f"DIR: hold shorter than {args.min_dir_hold_us} us at {rise_time:.6f}s")
        report.ok("DIR: setup/hold constraints checked")


def run_self_test() -> int:
    report = Report()
    modbus_response = append_modbus_crc(bytes((1, 0x03, 0x02, 0x00, 0x05)))
    frames, errors = parse_modbus_stream(modbus_response, response_stream=True)
    if errors or len(frames) != 1:
        report.fail("self-test Modbus parser")
    else:
        report.ok("self-test Modbus parser")

    tmc_write = bytes((0x05, 0x00, 0x80, 0x00, 0x00, 0x00, 0x40))
    tmc_write += bytes((tmc_crc(tmc_write),))
    tmc_frames, tmc_errors = parse_tmc_stream(tmc_write)
    if tmc_errors or len(tmc_frames) != 1:
        report.fail("self-test TMC parser")
    else:
        report.ok("self-test TMC parser")

    tmc_response = bytes((0x05, 0xFF, 0x6F, 0x00, 0x00, 0x02, 0x02))
    tmc_response += bytes((tmc_crc(tmc_response),))
    tmc_response_frames, tmc_response_errors = parse_tmc_stream(tmc_response)
    if tmc_response_errors or len(tmc_response_frames) != 1:
        report.fail("self-test TMC response parser")
    else:
        report.ok("self-test TMC response parser")

    return report.exit_code()


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--expected-id", type=int, default=1)
    parser.add_argument("--rs485-device-rx-bin")
    parser.add_argument("--rs485-device-tx-bin")
    parser.add_argument("--tmc-tx-bin")
    parser.add_argument("--tmc-rx-bin")
    parser.add_argument("--csv")
    parser.add_argument("--samplerate-hz", type=float, default=4_000_000.0)
    parser.add_argument("--step-channel", default="D2")
    parser.add_argument("--dir-channel", default="D3")
    parser.add_argument("--enable-channel", default="D6")
    enable_level = parser.add_mutually_exclusive_group()
    enable_level.add_argument("--enable-active-low", dest="enable_active_low",
                              action="store_true", default=True)
    enable_level.add_argument("--enable-active-high", dest="enable_active_low",
                              action="store_false")
    parser.add_argument("--min-step-high-us", type=float, default=1.0)
    parser.add_argument("--min-dir-setup-us", type=float, default=2.0)
    parser.add_argument("--min-dir-hold-us", type=float, default=2.0)
    return parser


def main(argv: list[str]) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)

    if args.self_test:
        return run_self_test()

    report = Report()
    validate_modbus(args.rs485_device_rx_bin, args.expected_id, False, report)
    validate_modbus(args.rs485_device_tx_bin, args.expected_id, True, report)
    validate_tmc(args.tmc_tx_bin, "TMC TX", report)
    validate_tmc(args.tmc_rx_bin, "TMC RX", report)

    try:
        validate_timing(args, report)
    except (OSError, ValueError) as exc:
        report.fail(f"CSV timing analysis failed: {exc}")

    if report.checks == 0:
        parser.error("no input captures supplied")

    return report.exit_code()


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
