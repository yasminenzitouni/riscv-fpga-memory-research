#!/usr/bin/env python3
"""Decode a Vivado AXI ILA CSV into a readable channel-event timeline.

This script is intentionally small and explicit.  It does not decide whether a
litmus outcome is "real"; it only extracts the AXI handshakes that matter:

  AWVALID && AWREADY  -> write address accepted
  WVALID  && WREADY   -> write data accepted
  BVALID  && BREADY   -> write response accepted
  ARVALID && ARREADY  -> read address accepted
  RVALID  && RREADY   -> read data accepted

Usage:
  python decode_axi_ila.py path/to/ila.csv
  python decode_axi_ila.py path/to/ila.csv --all-slots --out-dir generated

Address/data probes must use HEX. A radix row is validated when present;
legacy files without one are interpreted as HEX, never auto-detected.
"""

from __future__ import annotations

import argparse
import csv
import re
from pathlib import Path


SIGNALS = [
    "awaddr",
    "awvalid",
    "awready",
    "wdata",
    "wvalid",
    "wready",
    "bvalid",
    "bready",
    "araddr",
    "arvalid",
    "arready",
    "rdata",
    "rvalid",
    "rready",
    "trigger",
]

DEFAULT_SLOT_LABELS = {
    0: "sc_test_M00_AXI",
    1: "mbv_0_M_AXI_DC",
    2: "mbv_1_M_AXI_DP",
    3: "sc_uncached_M00_AXI",
}


def normalize_name(name: str) -> str:
    return re.sub(r"[^a-z0-9]", "", name.lower())


def find_column(headers: list[str], signal_name: str) -> int | None:
    wanted = normalize_name(signal_name)
    if wanted == "sampleinbuffer":
        for index, header in enumerate(headers):
            if "sample in buffer" in header.lower():
                return index
        return None

    if wanted != "trigger":
        # Vivado headers usually contain names such as:
        #   .../net_slot_0_axi_awvalid
        # Match the complete signal token so wvalid never matches awvalid.
        token_pattern = re.compile(rf"(?:^|[^a-z0-9])axi_?{re.escape(wanted)}(?:$|[^a-z0-9])")
        compact_token_pattern = re.compile(rf"(?:^|[^a-z0-9])axi{re.escape(wanted)}(?:$|[^a-z0-9])")
        for index, header in enumerate(headers):
            lower_header = header.lower()
            compact_header = normalize_name(lower_header)
            if token_pattern.search(lower_header) or compact_token_pattern.search(compact_header):
                return index
        return None

    for index, header in enumerate(headers):
        compact = normalize_name(header)
        if wanted in compact:
            return index
    return None


def find_slot_column(headers: list[str], slot: int, signal_name: str) -> int | None:
    if signal_name == "trigger":
        return find_column(headers, signal_name)

    token = f"net_slot_{slot}_axi_{signal_name}".lower()
    compact_token = normalize_name(token)
    for index, header in enumerate(headers):
        lower_header = header.lower()
        if token in lower_header or compact_token in normalize_name(lower_header):
            return index
    return None


def discover_slots(headers: list[str]) -> list[int]:
    slots: set[int] = set()
    for header in headers:
        match = re.search(r"net_slot_(\d+)_axi_", header.lower())
        if match:
            slots.add(int(match.group(1)))
    return sorted(slots)


