import gzip
import hashlib
import json
import os
import re
import struct

import pytest
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding, rsa

import ota_tool


@pytest.fixture(scope="module")
def key():
    return rsa.generate_private_key(public_exponent=65537, key_size=2048)


def verify(key, signature, data):
    key.public_key().verify(signature, data, padding.PKCS1v15(), hashes.SHA256())   # raises if invalid


def test_signed_image_uses_the_core_layout(key):
    image = bytes([0xE9]) + bytes(range(256)) * 10
    out = ota_tool.signed_image(key, image)
    (sig_len,) = struct.unpack("<I", out[-4:])
    assert sig_len == 256
    body = out[:-(sig_len + 4)]
    assert body[:2] == b"\x1f\x8b"                  # gzip: the bootloader unpacks it
    assert gzip.decompress(body) == image
    assert struct.unpack("<I", body[-4:])[0] == len(image)   # the size the bootloader reads
    verify(key, out[len(body):-4], body)
    assert ota_tool.signed_part(out) == body


def test_signed_image_is_reproducible(key):
    image = b"\xe9" + b"abc" * 100
    a = ota_tool.signed_image(key, image)
    b = ota_tool.signed_image(key, image)
    assert a == b   # no gzip timestamp; PKCS#1 v1.5 is deterministic


def test_signed_image_without_gzip(key):
    image = b"\xe9" + b"\x00" * 50
    out = ota_tool.signed_image(key, image, compress=False)
    assert out[:len(image)] == image
    verify(key, out[len(image):-4], image)


def test_signed_part_rejects_unsigned_files():
    with pytest.raises(ValueError):
        ota_tool.signed_part(b"\xe9\x00\x00")
    with pytest.raises(ValueError):
        ota_tool.signed_part(b"\xe9" * 100)


def test_manifest_is_signed_and_binds_the_image(key):
    rider = ota_tool.signed_image(key, b"\xe9rider")
    kids = ota_tool.signed_image(key, b"\xe9kids")
    text = ota_tool.build_manifest(key, "1.2.3", {
        "rider": ("motoclock-rider.bin.gz", rider),
        "kids": ("motoclock-kids.bin.gz", kids),
    }, notes="Fixes")
    line, sig_hex, rest = text.split(b"\n")
    assert rest == b""
    verify(key, bytes.fromhex(sig_hex.decode()), line)
    payload = json.loads(line)
    assert payload["version"] == "1.2.3"
    assert payload["notes"] == "Fixes"
    assert payload["variants"]["rider"] == {
        "file": "motoclock-rider.bin.gz", "size": len(rider),
        "sha256": hashlib.sha256(ota_tool.signed_part(rider)).hexdigest()}
    assert payload["variants"]["kids"]["sha256"] == hashlib.sha256(ota_tool.signed_part(kids)).hexdigest()


def test_manifest_rejects_bad_input(key):
    with pytest.raises(ValueError):
        ota_tool.build_manifest(key, "v1.2.3", {})
    with pytest.raises(ValueError):
        ota_tool.build_manifest(key, "1.2.3-rc1", {})
    signed = ota_tool.signed_image(key, b"\xe9")
    with pytest.raises(ValueError):
        ota_tool.build_manifest(key, "1.2.3", {"debug": ("x.bin.gz", signed)})
    with pytest.raises(ValueError):
        ota_tool.build_manifest(key, "1.2.3", {"rider": ("../x.bin", signed)})


def test_manifest_truncates_long_notes(key):
    text = ota_tool.build_manifest(key, "1.0.0", {}, notes="x" * 1000)
    assert len(json.loads(text.split(b"\n")[0])["notes"]) == ota_tool.NOTES_MAX


def test_pubkey_header_holds_the_raw_key(key):
    header = ota_tool.pubkey_header(key.public_key())
    assert "#define OTA_PUBKEY_SET 1" in header

    def array(name):
        body = re.search(name + r"\[\] PROGMEM = \{(.*?)\};", header, re.S).group(1)
        return bytes(int(h, 16) for h in re.findall(r"0x([0-9a-f]{2})", body))

    numbers = key.public_key().public_numbers()
    n = array("OTA_PUBKEY_N")
    assert len(n) == 256
    assert int.from_bytes(n, "big") == numbers.n
    assert int.from_bytes(array("OTA_PUBKEY_E"), "big") == numbers.e == 65537


def test_cli_round_trip(tmp_path):
    private = tmp_path / "key.pem"
    header = tmp_path / "ota_pubkey.h"
    ota_tool.main(["keygen", "--private", str(private), "--header", str(header)])
    assert private.stat().st_mode & 0o077 == 0
    with pytest.raises(SystemExit):
        ota_tool.main(["keygen", "--private", str(private), "--header", str(header)])   # never overwrite

    ota_tool.main(["check-key", "--key", str(private), "--header", str(header)])
    other = tmp_path / "other.pem"
    ota_tool.main(["keygen", "--private", str(other), "--header", str(tmp_path / "other.h")])
    with pytest.raises(SystemExit):
        ota_tool.main(["check-key", "--key", str(other), "--header", str(header)])

    image = tmp_path / "fw.bin"
    image.write_bytes(b"\xe9" + b"\x00" * 100)
    signed = tmp_path / "motoclock-rider.bin.gz"
    ota_tool.main(["sign", "--key", str(private), "--in", str(image), "--out", str(signed)])
    manifest = tmp_path / "ota-manifest.txt"
    ota_tool.main(["manifest", "--key", str(private), "--version", "0.3.0", "--out", str(manifest),
                   "--variant", f"rider={signed}"])
    payload = json.loads(manifest.read_bytes().split(b"\n")[0])
    assert payload["variants"]["rider"]["file"] == "motoclock-rider.bin.gz"
    assert payload["variants"]["rider"]["size"] == signed.stat().st_size


def test_free_update_space_matches_the_device():
    # 1m64: 0xEB000 before the filesystem; a 504336 byte image occupies 124 sectors
    assert ota_tool.free_update_space(504336, "eagle.flash.1m64.ld") == 0xEB000 - 124 * 0x1000 - 0x1000
    assert ota_tool.free_update_space(2_000_000, "eagle.flash.1m64.ld") == 0


def test_check_size(tmp_path):
    small = tmp_path / "small.bin"
    small.write_bytes(b"\xe9" + bytes(1000))
    ota_tool.main(["check-size", "--in", str(small), "--ldscript", "eagle.flash.1m64.ld"])
    big = tmp_path / "big.bin"
    big.write_bytes(bytes(range(256)) * 2000 + os.urandom(500_000))   # incompressible
    with pytest.raises(SystemExit):
        ota_tool.main(["check-size", "--in", str(big), "--ldscript", "eagle.flash.1m64.ld"])
    with pytest.raises(SystemExit):
        ota_tool.main(["check-size", "--in", str(small), "--ldscript", "eagle.flash.4m.ld"])
