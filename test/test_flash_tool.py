"""Host tests for the command-line flash helper's device cleanup paths."""

import importlib.util
import pathlib
import sys
import types
import unittest
from argparse import Namespace
from unittest import mock


def load_flash_tool():
    serial = types.ModuleType("serial")
    serial_tools = types.ModuleType("serial.tools")
    serial_ports = types.ModuleType("serial.tools.list_ports")
    serial_ports.comports = lambda: []
    serial_tools.list_ports = serial_ports
    serial.tools = serial_tools

    esptool = types.ModuleType("esptool")
    esptool_cmds = types.ModuleType("esptool.cmds")
    for name in ("attach_flash", "detect_chip", "erase_region", "read_flash",
                 "reset_chip", "run_stub", "verify_flash", "write_flash"):
        setattr(esptool_cmds, name, mock.Mock(name=name))
    esptool.cmds = esptool_cmds

    modules = {
        "serial": serial,
        "serial.tools": serial_tools,
        "serial.tools.list_ports": serial_ports,
        "esptool": esptool,
        "esptool.cmds": esptool_cmds,
    }
    path = pathlib.Path(__file__).parents[1] / "tools" / "flash.py"
    spec = importlib.util.spec_from_file_location("flash_tool", path)
    module = importlib.util.module_from_spec(spec)
    with mock.patch.dict(sys.modules, modules):
        spec.loader.exec_module(module)
    return module


class FakeEsp:
    def __init__(self, flash_id):
        self._flash_id = flash_id

    def flash_id(self):
        return self._flash_id

    def get_chip_description(self):
        return "ESP32-C3"

    def read_mac(self):
        return bytes.fromhex("001122334455")


class FlashToolTest(unittest.TestCase):
    def setUp(self):
        self.flash = load_flash_tool()
        self.esp = FakeEsp(0xFFFFFF)
        self.flash.run_stub.return_value = self.esp

    def test_info_resets_after_an_invalid_flash_id(self):
        with mock.patch.object(self.flash, "connect", return_value=self.esp), \
                mock.patch.object(self.flash, "finish") as finish:
            self.flash.cmd_info(Namespace())

        finish.assert_called_once_with(self.esp)

    def test_backup_resets_before_rejecting_an_unknown_size(self):
        args = Namespace(output="unused-backup.bin")
        with mock.patch.object(self.flash.os.path, "exists", return_value=False), \
                mock.patch.object(self.flash, "connect", return_value=self.esp), \
                mock.patch.object(self.flash, "finish") as finish:
            with self.assertRaisesRegex(SystemExit, "Could not determine"):
                self.flash.cmd_backup(args)

        finish.assert_called_once_with(self.esp)
        self.flash.read_flash.assert_not_called()

    def test_set_latch_hold_changes_only_bit_7(self):
        regs = {0x600080D4: 0x80000105}
        esp = mock.Mock()
        esp.read_reg.side_effect = lambda a: regs[a]
        esp.write_reg.side_effect = lambda a, v: regs.__setitem__(a, v)
        self.flash.set_latch_hold(esp, True)
        self.assertEqual(regs[0x600080D4], 0x80000185)
        self.flash.set_latch_hold(esp, False)
        self.assertEqual(regs[0x600080D4], 0x80000105)

    def test_finish_sets_the_hold_before_the_reset(self):
        calls = []
        with mock.patch.object(self.flash, "set_latch_hold",
                               side_effect=lambda esp, on: calls.append(("hold", on))), \
                mock.patch.object(self.flash, "reset_chip",
                                  side_effect=lambda esp, how: calls.append(("reset", how))):
            self.flash.finish(self.esp, latch_hold=True)
            self.flash.finish(self.esp, latch_hold=False)
            self.flash.finish(self.esp)
        self.assertEqual(calls, [("hold", True), ("reset", "hard-reset"),
                                 ("hold", False), ("reset", "hard-reset"),
                                 ("reset", "hard-reset")])

    def test_flash_and_restore_choose_the_hold(self):
        image = bytes([0xE9]) + bytes(0x8000 - 1) + b"\xaa\x50" + bytes(self.flash.FLASH_SIZE - 0x8002)
        esp = FakeEsp(0x164020)
        self.flash.run_stub.return_value = esp
        with mock.patch.object(self.flash, "connect", return_value=esp), \
                mock.patch.object(self.flash, "check_image", return_value=image), \
                mock.patch.object(self.flash, "finish") as finish:
            self.flash.cmd_restore(Namespace(image="backup.bin"))
            finish.assert_called_once_with(esp, latch_hold=False)
            finish.reset_mock()
            with mock.patch.object(self.flash, "build_images", return_value=[(0x8000, "pt.bin")]), \
                    mock.patch("builtins.open", mock.mock_open(read_data=b"\xaa\x50")):
                self.flash.read_flash.return_value = b"\xaa\x50"
                self.flash.cmd_flash(Namespace(backup="backup.bin", build="build"))
            finish.assert_called_once_with(esp, latch_hold=True)


if __name__ == "__main__":
    unittest.main()
