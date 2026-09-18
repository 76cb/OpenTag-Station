#!/usr/bin/env python3
"""Compile and validate the deterministic OpenTag Community catalog pack."""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import zlib
from pathlib import Path

MAGIC = b"OTCPACK\0"
FORMAT_VERSION = 1
HEADER = struct.Struct("<8sHHIIIIIIII32s24s20s")
DIRECTORY = struct.Struct("<B3xIIIIIII")
HEADER_SIZE = HEADER.size
BLOCK_LIMIT = 64 * 1024
MAX_PACK_SIZE = 2_621_440
DETAIL_FIELDS = (
    "id", "manufacturer", "name", "material", "density", "weight",
    "spool_weight", "spool_type", "is_refill", "diameter", "color_hex",
    "color_hexes", "extruder_temp", "extruder_temp_range", "bed_temp",
    "bed_temp_range", "fill", "finish", "multi_color_direction", "pattern",
    "translucent", "glow", "codes", "eans", "eans_refill",
    "country_of_origin", "sds_url", "tds_url",
)
REQUIRED_TEXT = ("id", "manufacturer", "name", "material")


class CatalogError(ValueError):
    pass


def canonical_record(value: object) -> dict:
    if not isinstance(value, dict):
        raise CatalogError("Community record must be an object")
    unknown = set(value) - set(DETAIL_FIELDS)
    if unknown:
        raise CatalogError(f"Community source format drift: {sorted(unknown)}")
    for key in REQUIRED_TEXT:
        if not isinstance(value.get(key), str) or not value[key]:
            raise CatalogError(f"Community record requires nonempty {key}")
    if len(value["id"].encode()) > 180:
        raise CatalogError("Community id exceeds 180 bytes")
    for key in ("manufacturer", "name", "material"):
        if len(value[key].encode()) > 128:
            raise CatalogError(f"Community {key} exceeds 128 bytes")
    for key in ("density", "diameter"):
        number = value.get(key)
        if not isinstance(number, (int, float)) or isinstance(number, bool) or number <= 0:
            raise CatalogError(f"Community {key} must be positive")
    record = {key: value.get(key) for key in DETAIL_FIELDS if key in value}
    return record


def _length_string(value: str) -> bytes:
    data = value.encode("utf-8")
    if len(data) > 65535:
        raise CatalogError("Catalog string exceeds binary format")
    return struct.pack("<H", len(data)) + data


def index_record(record: dict, ordinal: int) -> bytes:
    searchable = " ".join(record[key].lower() for key in ("manufacturer", "name", "material"))
    color = record.get("color_hex") or ""
    if not isinstance(color, str):
        color = ""
    weight = record.get("weight")
    weight = float(weight) if isinstance(weight, (int, float)) and not isinstance(weight, bool) else 0.0
    body = struct.pack("<If", ordinal, weight)
    for value in (record["id"], record["manufacturer"], record["name"],
                  record["material"], color, searchable):
        body += _length_string(value)
    return struct.pack("<H", len(body)) + body


def detail_record(record: dict) -> bytes:
    data = json.dumps(record, sort_keys=True, separators=(",", ":"),
                      ensure_ascii=False).encode("utf-8")
    if len(data) > 65535:
        raise CatalogError("Community detail record exceeds 65535 bytes")
    return struct.pack("<H", len(data)) + data


def blocks(records: list[bytes]) -> list[tuple[int, int, bytes]]:
    result, current, first, count = [], bytearray(), 0, 0
    for ordinal, record in enumerate(records):
        if len(record) > BLOCK_LIMIT:
            raise CatalogError("Catalog record exceeds block limit")
        if current and len(current) + len(record) > BLOCK_LIMIT:
            result.append((first, count, bytes(current)))
            current, first, count = bytearray(), ordinal, 0
        current.extend(record)
        count += 1
    if current:
        result.append((first, count, bytes(current)))
    return result


