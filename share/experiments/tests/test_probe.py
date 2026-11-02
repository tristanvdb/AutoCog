"""Machine probing without the machine: a fake sysfs and fake device lists."""

from campaign.probe import _scan_amd, offload_problem


def card(root, name, vendor, gtt=None, vram=None):
    device = root / name / "device"
    device.mkdir(parents=True)
    (device / "vendor").write_text(vendor + "\n")
    (device / "device").write_text("0x1586\n")
    if gtt is not None:
        (device / "mem_info_gtt_total").write_text(f"{gtt}\n")
        (device / "mem_info_vram_total").write_text(f"{vram}\n")


def test_amd_counts_gtt_and_carveout(tmp_path):
    card(tmp_path, "card1", "0x1002", gtt=65, vram=2)
    gpus = _scan_amd(str(tmp_path))
    assert gpus == [{"index": 0, "name": "amd:0x1586", "total_bytes": 67}]


def test_other_vendors_and_connectors_are_ignored(tmp_path):
    card(tmp_path, "card0", "0x8086")
    (tmp_path / "card1-DP-1").mkdir()
    assert _scan_amd(str(tmp_path)) == []


def test_offload_problem():
    gpu = {"gpus": [{"index": 0}]}
    cpu = [{"type": "cpu"}]
    assert offload_problem({"gpus": []}, cpu) is None
    assert offload_problem(gpu, cpu + [{"type": "igpu"}]) is None
    assert "run on the CPU" in offload_problem(gpu, cpu)
