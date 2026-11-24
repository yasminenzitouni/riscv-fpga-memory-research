"""Synthetic parser and CLI tests for the AXI capture decoder."""
from pathlib import Path
import csv
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
import decode_axi_ila as decoder


def headers_for(slot):
    return [f"capture/net_slot_{slot}_axi_{name}[31:0]" for name in decoder.SIGNALS if name != "trigger"]


def row_for(headers, sample="0", **signals):
    row = ["0"] * len(headers)
    row[0] = sample
    for name, value in signals.items():
        row[headers.index(name)] = str(value)
    return row


class ColumnTests(unittest.TestCase):
    def test_wvalid_is_not_awvalid(self):
        headers = ["capture/net_slot_0_axi_awvalid[0:0]", "capture/net_slot_0_axi_wvalid[0:0]"]
        self.assertEqual(decoder.find_column(headers, "awvalid"), 0)
        self.assertEqual(decoder.find_column(headers, "wvalid"), 1)
        self.assertIsNone(decoder.find_column(headers[:1], "wvalid"))

    def test_slot_discovery_and_selection(self):
        headers = headers_for(10) + headers_for(1)
        self.assertEqual(decoder.discover_slots(headers), [1, 10])
        self.assertEqual(decoder.find_slot_column(headers, 1, "awaddr"), 14)
        self.assertIsNone(decoder.find_slot_column(headers, 2, "awaddr"))

    def test_custom_slot_labels_preserve_defaults(self):
        labels = decoder.parse_slot_labels("0=writer,5=noise")
        self.assertEqual(labels[0], "writer")
        self.assertEqual(labels[5], "noise")
        self.assertIn(1, labels)

    def test_historical_six_slot_profile(self):
        labels = decoder.parse_slot_labels("t3wbatg-fullila6")
        self.assertEqual(labels[5], "atg_noise_M_AXI")
        self.assertEqual(labels[3], "mbv_1_M_AXI_DP")

    def test_invalid_label_argument_is_rejected(self):
        with self.assertRaises(SystemExit):
            decoder.parse_slot_labels("unknown-profile")


class EventTests(unittest.TestCase):
    def setUp(self):
        self.headers = ["Sample in Buffer"] + headers_for(0) + ["TRIGGER"]

    def make(self, sample="0", **signals):
        names = {f"capture/net_slot_0_axi_{name}[31:0]": str(value) for name, value in signals.items() if name != "TRIGGER"}
        if "TRIGGER" in signals:
            names["TRIGGER"] = str(signals["TRIGGER"])
        return row_for(self.headers, sample=sample, **names)

    def test_backpressure_does_not_emit_an_acceptance(self):
        rows = [self.make(awvalid=1, awready=0), self.make(awvalid=0, awready=1), self.make(awvalid=1, awready=1, awaddr="C2000000")]
        _, events = decoder.decode_events(self.headers, rows)
        self.assertEqual([(event["kind"], event["address"]) for event in events], [("AW", "0xC2000000")])

    def test_independent_channels_on_different_samples(self):
        rows = [self.make(sample="3", awvalid=1, awready=1, awaddr="C2000000"), self.make(sample="5", wvalid=1, wready=1, wdata="00000001"), self.make(sample="8", bvalid=1, bready=1)]
        _, events = decoder.decode_events(self.headers, rows)
        self.assertEqual([(event["sample"], event["kind"]) for event in events], [("3", "AW"), ("5", "W"), ("8", "B")])
        # No address is invented for W or B, and the decoder does not pair transactions.
        self.assertEqual(events[1]["address"], "")

    def test_read_response_and_trigger_are_separate_events(self):
        _, events = decoder.decode_events(self.headers, [self.make(sample="9", arvalid=1, arready=1, araddr="C2000004", rvalid=1, rready=1, rdata="0", TRIGGER=1)])
        self.assertEqual([event["kind"] for event in events], ["TRIGGER", "AR", "R"])
        self.assertEqual(events[-1]["data"], "0x0000")

    def test_explicit_bready_low_blocks_response(self):
        _, events = decoder.decode_events(self.headers, [self.make(bvalid=1, bready=0)])
        self.assertEqual(events, [])

    def test_absent_bready_uses_documented_ready_assumption(self):
        column = decoder.find_column(self.headers, "bready")
        headers = [item for index, item in enumerate(self.headers) if index != column]
        row = [item for index, item in enumerate(self.make(bvalid=1)) if index != column]
        _, events = decoder.decode_events(headers, [row])
        self.assertEqual([event["kind"] for event in events], ["B"])

    def test_blank_sample_is_skipped(self):
        _, events = decoder.decode_events(self.headers, [self.make(sample="", awvalid=1, awready=1)])
        self.assertEqual(events, [])

    def test_multislot_decode_uses_each_slots_signals(self):
        headers = ["Sample in Buffer"] + headers_for(0) + headers_for(1)
        row = row_for(headers, sample="12", **{"capture/net_slot_1_axi_arvalid[31:0]":1, "capture/net_slot_1_axi_arready[31:0]":1, "capture/net_slot_1_axi_araddr[31:0]":"C2000004"})
        _, first = decoder.decode_slot_events(headers, [row], 0, {0:"writer",1:"reader"})
        _, second = decoder.decode_slot_events(headers, [row], 1, {0:"writer",1:"reader"})
        self.assertEqual(first, [])
        self.assertEqual(second[0]["kind"], "AR")
        self.assertIn("slot1:reader", second[0]["meaning"])


class CsvAndCliTests(unittest.TestCase):
    def test_radix_row_is_removed(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/"radix.csv"
            path.write_text("Sample in Buffer,net_slot_0_axi_awvalid\nRadix - UNSIGNED,HEX\n0,1\n", encoding="utf-8")
            headers, rows = decoder.read_vivado_csv(path)
        self.assertEqual(headers[0], "Sample in Buffer")
        self.assertEqual(rows, [["0", "1"]])

    def test_short_csv_is_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)/"short.csv"
            path.write_text("Sample in Buffer\n", encoding="utf-8")
            with self.assertRaises(SystemExit):
                decoder.read_vivado_csv(path)

    def test_cli_creates_expected_synthetic_timeline(self):
        with tempfile.TemporaryDirectory() as temp:
            result = subprocess.run([sys.executable, str(ROOT/"decode_axi_ila.py"), "synthetic_capture.csv", "--all-slots", "--slot-labels", "0=writer,1=reader", "--out-dir", temp], cwd=ROOT, text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            with (Path(temp)/"synthetic_capture_decoded_all_slots_events.csv").open(newline="") as handle:
                rows = list(csv.DictReader(handle))
            self.assertEqual([(r["sample"],r["slot"],r["kind"]) for r in rows], [("1","slot0","AW"),("2","slot0","W"),("3","slot0","B"),("4","slot0","TRIGGER"),("4","slot1","AR"),("6","slot1","R")])
            explanation = (Path(temp)/"synthetic_capture_decoded_all_slots_explanation.md").read_text()
            self.assertIn("one shared sample timeline", explanation)


if __name__ == "__main__":
    unittest.main()
