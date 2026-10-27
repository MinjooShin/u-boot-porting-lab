#!/usr/bin/env python3
"""Wrap a raw U-Boot binary in the normal-boot header expected by BL2.

Preserve configuration bytes from an existing, checksum-valid image. This
generates a file only; it does not access QSPI or update firmware signing data.
"""

import argparse
import hashlib
from pathlib import Path
import struct


HEADER_SIZE = 64
SIGNATURE = 0x30365041


def integer(value):
    return int(value, 0)


def checksum(data):
    """Match NC_Checksum(): sum little-endian 32-bit words modulo 2**32."""
    word_bytes = data[:len(data) // 4 * 4]
    return sum(word[0] for word in struct.iter_unpack("<I", word_bytes)) & 0xffffffff


def template_header(data, offset):
    if offset < 0 or offset + HEADER_SIZE > len(data):
        raise ValueError("template does not contain a complete header at this offset")
    header = data[offset:offset + HEADER_SIZE]
    length, expected, config1 = struct.unpack_from("<III", header)
    if struct.unpack_from("<I", header, 60)[0] != SIGNATURE:
        raise ValueError("template has an invalid BL2 signature")
    if not length or offset + HEADER_SIZE + length > len(data):
        raise ValueError("template payload is empty or truncated")
    body = data[offset + HEADER_SIZE:offset + HEADER_SIZE + length]
    if checksum(body) != expected:
        raise ValueError("template payload checksum does not match its header")
    if ((config1 >> 4) & 0xf) != 1:
        raise ValueError("template must explicitly select normal boot; secure images are unsupported")
    return header


def build_image(header, payload):
    if not payload:
        raise ValueError("payload is empty")
    if len(payload) >= HEADER_SIZE and struct.unpack_from("<I", payload, 60)[0] == SIGNATURE:
        raise ValueError("payload already appears to contain a BL2 header")
    # BL2 reads an 8-byte-aligned body. Include zero padding in the length and
    # checksum so every byte of the input is covered by the checksum.
    body = payload + b"\0" * (-len(payload) % 8)
    if len(body) > 0xffffffff:
        raise ValueError("payload exceeds the header's 32-bit length field")
    updated = bytearray(header)
    struct.pack_into("<II", updated, 0, len(body), checksum(body))
    return bytes(updated) + body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--template", required=True, type=Path,
                        help="original normal-boot image, or a QSPI dump")
    parser.add_argument("--template-offset", type=integer, default=0,
                        help="header offset within the template; default: 0")
    parser.add_argument("--payload", required=True, type=Path,
                        help="raw u-boot.bin including its DTB")
    parser.add_argument("--output", required=True, type=Path,
                        help="new output file (must not already exist)")
    parser.add_argument("--max-size", type=integer,
                        help="maximum total output bytes, including the header")
    args = parser.parse_args()

    try:
        if args.output.resolve() in (args.template.resolve(), args.payload.resolve()):
            raise ValueError("output must differ from the template and payload")
        header = template_header(args.template.read_bytes(), args.template_offset)
        payload = args.payload.read_bytes()
        image = build_image(header, payload)
        if args.max_size is not None and (args.max_size <= 0 or len(image) > args.max_size):
            raise ValueError("output size {} exceeds the allowed size {}".format(
                len(image), args.max_size))
        # Reuse the loader-format checks on the complete assembled image.
        template_header(image, 0)
        with args.output.open("xb") as target:
            target.write(image)
        if args.output.read_bytes() != image:
            raise OSError("output read-back verification failed")
    except (OSError, ValueError, struct.error) as exc:
        parser.exit(1, "error: {}\n".format(exc))

    stored_length, stored_checksum = struct.unpack_from("<II", image)
    print("Output: {}".format(args.output))
    print("Raw payload: {} bytes; stored payload: {} bytes".format(len(payload), stored_length))
    print("Total: {} bytes (64-byte header included)".format(len(image)))
    print("Checksum: 0x{:08x}".format(stored_checksum))
    print("SHA256: {}".format(hashlib.sha256(image).hexdigest()))


if __name__ == "__main__":
    main()