def parse_slot_labels(value: str | None) -> dict[int, str]:
    labels = dict(DEFAULT_SLOT_LABELS)
    if value is None or value.strip() == "":
        return labels
    presets = {
        "t3-fullila": {
            0: "sc_test_M00_AXI",
            1: "mbv_0_M_AXI_DC",
            2: "mbv_1_M_AXI_DP",
            3: "sc_uncached_M00_AXI",
        },
        "t4c-wrc-fullila": {
            0: "mbv_0_M_AXI_DC",
            1: "sc_test_M00_AXI",
            2: "sc_uncached_M00_AXI",
            3: "mbv_2_M_AXI_DP",
        },
        "t3wbatg-fullila6": {
            0: "sc_test_M00_AXI",
            1: "mbv_0_M_AXI_DC",
            2: "mbv_0_M_AXI_DP",
            3: "mbv_1_M_AXI_DP",
            4: "sc_uncached_M00_AXI",
            5: "atg_noise_M_AXI",
        },
    }
    if value in presets:
        labels.update(presets[value])
        return labels
    for item in value.split(","):
        if "=" not in item:
            raise SystemExit(
                "--slot-labels must be a preset name or comma-separated N=label entries"
            )
        slot_text, label = item.split("=", 1)
        labels[int(slot_text.strip())] = label.strip()
    return labels


def cell(row: list[str], index: int | None) -> str:
    if index is None or index >= len(row):
        return ""
    return row[index].strip()


def is_one(value: str) -> bool:
    return value.strip().lower() in {"1", "1'b1", "true"}


def normalize_hex(value: str) -> str:
    cleaned = value.strip().replace("_", "")
    if cleaned == "":
        return ""
    if cleaned.lower().startswith("0x"):
        cleaned = cleaned[2:]
    if cleaned and all(ch in "0123456789abcdefABCDEF" for ch in cleaned):
        return "0x" + cleaned.upper().zfill(4)
    raise SystemExit(f"Unsupported hexadecimal address/data value {value!r}; expected HEX digits (optional 0x prefix).")


def validate_radices(headers: list[str], radices: list[str]) -> None:
    """Validate every consumed column, including probes from later slots."""
    for index, header in enumerate(headers):
        radix = cell(radices, index).upper()
        if find_column([header], "Sample in Buffer") is not None:
            if radix.removeprefix("RADIX -").strip() != "UNSIGNED":
                raise SystemExit(f"Unsupported sample radix {radix!r} for {header!r}; expected UNSIGNED decimal.")
            continue
        signal = next((name for name in SIGNALS if find_column([header], name) is not None), None)
        if signal is None:
            continue
        allowed = {"HEX"} if signal in {"awaddr", "wdata", "araddr", "rdata"} else {"HEX", "BINARY", "UNSIGNED"}
        if radix not in allowed:
            raise SystemExit(
                f"Unsupported radix {radix or '(missing)'!r} for {header!r}; "
                f"expected {'/'.join(sorted(allowed))}. Export address/data probes as HEX."
            )


def read_vivado_csv(path: Path) -> tuple[list[str], list[list[str]]]:
    """Return headers/rows; no-radix legacy address/data fields must be HEX."""
    with path.open(newline="") as handle:
        rows = list(csv.reader(handle))
    if len(rows) < 2:
        raise SystemExit(f"CSV is too short: {path}")

    headers = rows[0]
    data_rows = rows[1:]

    # Validate metadata before removing it; the two-item API stays unchanged.
    if data_rows and data_rows[0] and data_rows[0][0].strip().lower().startswith("radix"):
        validate_radices(headers, data_rows[0])
        data_rows = data_rows[1:]

    return headers, data_rows


