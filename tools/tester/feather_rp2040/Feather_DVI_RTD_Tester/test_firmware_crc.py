#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Exercise CRC verification and programming safeguards without serial hardware."""
import contextlib
import io
import unittest
from unittest.mock import patch
import zlib

import host


class Clock:
    def __init__(self):
        self.now = 0.0

    def sleep(self, seconds):
        self.now += seconds

    def monotonic(self):
        return self.now


class FakeClient:
    def __init__(self, image, clock):
        self.memory = bytearray(image)
        self.clock = clock
        self.synchronized = True
        self.isp_active = False
        self.jedec = "EF3013"
        self.flash_status = 0x1c
        self.port = "FAKE"
        self.commands = []
        self.reads = []
        self.jobs = 0
        self.crc_start_requests = 0
        self.ignore_crc_start = False
        self.requested_regions = []
        self.region_id = 0
        self.reported_region = None
        self.region_maximum = 2
        self.progress_maximum = None
        self.ready_pages = None
        self.crc_supported = True
        self.region_supported = True
        self.queried_codes = []
        self.initial_busy = False
        self.forever_busy = False
        self.bad_crc_job = 0
        self.bad_bank_read = False
        self.bad_sector_read = False
        self.fail_ddc_read = 0
        self.ddc_reads = 0
        self.remaining_busy = 0
        self.state = 0
        self.crc = 0
        self.reply = b""
        self.boot_delay = 0
        self.boot_ready_at = 0

    def hello(self, require_isp=False):
        return {"video": "off", "isp_active": self.isp_active}

    def isp(self):
        self.commands.append("isp")
        self.isp_active = True
        return {"jedec": self.jedec, "size": len(self.memory), "status": self.flash_status}

    def command(self, command, timeout=5):
        self.commands.append(command)
        pieces = command.split()
        if pieces[0] == "erase":
            address = int(pieces[1])
            self.memory[address:address + host.SECTOR_SIZE] = b"\xff" * host.SECTOR_SIZE
        elif pieces[0] == "page":
            address = int(pieces[1])
            data = bytes.fromhex(pieces[2])
            assert len(data) == host.PAGE_SIZE
            self.memory[address:address + len(data)] = data
        elif pieces[0] == "finish":
            self.isp_active = False
        elif pieces[0] == "reset-chip":
            self.state = 0
            self.region_id = 0
            self.boot_ready_at = self.clock.now + self.boot_delay
        return {"ok": True}

    def read_flash(self, size, address=0, progress=None):
        self.reads.append((address, size))
        result = bytearray(self.memory[address:address + size])
        if ((self.bad_bank_read and size == host.BANK_SIZE) or
                (self.bad_sector_read and size == host.SECTOR_SIZE)):
            result[0] ^= 1
        return bytes(result)

    def ddc_write(self, packet):
        if self.clock.now < self.boot_ready_at:
            raise host.TesterError("DDC unavailable during bitmap benchmark")
        assert host.ddc_checksum(packet, 0x6e) == 0
        self.clock.sleep(host.DDC_CI_GAP_SECONDS)
        command, code = packet[2:4]
        if command == 3:
            assert code == 0xf6
            self.crc_start_requests += 1
            if self.ignore_crc_start:
                return  # Bus ACK does not prove that firmware accepted Set VCP.
            self.region_id = int.from_bytes(packet[4:6], "big")
            assert self.region_id in (1, 2)
            self.jobs += 1
            self.requested_regions.append(self.region_id)
            self.remaining_busy = 1
            self.state = 1
            address, size = (0, host.BANK_SIZE) if self.region_id == 1 else (0x10000, 8192)
            self.crc = zlib.crc32(self.memory[address:address + size])
            if self.jobs == self.bad_crc_job:
                self.crc ^= 1
            return
        assert command == 1
        self.queried_codes.append(code)
        result = 0
        if not self.crc_supported or (code == 0xfa and not self.region_supported):
            result, maximum, value = 1, 0, 0
        elif code == 0xf6:
            maximum = 2
            if self.initial_busy and not self.jobs:
                value = 1
            elif self.state == 0:
                value = 0
            elif self.forever_busy or self.remaining_busy:
                value = 1
                self.remaining_busy = max(0, self.remaining_busy - 1)
            else:
                value = 2
            self.state = value
        elif code == 0xf9:
            total_pages = 256 if self.region_id == 1 else 32
            maximum = total_pages if self.progress_maximum is None else self.progress_maximum
            value = total_pages if self.ready_pages is None else self.ready_pages
            if self.state != 2:
                value = 1
        elif code == 0xfa:
            maximum = self.region_maximum
            value = self.region_id if self.reported_region is None else self.reported_region
        else:
            assert code in (0xf7, 0xf8) and self.state == 2
            maximum = 65535
            value = self.crc & 65535 if code == 0xf7 else self.crc >> 16
        reply = bytes((0x6e, 0x88, 2, result, code, 0,
                       maximum >> 8, maximum & 255, value >> 8, value & 255))
        self.reply = reply + bytes((host.ddc_checksum(reply, 0x50),))

    def ddc_read(self, length):
        assert length == 11
        self.ddc_reads += 1
        if self.ddc_reads == self.fail_ddc_read:
            raise host.TesterError("Injected DDC failure")
        self.clock.sleep(host.DDC_CI_GAP_SECONDS)
        return self.reply