def compile_catalog(source: Path, output: Path, catalog_version: str,
                    source_revision: str) -> dict:
    raw = source.read_bytes()
    try:
        values = json.loads(raw)
    except json.JSONDecodeError as error:
        raise CatalogError(f"Malformed Community JSON: {error}") from error
    if not isinstance(values, list) or not values:
        raise CatalogError("Community source must be a nonempty array")
    records = [canonical_record(value) for value in values]
    ids = [record["id"] for record in records]
    if len(ids) != len(set(ids)):
        raise CatalogError("Community ids must be unique")
    index_blocks = blocks([index_record(record, n) for n, record in enumerate(records)])
    detail_blocks = blocks([detail_record(record) for record in records])
    encoded = []
    for kind, source_blocks in ((1, index_blocks), (2, detail_blocks)):
        for first, count, expanded in source_blocks:
            compressed = zlib.compress(expanded, 9, wbits=-15)
            encoded.append((kind, first, count, expanded, compressed))
    directory_offset = HEADER_SIZE
    data_offset = directory_offset + len(encoded) * DIRECTORY.size
    cursor = data_offset
    directory = bytearray()
    payload = bytearray()
    for kind, first, count, expanded, compressed in encoded:
        directory.extend(DIRECTORY.pack(kind, first, count, cursor,
                                        len(compressed), len(expanded),
                                        zlib.crc32(expanded), 0))
        payload.extend(compressed)
        cursor += len(compressed)
    pack_size = cursor
    if pack_size > MAX_PACK_SIZE:
        raise CatalogError(f"community.pack {pack_size} exceeds {MAX_PACK_SIZE} byte gate")
    version_bytes = catalog_version.encode("ascii")
    revision_bytes = source_revision.encode("ascii")
    if len(version_bytes) > 23 or len(revision_bytes) > 19:
        raise CatalogError("Catalog version/source revision exceeds header")
    source_hash = hashlib.sha256(raw).digest()
    body = bytes(directory + payload)
    header = HEADER.pack(MAGIC, FORMAT_VERSION, HEADER_SIZE, len(records),
                         len(index_blocks), len(detail_blocks), directory_offset,
                         data_offset, pack_size, zlib.crc32(body), BLOCK_LIMIT,
                         source_hash, version_bytes.ljust(24, b"\0"),
                         revision_bytes.ljust(20, b"\0"))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + body)
    return {"schema": FORMAT_VERSION, "version": catalog_version,
            "source_revision": source_revision, "source_sha256": source_hash.hex(),
            "records": len(records), "size": pack_size,
            "sha256": hashlib.sha256(header + body).hexdigest()}


def inspect_pack(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) < HEADER_SIZE:
        raise CatalogError("Catalog header truncated")
    unpacked = HEADER.unpack_from(data)
    (magic, schema, header_size, record_count, index_count, detail_count,
     directory_offset, data_offset, pack_size, payload_crc, block_limit,
     source_hash, version, revision) = unpacked
    if magic != MAGIC or schema != FORMAT_VERSION or header_size != HEADER_SIZE:
        raise CatalogError("Catalog header incompatible")
    if pack_size != len(data) or block_limit != BLOCK_LIMIT:
        raise CatalogError("Catalog size/bounds mismatch")
    if directory_offset != HEADER_SIZE or data_offset != HEADER_SIZE + (index_count + detail_count) * DIRECTORY.size:
        raise CatalogError("Catalog directory offsets invalid")
    if zlib.crc32(data[HEADER_SIZE:]) != payload_crc:
        raise CatalogError("Catalog checksum mismatch")
    payload_cursor, next_index, next_detail = data_offset, 0, 0
    for n in range(index_count + detail_count):
        kind, first, count, offset, compressed, expanded, crc, reserved = DIRECTORY.unpack_from(data, directory_offset + n * DIRECTORY.size)
        expected_kind = 1 if n < index_count else 2
        next_record = next_index if kind == 1 else next_detail
        if kind != expected_kind or not count or not compressed or not expanded or expanded > BLOCK_LIMIT or reserved or first != next_record or offset != payload_cursor:
            raise CatalogError("Catalog block metadata invalid")
        if offset < data_offset or offset + compressed > len(data):
            raise CatalogError("Catalog block offset invalid")
        try:
            block = zlib.decompress(data[offset:offset + compressed], wbits=-15)
        except zlib.error as error:
            raise CatalogError("Catalog block compression invalid") from error
        if len(block) != expanded or zlib.crc32(block) != crc:
            raise CatalogError("Catalog block checksum mismatch")
        payload_cursor += compressed
        if kind == 1:
            next_index += count
        else:
            next_detail += count
    if payload_cursor != len(data) or next_index != record_count or next_detail != record_count:
        raise CatalogError("Catalog block coverage invalid")
    return {"schema": schema, "version": version.rstrip(b"\0").decode(),
            "source_revision": revision.rstrip(b"\0").decode(),
            "source_sha256": source_hash.hex(), "records": record_count,
            "size": pack_size, "sha256": hashlib.sha256(data).hexdigest()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    compile_cmd = sub.add_parser("compile")
    compile_cmd.add_argument("--source", type=Path, required=True)
    compile_cmd.add_argument("--output", type=Path, required=True)
    compile_cmd.add_argument("--catalog-version", required=True)
    compile_cmd.add_argument("--source-revision", required=True)
    inspect_cmd = sub.add_parser("inspect")
    inspect_cmd.add_argument("pack", type=Path)
    args = parser.parse_args()
    result = (compile_catalog(args.source, args.output, args.catalog_version,
                              args.source_revision) if args.command == "compile"
              else inspect_pack(args.pack))
    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