def decode_events(headers: list[str], rows: list[list[str]]) -> tuple[dict[str, int | None], list[dict[str, str]]]:
    if len(discover_slots(headers)) > 1:
        raise SystemExit("Multiple AXI slots found; use --all-slots to keep each interface's signals separate.")
    columns = {name: find_column(headers, name) for name in SIGNALS}
    sample_column = find_column(headers, "Sample in Buffer")

    events: list[dict[str, str]] = []
    for row in rows:
        sample = cell(row, sample_column)
        if sample == "":
            continue

        trigger = is_one(cell(row, columns["trigger"]))
        aw = is_one(cell(row, columns["awvalid"])) and is_one(cell(row, columns["awready"]))
        w = is_one(cell(row, columns["wvalid"])) and is_one(cell(row, columns["wready"]))
        b = is_one(cell(row, columns["bvalid"])) and (
            columns["bready"] is None or is_one(cell(row, columns["bready"]))
        )
        ar = is_one(cell(row, columns["arvalid"])) and is_one(cell(row, columns["arready"]))
        r = is_one(cell(row, columns["rvalid"])) and is_one(cell(row, columns["rready"]))

        if trigger:
            events.append({
                "sample": sample,
                "kind": "TRIGGER",
                "address": "",
                "data": "",
                "meaning": "ILA trigger point",
            })
        if aw:
            events.append({
                "sample": sample,
                "kind": "AW",
                "address": normalize_hex(cell(row, columns["awaddr"])),
                "data": "",
                "meaning": "write address accepted",
            })
        if w:
            events.append({
                "sample": sample,
                "kind": "W",
                "address": "",
                "data": normalize_hex(cell(row, columns["wdata"])),
                "meaning": "write data accepted",
            })
        if b:
            events.append({
                "sample": sample,
                "kind": "B",
                "address": "",
                "data": "",
                "meaning": ("write response accepted" if columns["bready"] is not None else
                            "write response observed (BREADY absent; acceptance assumed)"),
            })
        if ar:
            events.append({
                "sample": sample,
                "kind": "AR",
                "address": normalize_hex(cell(row, columns["araddr"])),
                "data": "",
                "meaning": "read address accepted",
            })
        if r:
            events.append({
                "sample": sample,
                "kind": "R",
                "address": "",
                "data": normalize_hex(cell(row, columns["rdata"])),
                "meaning": "read data accepted",
            })

    return columns, events


def decode_slot_events(
    headers: list[str], rows: list[list[str]], slot: int, slot_labels: dict[int, str]
) -> tuple[dict[str, int | None], list[dict[str, str]]]:
    columns = {name: find_slot_column(headers, slot, name) for name in SIGNALS}
    sample_column = find_column(headers, "Sample in Buffer")

    events: list[dict[str, str]] = []
    for row in rows:
        sample = cell(row, sample_column)
        if sample == "":
            continue

        trigger = is_one(cell(row, columns["trigger"]))
        aw = is_one(cell(row, columns["awvalid"])) and is_one(cell(row, columns["awready"]))
        w = is_one(cell(row, columns["wvalid"])) and is_one(cell(row, columns["wready"]))
        b = is_one(cell(row, columns["bvalid"])) and (
            columns["bready"] is None or is_one(cell(row, columns["bready"]))
        )
        ar = is_one(cell(row, columns["arvalid"])) and is_one(cell(row, columns["arready"]))
        r = is_one(cell(row, columns["rvalid"])) and is_one(cell(row, columns["rready"]))

        prefix = f"slot{slot}:{slot_labels.get(slot, 'unknown')}"
        if trigger and slot == 0:
            events.append({
                "sample": sample,
                "kind": "TRIGGER",
                "address": "",
                "data": "",
                "meaning": "ILA trigger point",
            })
        if aw:
            events.append({
                "sample": sample,
                "kind": "AW",
                "address": normalize_hex(cell(row, columns["awaddr"])),
                "data": "",
                "meaning": f"{prefix} write address accepted",
            })
        if w:
            events.append({
                "sample": sample,
                "kind": "W",
                "address": "",
                "data": normalize_hex(cell(row, columns["wdata"])),
                "meaning": f"{prefix} write data accepted",
            })
        if b:
            events.append({
                "sample": sample,
                "kind": "B",
                "address": "",
                "data": "",
                "meaning": (f"{prefix} write response accepted" if columns["bready"] is not None else
                            f"{prefix} write response observed (BREADY absent; acceptance assumed)"),
            })
        if ar:
            events.append({
                "sample": sample,
                "kind": "AR",
                "address": normalize_hex(cell(row, columns["araddr"])),
                "data": "",
                "meaning": f"{prefix} read address accepted",
            })
        if r:
            events.append({
                "sample": sample,
                "kind": "R",
                "address": "",
                "data": normalize_hex(cell(row, columns["rdata"])),
                "meaning": f"{prefix} read data accepted",
            })

    return columns, events


