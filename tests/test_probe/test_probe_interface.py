import pytest
import time
from pathlib import Path


EXPECTED_NUM_TAPS = 2
EXPECTED_IDCODES = [0x4BA00477, 0x16469041]

cb_jtag = pytest.importorskip(
    "cb_jtag", reason="Install Python package 'cb-jtag' before running this test."
)


def _read_fw_version_string(header_path: Path) -> str:
    for line in header_path.read_text(encoding="utf-8", errors="ignore").splitlines():
        line = line.strip()
        if line.startswith("#define") and "FW_VERSION_STRING" in line:
            parts = line.split('"')
            if len(parts) >= 2:
                return parts[1]
    raise RuntimeError(f"FW_VERSION_STRING not found in {header_path}")


def _open_jtag_with_retry(retries=10, delay=0.5):
    last_error = None
    for _ in range(retries):
        try:
            probe = cb_jtag.CBJtagProbe()
            return cb_jtag.CBJtag(jtag_probe=probe)
        except Exception as exc:  # pragma: no cover - depends on USB timing
            last_error = exc
            time.sleep(delay)

    raise RuntimeError(
        f"Unable to open CB JTAG probe after {retries} retries over {retries * delay:.1f}s"
    ) from last_error


class TestProbeInterface:
    @pytest.fixture(scope="class", autouse=True)
    def _prepare_jtag_after_flash(self, dut, request):
        app_build_dir = Path(dut.device_config.app_build_dir)
        fw_version_header = app_build_dir / "cb_info" / "fw_version.h"
        request.cls.expected_fw_version = _read_fw_version_string(fw_version_header)

        # Requesting dut ensures Twister flashes and starts this firmware before host I/O.
        # After flashing, USB interfaces can re-enumerate; allow extra time for probe recovery.
        del dut
        time.sleep(2.0)
        request.cls.jtag = _open_jtag_with_retry(retries=60, delay=0.5)

    def test_probe_version(self):
        probe_version = self.jtag.get_probe_version()
        assert probe_version, "Probe version must not be empty"
        assert probe_version == self.expected_fw_version, (
            f"Probe version mismatch. Expected '{self.expected_fw_version}', got '{probe_version}'"
        )

    def test_read_idcodes(self):
        self.jtag.set_sys_reset_pin_low()
        self.jtag.tap_reset()

        num_taps = self.jtag.get_taps_in_chain()
        assert num_taps == EXPECTED_NUM_TAPS, (
            f"Wrong number of TAPs detected in chain "
            f"(expected {EXPECTED_NUM_TAPS}, got {num_taps})"
        )

        id_codes = self.jtag.get_tap_id_code(num_taps)
        assert len(id_codes) == num_taps, (
            f"Expected {num_taps} IDCODE(s), got {len(id_codes)}"
        )

        for idx, idcode in enumerate(id_codes):
            assert isinstance(idcode, int), f"IDCODE at TAP {idx} is not an integer"
            assert 0 <= idcode <= 0xFFFFFFFF, f"IDCODE at TAP {idx} out of range"
            assert idcode not in (0x00000000, 0xFFFFFFFF), (
                f"Invalid IDCODE at TAP {idx}: 0x{idcode:08X}"
            )

        assert id_codes == EXPECTED_IDCODES, (
            f"IDCODE mismatch. Expected {[f'0x{x:08X}' for x in EXPECTED_IDCODES]}, "
            f"got {[f'0x{x:08X}' for x in id_codes]}"
        )