class FirmwareCRCTest(unittest.TestCase):
    def setUp(self):
        self.clock = Clock()
        self.sleep = patch.object(host.time, "sleep", self.clock.sleep)
        self.monotonic = patch.object(host.time, "monotonic", self.clock.monotonic)
        self.sleep.start()
        self.monotonic.start()
        self.addCleanup(self.sleep.stop)
        self.addCleanup(self.monotonic.stop)
        self.backup = bytes(range(256)) * 256 + b"\x79" * (host.FULL_IMAGE_SIZE - host.BANK_SIZE)
        changed = bytearray(self.backup)
        changed[17] ^= 0x55
        self.target = bytes(changed)
        self.client = FakeClient(self.backup, self.clock)

    def test_crc_bank_and_full_image_scope(self):
        for image in (self.backup[:host.BANK_SIZE], self.backup):
            result = host.firmware_crc(self.client, image)
            self.assertTrue(result["verified"])
            self.assertEqual(result["size"], host.BANK_SIZE)
            self.assertEqual((result["scope"], result["region_id"], result["address"]),
                             ("bank0", 1, 0))
            self.assertEqual(result["crc32"], f"{zlib.crc32(image[:host.BANK_SIZE]):08x}")
            self.assertFalse(result["tail_verified"])
            self.assertFalse(result["full_image_verified"])
            self.assertEqual(result["region_sha256"], result["bank0_sha256"])
        self.assertFalse(self.client.commands)

    def test_vendor_probe_uses_exact_two_sector_range_from_full_image(self):
        image = bytearray(self.backup)
        for address, value in ((0xffff, 0x01), (0x10000, 0x23), (0x11000, 0x45),
                               (0x11fff, 0x67), (0x12000, 0x89)):
            image[address] = value
        client = FakeClient(image, self.clock)
        # Bank0 and the tail after the requested prefix are outside this check.
        expected = bytearray(image)
        expected[0xffff] ^= 1
        expected[0x12000] ^= 1
        result = host.firmware_crc(client, expected, region="vendor-probe")
        self.assertEqual((result["scope"], result["region_id"], result["address"],
                          result["size"], result["pages"]),
                         ("vendor-probe", 2, 0x10000, 8192, 32))
        self.assertEqual(result["crc32"], f"{zlib.crc32(image[0x10000:0x12000]):08x}")
        self.assertEqual(result["region_sha256"], host.sha256(image[0x10000:0x12000]))
        self.assertNotIn("bank0_sha256", result)
        self.assertTrue(result["verified"])
        self.assertFalse(result["tail_verified"] or result["full_image_verified"])
        self.assertEqual(client.requested_regions, [2])
        self.assertFalse(client.commands or client.reads)

    def test_vendor_probe_detects_changes_in_both_sectors(self):
        for address in (0x10000, 0x11000, 0x11fff):
            with self.subTest(address=address):
                expected = bytearray(self.backup)
                expected[address] ^= 1
                client = FakeClient(self.backup, self.clock)
                with self.assertRaisesRegex(host.TesterError, "vendor-probe CRC mismatch"):
                    host.firmware_crc(client, expected, region="vendor-probe")
                self.assertEqual(client.requested_regions, [2])

    def test_invalid_regions_and_vendor_image_sizes_fail_before_transport(self):
        with self.assertRaisesRegex(host.TesterError, "Unknown.*region"):
            host.read_firmware_crc(self.client, region="whole-flash")
        with self.assertRaisesRegex(host.TesterError, "Unknown.*region"):
            host.firmware_crc(self.client, self.backup, region="whole-flash")
        for size in (8192, host.BANK_SIZE, host.FULL_IMAGE_SIZE - 1):
            with self.subTest(size=size), self.assertRaisesRegex(host.TesterError, "524288-byte"):
                host.firmware_crc(self.client, self.backup[:size], region="vendor-probe")
        self.assertEqual((self.client.jobs, self.client.ddc_reads), (0, 0))

    def test_crc_requires_matching_completed_region(self):
        for region, wrong_region in (("bank0", 2), ("vendor-probe", 1)):
            with self.subTest(region=region):
                client = FakeClient(self.backup, self.clock)
                client.reported_region = wrong_region
                with self.assertRaisesRegex(host.TesterError, "region mismatch"):
                    host.firmware_crc(client, self.backup, region=region)
                self.assertEqual(client.jobs, 1)
                self.assertNotIn(0xf7, client.queried_codes)
                self.assertNotIn(0xf8, client.queried_codes)
        self.client.region_maximum = 3
        with self.assertRaisesRegex(host.TesterError, "region mismatch"):
            host.firmware_crc(self.client, self.backup)

    def test_missing_region_interface_cannot_accept_a_crc_or_retry(self):
        self.client.region_supported = False
        with self.assertRaisesRegex(host.TesterError, "rejected VCP 0xFA"):
            host.firmware_crc(self.client, self.backup)
        self.assertEqual(self.client.jobs, 1)
        self.assertEqual(self.client.queried_codes.count(0xfa), 1)
        self.assertNotIn(0xf7, self.client.queried_codes)
        self.assertNotIn(0xf8, self.client.queried_codes)

    def test_rejected_same_region_restart_cannot_accept_stale_ready_crc(self):
        for region in ("bank0", "vendor-probe"):
            with self.subTest(region=region):
                client = FakeClient(self.backup, self.clock)
                host.firmware_crc(client, self.backup, region=region)
                client.ignore_crc_start = True
                client.queried_codes.clear()
                with self.assertRaisesRegex(host.TesterError, "new job not observed"):
                    host.firmware_crc(client, self.backup, region=region)
                self.assertEqual((client.crc_start_requests, client.jobs), (2, 1))
                self.assertEqual(client.queried_codes, [0xf6, 0xf6])

    def test_fast_preflight_rejects_stale_crc_before_entering_isp(self):
        host.firmware_crc(self.client, self.backup)
        self.client.ignore_crc_start = True
        with self.assertRaisesRegex(host.TesterError, "new job not observed"):
            host.program_flash(self.client, self.target, self.backup, fast=True)
        self.assertEqual((self.client.crc_start_requests, self.client.jobs), (2, 1))
        self.assertFalse(self.client.commands or self.client.reads or self.client.isp_active)

    def test_vendor_progress_must_report_exact_region_size(self):
        self.client.progress_maximum = 256
        with self.assertRaisesRegex(host.TesterError, "invalid.*page progress"):
            host.firmware_crc(self.client, self.backup, region="vendor-probe")
        self.assertEqual(self.client.jobs, 1)
        client = FakeClient(self.backup, self.clock)
        client.ready_pages = 31
        with self.assertRaisesRegex(host.TesterError, "before all 32 vendor-probe pages"):
            host.firmware_crc(client, self.backup, region="vendor-probe")
        self.assertEqual(client.jobs, 1)

    def test_cli_region_default_explicit_choice_and_rejection(self):
        parser = host.argument_parser()
        self.assertEqual(parser.parse_args(["firmware-crc", "full.bin"]).region, "bank0")
        self.assertEqual(parser.parse_args(["firmware-crc", "full.bin", "--region",
                                           "vendor-probe"]).region, "vendor-probe")
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            parser.parse_args(["firmware-crc", "full.bin", "--region", "whole-flash"])

    def test_crc_invalid_size_and_mismatch(self):
        with self.assertRaisesRegex(host.TesterError, "65536-byte"):
            host.firmware_crc(self.client, b"short")
        self.assertEqual(self.client.jobs, 0)
        with self.assertRaisesRegex(host.TesterError, "CRC mismatch"):
            host.firmware_crc(self.client, self.target)
        self.assertEqual(self.client.jobs, 1)

    def test_busy_and_transport_errors_are_not_retried(self):
        self.client.initial_busy = True
        with self.assertRaisesRegex(host.TesterError, "already busy"):
            host.firmware_crc(self.client, self.backup)
        self.assertEqual(self.client.jobs, 0)
        self.client.initial_busy = False
        self.client.state = 0
        self.client.fail_ddc_read = self.client.ddc_reads + 2
        with self.assertRaisesRegex(host.TesterError, "Injected DDC failure"):
            host.firmware_crc(self.client, self.backup)
        self.assertEqual(self.client.jobs, 1)

    def test_crc_timeout_starts_only_one_job(self):
        self.client.forever_busy = True
        with self.assertRaisesRegex(host.TesterError, "timed out"):
            host.firmware_crc(self.client, self.backup, timeout=0.3)
        self.assertEqual(self.client.jobs, 1)

    def test_fast_program_verifies_bank_and_preserves_tail(self):
        result = host.program_flash(self.client, self.target, self.backup, fast=True)
        self.assertEqual(bytes(self.client.memory), self.target)
        self.assertEqual(self.client.reads, [(0, host.BANK_SIZE), (0, host.SECTOR_SIZE)])
        self.assertEqual(result["changed_sectors"], [0])
        self.assertEqual(self.client.jobs, 2)
        self.assertEqual(self.client.requested_regions, [1, 1])
        self.assertEqual(result["verification_scope"], "bank0")
        self.assertEqual(result["verified_bytes"], host.BANK_SIZE)
        self.assertFalse(result["full_image_verified"])
        self.assertFalse(result["tail_readback"])
        self.assertEqual(self.client.commands[-2:], ["finish", "reset-chip"])
        self.assertFalse(self.client.isp_active)

    def test_fast_unchanged_image_skips_writes(self):
        host.firmware_crc(self.client, self.backup, region="vendor-probe")
        result = host.program_flash(self.client, self.backup, self.backup, fast=True)
        self.assertTrue(result["writes_skipped"])
        self.assertEqual(self.client.reads, [(0, host.BANK_SIZE)])
        self.assertEqual(self.client.commands, ["isp", "finish", "reset-chip"])
        self.assertEqual(self.client.requested_regions, [2, 1, 1])

    def test_fast_program_waits_for_startup_artwork_before_live_crc(self):
        self.client.boot_delay = 8
        result = host.program_flash(self.client, self.target, self.backup, fast=True)
        self.assertTrue(result["verified"])
        self.assertEqual(self.client.jobs, 2)
        self.assertEqual(self.client.commands.count("reset-chip"), 1)

    def test_fast_refuses_recovery_sizes_and_changed_tails_before_isp(self):
        with self.assertRaisesRegex(host.TesterError, "--recover"):
            host.program_flash(self.client, self.backup, self.backup, recover=True, fast=True)
        with self.assertRaisesRegex(host.TesterError, "512 KiB"):
            host.program_flash(self.client, b"short", self.backup, fast=True)
        changed_tail = self.backup[:-1] + b"\x78"
        with self.assertRaisesRegex(host.TesterError, "identical.*tails"):
            host.program_flash(self.client, changed_tail, self.backup, fast=True)
        self.assertFalse(self.client.commands)
        self.assertEqual(self.client.jobs, 0)

    def test_fast_requires_live_supported_known_firmware(self):
        self.client.isp_active = True
        with self.assertRaisesRegex(host.TesterError, "ISP inactive"):
            host.program_flash(self.client, self.target, self.backup, fast=True)
        self.client.isp_active = False
        self.client.crc_supported = False
        with self.assertRaisesRegex(host.TesterError, "without --fast"):
            host.program_flash(self.client, self.target, self.backup, fast=True)
        self.assertEqual(self.client.jobs, 0)
        self.client.crc_supported = True
        self.client.bad_crc_job = 1
        with self.assertRaisesRegex(host.TesterError, "neither backup nor target"):
            host.program_flash(self.client, self.target, self.backup, fast=True)
        self.assertFalse(self.client.commands)

    def test_fast_isp_preflight_still_requires_exact_bank(self):
        self.client.bad_bank_read = True
        with self.assertRaisesRegex(host.TesterError, "neither backup nor target"):
            host.program_flash(self.client, self.target, self.backup, fast=True)
        self.assertEqual(self.client.commands, ["isp", "finish"])
        self.assertEqual(bytes(self.client.memory), self.backup)

    def test_fast_requires_known_flash_and_full_protection_before_writes(self):
        for jedec, status in (("EF3013", 0x0c), ("EF3013", 0), ("EF4013", 0x1c)):
            with self.subTest(jedec=jedec, status=status):
                client = FakeClient(self.backup, self.clock)
                client.jedec, client.flash_status = jedec, status
                with self.assertRaisesRegex(host.TesterError, "whole-flash protection"):
                    host.program_flash(client, self.target, self.backup, fast=True)
                self.assertEqual(client.commands, ["isp", "finish"])
                self.assertFalse(client.reads or client.isp_active)
                self.assertEqual(client.jobs, 1)
                self.assertEqual(bytes(client.memory), self.backup)

    def test_sector_readback_failure_keeps_isp_active(self):
        self.client.bad_sector_read = True
        with self.assertRaisesRegex(host.TesterError, "ISP LEFT ACTIVE"):
            host.program_flash(self.client, self.target, self.backup, fast=True)
        self.assertTrue(self.client.isp_active)
        self.assertNotIn("finish", self.client.commands)
        self.assertNotIn("reset-chip", self.client.commands)

    def test_post_reset_crc_mismatch_is_not_retried(self):
        self.client.bad_crc_job = 2
        with self.assertRaisesRegex(host.TesterError, "post-reset live CRC verification failed"):
            host.program_flash(self.client, self.target, self.backup, fast=True)
        self.assertEqual(self.client.jobs, 2)
        self.assertEqual(self.client.commands.count("finish"), 1)
        self.assertEqual(self.client.commands.count("reset-chip"), 1)
        self.assertFalse(self.client.isp_active)

    def test_full_and_recovery_paths_retain_whole_image_checks(self):
        self.client.flash_status = 0x0c
        result = host.program_flash(self.client, self.target, self.backup)
        self.assertEqual(self.client.reads,
                         [(0, host.FULL_IMAGE_SIZE), (0, host.SECTOR_SIZE), (0, host.FULL_IMAGE_SIZE)])
        self.assertTrue(result["full_image_verified"])
        self.assertTrue(result["tail_readback"])
        self.assertEqual(self.client.jobs, 0)
        self.assertNotIn("reset-chip", self.client.commands)
        recovery = FakeClient(self.target, self.clock)
        recovery.flash_status = 0x0c
        result = host.program_flash(recovery, self.backup, self.backup, recover=True)
        self.assertTrue(result["recovery"] and result["full_image_verified"])
        self.assertEqual(bytes(recovery.memory), self.backup)
        self.assertEqual(recovery.jobs, 0)


if __name__ == "__main__":
    unittest.main()