def write_event_csv(path: Path, events: list[dict[str, str]]) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["sample", "kind", "address", "data", "meaning"])
        writer.writeheader()
        writer.writerows(events)


def write_slot_event_csv(path: Path, events: list[dict[str, str]]) -> None:
    with path.open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["sample", "slot", "kind", "address", "data", "meaning"])
        writer.writeheader()
        for event in events:
            writer.writerow(event)


def write_markdown(path: Path, source: Path, columns: dict[str, int | None], events: list[dict[str, str]]) -> None:
    lines: list[str] = []
    lines.append("# AXI ILA Decode")
    lines.append("")
    lines.append(f"Source CSV: `{source}`")
    lines.append("")
    lines.append("This file records AXI channel events from the raw Vivado ILA CSV. Events are not paired into complete transactions.")
    lines.append("Address/data probes require HEX; legacy input without radix metadata is interpreted as HEX.")
    lines.append("")
    lines.append("## Handshake Rules")
    lines.append("")
    lines.append("- `AWVALID && AWREADY`: write address accepted")
    lines.append("- `WVALID && WREADY`: write data accepted")
    lines.append("- `BVALID && BREADY`: write response accepted")
    lines.append("- `ARVALID && ARREADY`: read address accepted")
    lines.append("- `RVALID && RREADY`: read data accepted")
    if columns.get("bvalid") is not None and columns.get("bready") is None:
        lines.append("- Assumption: BREADY was not captured; BVALID observations assume acceptance and do not prove a response handshake.")
    lines.append("")
    lines.append("## Columns Used")
    lines.append("")
    for name in SIGNALS:
        index = columns.get(name)
        if index is not None:
            lines.append(f"- `{name}`: raw CSV column {index}")
    lines.append("")
    lines.append("## Channel-event Timeline")
    lines.append("")
    lines.append("| Sample | Event | Address | Data | Meaning |")
    lines.append("|---:|---|---|---|---|")
    for event in events:
        lines.append(
            f"| {event['sample']} | {event['kind']} | {event['address']} | {event['data']} | {event['meaning']} |"
        )
    lines.append("")
    lines.append("## Interpretation notes")
    lines.append("")
    lines.append(
        "The raw ILA export has many columns because it records every AXI wire. "
        "This decode records independent write-channel (`AW`, `W`, `B`) and "
        "read-channel (`AR`, `R`) events. It does not match complete transactions. "
        "Litmus interpretation also requires software results and cache context."
    )
    path.write_text("\n".join(lines) + "\n")


