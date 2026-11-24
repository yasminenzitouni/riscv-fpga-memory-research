"""Regression tests for input validation, monitor separation and channel events."""
from pathlib import Path
import csv
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
import decode_axi_ila as decoder


class CorrectionTests(unittest.TestCase):
    def setUp(self):
        (ROOT / "tmp").mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / "tmp")
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)

    def capture(self, names, values, radices=None, sample="0"):
        headers = ["Sample in Buffer"] + [f"net_slot_0_axi_{name}" for name in names]
        rows = [headers]
        if radices is not None:
            rows.append(["Radix - UNSIGNED"] + list(radices))
        rows.append([sample] + list(values))
        path = self.work / "capture.csv"
        with path.open("w", newline="", encoding="utf-8") as handle:
            csv.writer(handle).writerows(rows)
        return path

    def cli(self, path, *options):
        env = dict(os.environ, PYTHONDONTWRITEBYTECODE="1", TEMP=str(ROOT / "tmp"), TMP=str(ROOT / "tmp"))
        return subprocess.run([sys.executable, "-B", str(ROOT / "decode_axi_ila.py"), str(path), *map(str, options)],
                              cwd=ROOT, capture_output=True, text=True, env=env)

    def assert_radix_rejected(self, field, radix, value="10"):
        path = self.capture([field], [value], [radix])
        with self.assertRaises(SystemExit) as rejected:
            decoder.read_vivado_csv(path)
        message = str(rejected.exception)
        self.assertIn(field, message)
        self.assertIn(radix, message)
        self.assertIn("HEX", message)

    def test_unsigned_address_is_rejected_instead_of_relabelled_hex(self):
        self.assert_radix_rejected("awaddr", "UNSIGNED", "3254779904")

    def test_binary_write_data_is_rejected_instead_of_relabelled_hex(self):
        self.assert_radix_rejected("wdata", "BINARY", "00000010")

    def test_other_payload_fields_reject_unsupported_radices(self):
        for field, radix in (("araddr", "UNSIGNED"), ("rdata", "BINARY"),
                             ("awaddr", "OCTAL"), ("wdata", "SIGNED"), ("rdata", "ASCII")):
            with self.subTest(field=field, radix=radix):
                self.assert_radix_rejected(field, radix)

    def test_radix_validation_checks_each_slot_not_just_first_matching_column(self):
        path = self.work / "two_slots.csv"
        path.write_text("Sample in Buffer,net_slot_0_axi_wdata,net_slot_1_axi_wdata\n"
                        "Radix - UNSIGNED,HEX,BINARY\n0,1,00000010\n", encoding="utf-8")
        with self.assertRaises(SystemExit) as rejected:
            decoder.read_vivado_csv(path)
        self.assertIn("slot_1", str(rejected.exception))
        self.assertIn("BINARY", str(rejected.exception))

    def test_missing_radix_for_payload_is_rejected(self):
        path = self.work / "missing_radix.csv"
        path.write_text("Sample in Buffer,net_slot_0_axi_awaddr\nRadix - UNSIGNED\n0,C2000000\n", encoding="utf-8")
        with self.assertRaises(SystemExit) as rejected:
            decoder.read_vivado_csv(path)
        self.assertIn("awaddr", str(rejected.exception))
        self.assertIn("HEX", str(rejected.exception))

    def test_unsupported_scalar_radix_is_rejected(self):
        self.assert_radix_rejected("awvalid", "SIGNED", "-1")

    def test_sample_radix_must_be_unsigned_decimal(self):
        path = self.work / "hex_sample.csv"
        path.write_text("Sample in Buffer,net_slot_0_axi_awaddr\nRadix - HEX,HEX\n10,C2000000\n", encoding="utf-8")
        with self.assertRaises(SystemExit) as rejected:
            decoder.read_vivado_csv(path)
        self.assertIn("Sample in Buffer", str(rejected.exception))
        self.assertIn("UNSIGNED", str(rejected.exception))

    def test_valid_scalar_radices_preserve_handshake_acceptance(self):
        for radix in ("HEX", "BINARY", "UNSIGNED"):
            with self.subTest(radix=radix):
                path = self.capture(["awaddr", "awvalid", "awready"], ["C2000000", "1", "1"],
                                    ["HEX", radix, radix])
                result = decoder.read_vivado_csv(path)
                self.assertEqual(len(result), 2)
                _, events = decoder.decode_events(*result)
                self.assertEqual([(e["kind"], e["address"]) for e in events], [("AW", "0xC2000000")])

    def test_unused_probe_radix_is_not_mistaken_for_a_consumed_payload(self):
        path = self.capture(["awaddr", "awvalid", "awready", "awlen"], ["C2000000", "1", "1", "3"],
                            ["HEX", "BINARY", "BINARY", "SIGNED"])
        _, events = decoder.decode_events(*decoder.read_vivado_csv(path))
        self.assertEqual([(e["kind"], e["address"]) for e in events], [("AW", "0xC2000000")])

    def test_radix_error_cli_is_clear_and_creates_no_output_artifacts(self):
        path = self.capture(["awaddr", "awvalid", "awready"], ["3254779904", "1", "1"],
                            ["UNSIGNED", "BINARY", "BINARY"])
        output = self.work / "output"
        result = self.cli(path, "--all-slots", "--out-dir", output)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("UNSIGNED", result.stderr)
        self.assertIn("HEX", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertFalse(output.exists())

    def test_legacy_no_radix_address_and_data_use_hexadecimal_contract(self):
        path = self.capture(["awaddr", "awvalid", "awready", "wdata", "wvalid", "wready"],
                            ["C2000000", "1", "1", "10", "1", "1"])
        _, events = decoder.decode_events(*decoder.read_vivado_csv(path))
        self.assertEqual([(e["kind"], e["address"], e["data"]) for e in events],
                         [("AW", "0xC2000000", ""), ("W", "", "0x0010")])

    def test_nonempty_unsupported_hex_payload_is_rejected(self):
        for value in ("XXXX", "32'h0001", "0x", "10.5", "-1"):
            with self.subTest(value=value):
                with self.assertRaises(SystemExit) as rejected:
                    decoder.normalize_hex(value)
                self.assertIn("hex", str(rejected.exception).lower())

    def test_supported_hex_spellings_keep_numeric_output(self):
        for value, expected in (("C2000000", "0xC2000000"), ("0xc2000000", "0xC2000000"),
                                ("00_01", "0x0001"), ("0", "0x0000"), ("", "")):
            with self.subTest(value=value):
                self.assertEqual(decoder.normalize_hex(value), expected)

    def test_single_interface_refuses_multiple_slots_instead_of_mixing_handshake_halves(self):
        headers = ["Sample in Buffer", "net_slot_0_axi_awaddr", "net_slot_0_axi_awvalid",
                   "net_slot_1_axi_awvalid", "net_slot_1_axi_awready"]
        with self.assertRaises(SystemExit) as rejected:
            decoder.decode_events(headers, [["0", "C2000000", "1", "0", "1"]])
        self.assertIn("--all-slots", str(rejected.exception))

    def test_single_interface_cli_refusal_directs_all_slots(self):
        result = self.cli(ROOT / "synthetic_capture.csv")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("--all-slots", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_all_slots_does_not_mix_missing_ready_with_another_slots_ready(self):
        headers = ["Sample in Buffer", "net_slot_0_axi_awaddr", "net_slot_0_axi_awvalid",
                   "net_slot_1_axi_awvalid", "net_slot_1_axi_awready"]
        rows = [["0", "C2000000", "1", "0", "1"]]
        for slot in (0, 1):
            with self.subTest(slot=slot):
                _, events = decoder.decode_slot_events(headers, rows, slot, {})
                self.assertEqual(events, [])

    def test_absent_bready_event_labels_acceptance_as_assumed(self):
        headers = ["Sample in Buffer", "net_slot_0_axi_bvalid"]
        for decode in (lambda: decoder.decode_events(headers, [["0", "1"]]),
                       lambda: decoder.decode_slot_events(headers, [["0", "1"]], 0, {})):
            with self.subTest(decode=decode):
                _, events = decode()
                self.assertEqual([e["kind"] for e in events], ["B"])
                self.assertIn("BREADY", events[0]["meaning"])
                self.assertIn("assum", events[0]["meaning"].lower())

    def test_present_bready_preserves_accepted_meaning(self):
        headers = ["Sample in Buffer", "net_slot_0_axi_bvalid", "net_slot_0_axi_bready"]
        _, events = decoder.decode_events(headers, [["0", "1", "1"]])
        self.assertEqual(events[0]["meaning"], "write response accepted")

    def test_absent_bready_assumption_survives_cli_and_generated_csv(self):
        path = self.capture(["bvalid"], ["1"], ["BINARY"])
        result = self.cli(path, "--all-slots", "--out-dir", self.work / "output")
        self.assertEqual(result.returncode, 0, result.stderr)
        with (self.work / "output" / "capture_decoded_all_slots_events.csv").open(newline="") as handle:
            events = list(csv.DictReader(handle))
        self.assertEqual(len(events), 1)
        self.assertIn("BREADY", events[0]["meaning"])
        self.assertIn("assum", events[0]["meaning"].lower())

    def test_supplied_hexadecimal_fixture_matches_every_expected_csv_field(self):
        result = self.cli(ROOT / "synthetic_capture.csv", "--all-slots", "--slot-labels", "0=writer,1=reader",
                          "--out-dir", self.work / "output")
        self.assertEqual(result.returncode, 0, result.stderr)
        with (ROOT / "expected_events.csv").open(newline="") as handle:
            expected = list(csv.DictReader(handle))
        with (self.work / "output" / "synthetic_capture_decoded_all_slots_events.csv").open(newline="") as handle:
            actual = list(csv.DictReader(handle))
        self.assertEqual(actual, expected)

    def test_supplied_hexadecimal_fixture_also_works_without_radix_row(self):
        with (ROOT / "synthetic_capture.csv").open(newline="") as handle:
            source = list(csv.reader(handle))
        path = self.work / "legacy.csv"
        with path.open("w", newline="") as handle:
            csv.writer(handle).writerows([source[0], *source[2:]])
        result = self.cli(path, "--all-slots", "--slot-labels", "0=writer,1=reader", "--out-dir", self.work / "output")
        self.assertEqual(result.returncode, 0, result.stderr)
        with (ROOT / "expected_events.csv").open(newline="") as handle:
            expected = list(csv.DictReader(handle))
        with (self.work / "output" / "legacy_decoded_all_slots_events.csv").open(newline="") as handle:
            actual = list(csv.DictReader(handle))
        self.assertEqual(actual, expected)


if __name__ == "__main__":
    unittest.main()
