#!/usr/bin/env python3

"""Compare the emulator's architectural commit trace with Spike."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import re
import subprocess
import sys
from pathlib import Path


DUT_PREFIX = "RV32TRACE "
SPIKE_RECORD = re.compile(
    r"^core\s+\d+:\s+([0-3])\s+"
    r"0x([0-9a-fA-F]+)\s+\(0x([0-9a-fA-F]+)\)(.*)$"
)
SPIKE_GPR_WRITE = re.compile(
    r"(?:^|\s)x\s*(\d+)\s+0x([0-9a-fA-F]+)"
)


@dataclass(frozen=True)
class Commit:
    privilege: int
    pc: int
    instruction: int
    next_pc: int | None
    register: int | None
    value: int | None


def parse_dut_line(line: str) -> Commit | None:
    if not line.startswith(DUT_PREFIX):
        return None

    fields = line.split()
    if len(fields) != 6:
        raise ValueError(f"malformed DUT trace line: {line}")

    register = None
    value = None
    if fields[5] != "-":
        match = re.fullmatch(
            r"x(\d+)=([0-9a-fA-F]{1,8})",
            fields[5],
        )
        if match is None:
            raise ValueError(f"malformed DUT register write: {line}")
        register = int(match.group(1), 10)
        value = int(match.group(2), 16)

    return Commit(
        privilege=int(fields[1], 10),
        pc=int(fields[2], 16),
        instruction=int(fields[3], 16),
        next_pc=int(fields[4], 16),
        register=register,
        value=value,
    )


def parse_spike_line(line: str) -> Commit | None:
    match = SPIKE_RECORD.match(line)
    if match is None:
        return None

    register = None
    value = None
    register_match = SPIKE_GPR_WRITE.search(match.group(4))
    if register_match is not None:
        parsed_register = int(register_match.group(1), 10)
        if parsed_register != 0:
            register = parsed_register
            value = int(register_match.group(2), 16) & 0xFFFFFFFF

    return Commit(
        privilege=int(match.group(1), 10),
        pc=int(match.group(2), 16) & 0xFFFFFFFF,
        instruction=int(match.group(3), 16),
        next_pc=None,
        register=register,
        value=value,
    )


def parse_records(
    output: str,
    parser,
    base: int,
    size: int,
) -> list[Commit]:
    end = base + size
    records = []
    for line in output.splitlines():
        record = parser(line.strip())
        if record is not None and base <= record.pc < end:
            records.append(record)
    return records


def compare_records(
    dut: list[Commit],
    spike: list[Commit],
    allow_spike_tail: bool = False,
) -> str | None:
    if not dut:
        return "DUT produced no commit records"
    if not spike:
        return "Spike produced no commit records in the DUT memory range"

    for index, (actual, expected) in enumerate(zip(dut, spike)):
        actual_architecture = (
            actual.privilege,
            actual.pc,
            actual.instruction,
            actual.register,
            actual.value,
        )
        expected_architecture = (
            expected.privilege,
            expected.pc,
            expected.instruction,
            expected.register,
            expected.value,
        )
        if actual_architecture != expected_architecture:
            return (
                f"commit {index} differs\n"
                f"  DUT:   {actual}\n"
                f"  Spike: {expected}"
            )

    if len(dut) == len(spike):
        return None
    if allow_spike_tail and len(spike) > len(dut):
        return None
    return (
        "commit count differs after the common prefix matched: "
        f"DUT={len(dut)}, Spike={len(spike)}"
    )


def run_command(command: list[str], timeout: float) -> str:
    completed = subprocess.run(
        command,
        capture_output=True,
        check=False,
        text=True,
        timeout=timeout,
    )
    output = completed.stdout + "\n" + completed.stderr
    if completed.returncode != 0:
        raise RuntimeError(
            f"command exited with {completed.returncode}: "
            f"{' '.join(command)}\n{output}"
        )
    return output


def self_test() -> int:
    dut_line = (
        "RV32TRACE 3 80000000 00000093 80000004 "
        "x1=00000001"
    )
    spike_line = (
        "core   0: 3 0x80000000 (0x00000093) "
        "x1  0x00000001"
    )
    compressed_line = (
        "core   0: 3 0x80000004 (0x0085)"
    )

    dut = parse_dut_line(dut_line)
    spike = parse_spike_line(spike_line)
    compressed = parse_spike_line(compressed_line)
    if (
        dut is None
        or spike is None
        or compressed is None
        or dut.privilege != 3
        or dut.pc != 0x80000000
        or dut.register != 1
        or dut.value != 1
        or spike.instruction != 0x00000093
        or compressed.instruction != 0x0085
    ):
        print("Spike trace parser self-test failed", file=sys.stderr)
        return 1
    if compare_records([dut], [spike]) is not None:
        print("equal trace comparison self-test failed", file=sys.stderr)
        return 1
    if compare_records([dut], [spike, compressed]) is None:
        print("strict trace-length self-test failed", file=sys.stderr)
        return 1
    if compare_records(
        [dut],
        [spike, compressed],
        allow_spike_tail=True,
    ) is not None:
        print("Spike tail comparison self-test failed", file=sys.stderr)
        return 1

    print("Spike trace parser self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--dut", type=Path)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--spike", type=Path)
    parser.add_argument("--base", type=lambda value: int(value, 0),
                        default=0x80000000)
    parser.add_argument("--size", type=lambda value: int(value, 0),
                        default=0x00100000)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument(
        "--allow-spike-tail",
        action="store_true",
        help=(
            "allow Spike commit records after the DUT has stopped; "
            "the complete DUT prefix is still compared"
        ),
    )
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    required = {
        "--dut": args.dut,
        "--binary": args.binary,
        "--elf": args.elf,
        "--spike": args.spike,
    }
    missing = [name for name, value in required.items() if value is None]
    if missing:
        parser.error(f"missing required arguments: {', '.join(missing)}")

    dut_output = run_command(
        [str(args.dut), "--trace", str(args.binary)],
        args.timeout,
    )
    spike_output = run_command(
        [
            str(args.spike),
            "--isa=rv32imac_zicsr_zifencei",
            "--priv=msu",
            "--pmpregions=0",
            f"-m0x{args.base:x}:0x{args.size:x}",
            "--instructions=1000000",
            "--log-commits",
            str(args.elf),
        ],
        args.timeout,
    )

    dut_records = parse_records(
        dut_output,
        parse_dut_line,
        args.base,
        args.size,
    )
    spike_records = parse_records(
        spike_output,
        parse_spike_line,
        args.base,
        args.size,
    )
    difference = compare_records(
        dut_records,
        spike_records,
        allow_spike_tail=args.allow_spike_tail,
    )
    if difference is not None:
        print(difference, file=sys.stderr)
        return 1

    message = (
        "Spike differential trace passed; "
        f"compared {len(dut_records)} commits"
    )
    if len(spike_records) > len(dut_records):
        message += (
            f"; ignored {len(spike_records) - len(dut_records)} "
            "post-DUT Spike commits"
        )
    print(message)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