def write_all_slots_markdown(
    path: Path,
    source: Path,
    slots: list[int],
    columns_by_slot: dict[int, dict[str, int | None]],
    events: list[dict[str, str]],
    slot_labels: dict[int, str],
) -> None:
    lines: list[str] = []
    lines.append("# AXI ILA Decode - All Slots")
    lines.append("")
    lines.append(f"Source CSV: `{source}`")
    lines.append("")
    lines.append("This decode keeps AXI handshakes from every `net_slot_N_axi_*` monitor in one shared sample timeline.")
    lines.append("Events are not paired into complete transactions. Address/data probes require HEX; legacy input without radix metadata is interpreted as HEX.")
    lines.append("")
    lines.append("## Slots")
    lines.append("")
    for slot in slots:
        lines.append(f"- `slot{slot}`: `{slot_labels.get(slot, 'unknown')}`")
    lines.append("")
    lines.append("## Columns Used")
    lines.append("")
    for slot in slots:
        lines.append(f"### slot{slot}: {slot_labels.get(slot, 'unknown')}")
        for name in SIGNALS:
            index = columns_by_slot[slot].get(name)
            if index is not None:
                lines.append(f"- `{name}`: raw CSV column {index}")
        if columns_by_slot[slot].get("bvalid") is not None and columns_by_slot[slot].get("bready") is None:
            lines.append("- Assumption: BREADY was not captured; BVALID observations assume acceptance and do not prove a response handshake.")
        lines.append("")
    lines.append("## Channel-event Timeline")
    lines.append("")
    lines.append("| Sample | Slot | Event | Address | Data | Meaning |")
    lines.append("|---:|---|---|---|---|---|")
    for event in events:
        lines.append(
            f"| {event['sample']} | {event['slot']} | {event['kind']} | "
            f"{event['address']} | {event['data']} | {event['meaning']} |"
        )
    lines.append("")
    lines.append("## Interpretation notes")
    lines.append("")
    lines.append(
        "All rows come from one ILA capture, so equal sample numbers share one trigger/timebase. "
        "Interpreting a stale read requires a valid request/response association, "
        "software results and cache context. This script does not establish that "
        "association or prove a memory-model violation. Same-sample display order is not causality."
    )
    path.write_text("\n".join(lines) + "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv_path", type=Path)
    parser.add_argument("--out-dir", type=Path, default=None)
    parser.add_argument("--all-slots", action="store_true", help="Decode every net_slot_N_axi monitor in one timeline.")
    parser.add_argument(
        "--slot-labels",
        default=None,
        help=(
            "Slot labels preset or N=label list. Presets: t3-fullila, "
            "t4c-wrc-fullila, t3wbatg-fullila6. "
            "Example: 0=mbv0,1=sc_test"
        ),
    )
    args = parser.parse_args()

    headers, rows = read_vivado_csv(args.csv_path)
    slot_labels = parse_slot_labels(args.slot_labels)
    if args.all_slots:
        slots = discover_slots(headers)
        if not slots:
            raise SystemExit("No net_slot_N_axi probes found in CSV.")
        columns_by_slot: dict[int, dict[str, int | None]] = {}
        all_events: list[dict[str, str]] = []
        for slot in slots:
            columns, events = decode_slot_events(headers, rows, slot, slot_labels)
            columns_by_slot[slot] = columns
            for event in events:
                all_events.append({"slot": f"slot{slot}", **event})
        all_events.sort(key=lambda event: (int(event["sample"]), event["slot"], event["kind"]))

        if args.out_dir is None:
            for event in all_events:
                print(
                    f"{event['sample']:>6}  {event['slot']:<6} {event['kind']:<7} "
                    f"{event['address']:<10} {event['data']:<12} {event['meaning']}"
                )
            return

        args.out_dir.mkdir(parents=True, exist_ok=True)
        stem = args.csv_path.stem
        event_csv = args.out_dir / f"{stem}_decoded_all_slots_events.csv"
        markdown = args.out_dir / f"{stem}_decoded_all_slots_explanation.md"
        write_slot_event_csv(event_csv, all_events)
        write_all_slots_markdown(markdown, args.csv_path, slots, columns_by_slot, all_events, slot_labels)
        print(f"decoded all-slot events: {event_csv}")
        print(f"explanation:             {markdown}")
        return

    columns, events = decode_events(headers, rows)

    if args.out_dir is None:
        for event in events:
            print(
                f"{event['sample']:>6}  {event['kind']:<7} "
                f"{event['address']:<8} {event['data']:<12} {event['meaning']}"
            )
        return

    args.out_dir.mkdir(parents=True, exist_ok=True)
    stem = args.csv_path.stem
    event_csv = args.out_dir / f"{stem}_decoded_events.csv"
    markdown = args.out_dir / f"{stem}_decoded_explanation.md"
    write_event_csv(event_csv, events)
    write_markdown(markdown, args.csv_path, columns, events)
    print(f"decoded events: {event_csv}")
    print(f"explanation:    {markdown}")


if __name__ == "__main__":
    main()
