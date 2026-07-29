#!/usr/bin/env python3

"""Compare the emulator's architectural commit trace with Spike."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import re
import subprocess
import sys
from pathlib import Path


SPIKE_RECORD = re.compile(
    r"^core\s+\d+:\s+([0-3])\s+"
    r"0x([0-9a-fA-F]+)\s+\(0x([0-9a-fA-F]+)\)(.*)$"
)
SPIKE_GPR_WRITE = re.compile(
    r"(?:^|\s)x\s*(\d+)\s+0x([0-9a-fA-F]+)"
)
SPIKE_FPR_WRITE = re.compile(
    r"(?:^|\s)f\s*(\d+)\s+0x([0-9a-fA-F]+)"
)


@dataclass(frozen=True)
class Commit:
    privilege: int
    pc: int
    instruction: int
    next_pc: int | None
    register: int | None
    value: int | None
    floating_register: bool = False


def parse_dut_line(line: str, xlen: int = 32) -> Commit | None:
    dut_prefix = f"RV{xlen}TRACE "
    if not line.startswith(dut_prefix):
        return None

    fields = line.split()
    if len(fields) != 6:
        raise ValueError(f"malformed DUT trace line: {line}")

    register = None
    value = None
    floating_register = False
    if fields[5] != "-":
        match = re.fullmatch(
            rf"([xf])(\d+)=([0-9a-fA-F]{{1,{xlen // 4}}})",
            fields[5],
        )
        if match is None:
            raise ValueError(f"malformed DUT register write: {line}")
        floating_register = match.group(1) == "f"
        register = int(match.group(2), 10)
        value = int(match.group(3), 16)

    return Commit(
        privilege=int(fields[1], 10),
        pc=int(fields[2], 16),
        instruction=int(fields[3], 16),
        next_pc=int(fields[4], 16),
        register=register,
        value=value,
        floating_register=floating_register,
    )


def parse_spike_line(line: str, xlen: int = 32) -> Commit | None:
    match = SPIKE_RECORD.match(line)
    if match is None:
        return None

    register = None
    value = None
    floating_register = False
    register_match = SPIKE_GPR_WRITE.search(match.group(4))
    if register_match is not None:
        parsed_register = int(register_match.group(1), 10)
        if parsed_register != 0:
            register = parsed_register
            value = int(register_match.group(2), 16) & ((1 << xlen) - 1)
    else:
        register_match = SPIKE_FPR_WRITE.search(match.group(4))
        if register_match is not None:
            floating_register = True
            register = int(register_match.group(1), 10)
            value = int(register_match.group(2), 16) & ((1 << xlen) - 1)

    return Commit(
        privilege=int(match.group(1), 10),
        pc=int(match.group(2), 16) & ((1 << xlen) - 1),
        instruction=int(match.group(3), 16),
        next_pc=None,
        register=register,
        value=value,
        floating_register=floating_register,
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
            actual.floating_register,
        )
        expected_architecture = (
            expected.privilege,
            expected.pc,
            expected.instruction,
            expected.register,
            expected.value,
            expected.floating_register,
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
    dut64_line = (
        "RV64TRACE 3 0000000080000000 022081b3 "
        "0000000080000004 x3=fffffffffffffffe"
    )
    spike64_line = (
        "core   0: 3 0x0000000080000000 (0x022081b3) "
        "x3  0xfffffffffffffffe"
    )
    dut64_float_line = (
        "RV64TRACE 0 000000008000015e 00b576d3 "
        "0000000080000162 f13=ffffffff40600000"
    )
    spike64_float_line = (
        "core   0: 0 0x000000008000015e (0x00b576d3) "
        "f13 0xffffffff40600000"
    )

    dut = parse_dut_line(dut_line, 32)
    spike = parse_spike_line(spike_line, 32)
    compressed = parse_spike_line(compressed_line, 32)
    dut64 = parse_dut_line(dut64_line, 64)
    spike64 = parse_spike_line(spike64_line, 64)
    dut64_float = parse_dut_line(dut64_float_line, 64)
    spike64_float = parse_spike_line(spike64_float_line, 64)
    if (
        dut is None
        or spike is None
        or compressed is None
        or dut64 is None
        or spike64 is None
        or dut64_float is None
        or spike64_float is None
        or dut.privilege != 3
        or dut.pc != 0x80000000
        or dut.register != 1
        or dut.value != 1
        or spike.instruction != 0x00000093
        or compressed.instruction != 0x0085
        or dut64.value != 0xFFFFFFFFFFFFFFFE
        or spike64.value != 0xFFFFFFFFFFFFFFFE
        or not dut64_float.floating_register
        or not spike64_float.floating_register
        or dut64_float.register != 13
        or spike64_float.value != 0xFFFFFFFF40600000
    ):
        print("Spike trace parser self-test failed", file=sys.stderr)
        return 1
    if compare_records([dut], [spike]) is not None:
        print("equal trace comparison self-test failed", file=sys.stderr)
        return 1
    if compare_records([dut64], [spike64]) is not None:
        print("RV64 trace comparison self-test failed", file=sys.stderr)
        return 1
    if compare_records([dut64_float], [spike64_float]) is not None:
        print("RV64 floating trace comparison self-test failed", file=sys.stderr)
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
    parser.add_argument(
        "--dut-binary",
        type=Path,
        help=(
            "optional DUT-native path for the raw binary when the script "
            "runs through WSL"
        ),
    )
    parser.add_argument("--elf", type=Path)
    parser.add_argument("--spike", type=Path)
    parser.add_argument("--base", type=lambda value: int(value, 0),
                        default=0x80000000)
    parser.add_argument("--size", type=lambda value: int(value, 0),
                        default=0x00100000)
    parser.add_argument("--timeout", type=float, default=30.0)
    parser.add_argument("--xlen", type=int, choices=(32, 64), default=32)
    parser.add_argument("--isa")
    parser.add_argument("--priv", default="msu")
    parser.add_argument(
        "--reference-dut",
        action="store_true",
        help="force the DUT architecture runner to use its reference path",
    )
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

    dut_command = [str(args.dut), "--trace"]
    if args.reference_dut:
        dut_command.append("--reference")
    dut_command.append(str(args.dut_binary or args.binary))
    dut_output = run_command(dut_command, args.timeout)
    spike_output = run_command(
        [
            str(args.spike),
            f"--isa={args.isa or ('rv32imac_zicsr_zifencei' if args.xlen == 32 else 'rv64im')}",
            f"--priv={args.priv}",
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
        lambda line: parse_dut_line(line, args.xlen),
        args.base,
        args.size,
    )
    spike_records = parse_records(
        spike_output,
        lambda line: parse_spike_line(line, args.xlen),
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
