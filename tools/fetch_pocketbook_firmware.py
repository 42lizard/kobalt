#!/usr/bin/env python3
"""Download and verify the official firmware used by the PocketBook build."""
import argparse
from pathlib import Path
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from pocketbook.prepare_firmware import SWUPDATE_SHA256
from release_sources import digest, download

FIRMWARE = {
    'url': 'https://download.pocketbook-int.com/fw/Yitoa/743k3/user/20260105/sw_20260105_U743k3_6.10.2854_user.zip',
    'sha256': '769fb0ed98446fb134f6baf177504e1ab2e9552b2944f2a4ec55fab059d305cf',
}


def fetch(output):
    output.mkdir(parents=True, exist_ok=True)
    firmware = output / 'SWUPDATE.BIN'
    if firmware.is_symlink():
        raise ValueError('Firmware must not be a symlink')
    if firmware.is_file() and digest(firmware) == SWUPDATE_SHA256:
        return firmware
    archive = output / 'PB743K3.zip'
    download(FIRMWARE, archive)
    with tempfile.TemporaryDirectory(dir=output) as temporary:
        with zipfile.ZipFile(archive) as source:
            member = source.getinfo('SWUPDATE.BIN')
            if member.file_size > 1024 * 1024 * 1024:
                raise ValueError('Firmware exceeds 1 GiB')
            source.extract(member, temporary)
        extracted = Path(temporary) / 'SWUPDATE.BIN'
        if digest(extracted) != SWUPDATE_SHA256:
            raise ValueError('Firmware checksum mismatch')
        extracted.replace(firmware)
    return firmware


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/firmware')
    args = parser.parse_args()
    print(fetch(args.output))
