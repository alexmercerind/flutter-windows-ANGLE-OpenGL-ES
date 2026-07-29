from __future__ import annotations

import hashlib
import re
import struct
from pathlib import Path
from typing import Any


PE_MACHINE_NAMES = {
    0x014C: "x86",
    0x8664: "x64",
    0xAA64: "arm64",
}

DEBUG_RUNTIME_RE = re.compile(
    r"^(?:ucrtbased|(?:msvcp|vcruntime|vccorlib|concrt|vcomp)"
    r"\d+(?:_\d+)?d(?:_[a-z0-9_]+)?)\.dll$",
    re.IGNORECASE,
)
VC_RUNTIME_RE = re.compile(
    r"^(?:msvcp|vcruntime|vccorlib|concrt|vcomp)\d+"
    r"(?:_[a-z0-9]+)*\.dll$",
    re.IGNORECASE,
)

SYSTEM_DLLS = {
    "advapi32.dll",
    "bcrypt.dll",
    "cfgmgr32.dll",
    "combase.dll",
    "crypt32.dll",
    "d3d11.dll",
    "d3d12.dll",
    "d3d9.dll",
    "dcomp.dll",
    "dxgi.dll",
    "gdi32.dll",
    "imm32.dll",
    "kernel32.dll",
    "ntdll.dll",
    "ole32.dll",
    "oleaut32.dll",
    "rpcrt4.dll",
    "secur32.dll",
    "setupapi.dll",
    "shell32.dll",
    "shlwapi.dll",
    "ucrtbase.dll",
    "user32.dll",
    "userenv.dll",
    "version.dll",
    "winmm.dll",
    "ws2_32.dll",
}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def machine_name(machine: int) -> str:
    return PE_MACHINE_NAMES.get(machine, f"unknown-0x{machine:04x}")


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


def inspect_pe(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise ValueError(f"Not a PE file: {path}")
    pe_offset = _u32(data, 0x3C)
    if data[pe_offset : pe_offset + 4] != b"PE\0\0":
        raise ValueError(f"Invalid PE signature: {path}")

    coff = pe_offset + 4
    machine = _u16(data, coff)
    section_count = _u16(data, coff + 2)
    optional_size = _u16(data, coff + 16)
    optional = coff + 20
    magic = _u16(data, optional)
    if magic == 0x20B:
        data_directory = optional + 112
        image_base = _u64(data, optional + 24)
    elif magic == 0x10B:
        data_directory = optional + 96
        image_base = _u32(data, optional + 28)
    else:
        raise ValueError(f"Unsupported PE optional-header magic 0x{magic:04x}: {path}")

    section_table = optional + optional_size
    sections: list[tuple[int, int, int, int]] = []
    for index in range(section_count):
        offset = section_table + index * 40
        virtual_size = _u32(data, offset + 8)
        virtual_address = _u32(data, offset + 12)
        raw_size = _u32(data, offset + 16)
        raw_offset = _u32(data, offset + 20)
        sections.append((virtual_address, max(virtual_size, raw_size), raw_offset, raw_size))

    def rva_to_offset(rva: int) -> int:
        for virtual_address, size, raw_offset, raw_size in sections:
            if virtual_address <= rva < virtual_address + size:
                delta = rva - virtual_address
                if delta >= raw_size:
                    raise ValueError(f"RVA 0x{rva:x} has no raw backing in {path}")
                return raw_offset + delta
        if rva < optional:
            return rva
        raise ValueError(f"Cannot map RVA 0x{rva:x} in {path}")

    def import_name(name_rva: int) -> str:
        name_offset = rva_to_offset(name_rva)
        end = data.find(b"\0", name_offset)
        if end < 0:
            raise ValueError(f"Unterminated import name in {path}")
        return data[name_offset:end].decode("ascii", errors="strict")

    import_rva = _u32(data, data_directory + 8)
    import_size = _u32(data, data_directory + 12)
    imports: list[str] = []
    if import_rva and import_size:
        cursor = rva_to_offset(import_rva)
        while cursor + 20 <= len(data):
            descriptor = data[cursor : cursor + 20]
            if descriptor == b"\0" * 20:
                break
            name_rva = _u32(data, cursor + 12)
            imports.append(import_name(name_rva))
            cursor += 20

    delay_import_rva = _u32(data, data_directory + 13 * 8)
    delay_import_size = _u32(data, data_directory + 13 * 8 + 4)
    delay_imports: list[str] = []
    if delay_import_rva and delay_import_size:
        cursor = rva_to_offset(delay_import_rva)
        while cursor + 32 <= len(data):
            descriptor = data[cursor : cursor + 32]
            if descriptor == b"\0" * 32:
                break
            attributes = _u32(data, cursor)
            name_address = _u32(data, cursor + 4)
            if attributes & 1:
                name_rva = name_address
            else:
                if name_address < image_base:
                    raise ValueError(
                        f"Invalid delay-import VA 0x{name_address:x} in {path}"
                    )
                name_rva = name_address - image_base
            delay_imports.append(import_name(name_rva))
            cursor += 32

    imports = sorted(set(imports), key=str.lower)
    delay_imports = sorted(set(delay_imports), key=str.lower)
    return {
        "machine": f"0x{machine:04x}",
        "machine_name": machine_name(machine),
        "imports": imports,
        "delay_imports": delay_imports,
        "runtime_dependencies": sorted(
            set(imports) | set(delay_imports), key=str.lower
        ),
    }


def inspect_import_library(path: Path) -> dict[str, Any]:
    data = path.read_bytes()
    if not data.startswith(b"!<arch>\n"):
        raise ValueError(f"Not a COFF archive: {path}")
    cursor = 8
    machines: set[int] = set()
    while cursor + 60 <= len(data):
        header = data[cursor : cursor + 60]
        try:
            size = int(header[48:58].decode("ascii").strip())
        except ValueError as error:
            raise ValueError(f"Invalid COFF member header in {path}") from error
        body_offset = cursor + 60
        body = data[body_offset : body_offset + size]
        if len(body) >= 8 and body[:4] == b"\x00\x00\xff\xff":
            machines.add(_u16(body, 6))
        elif len(body) >= 2:
            candidate = _u16(body, 0)
            if candidate in PE_MACHINE_NAMES:
                machines.add(candidate)
        cursor = body_offset + size + (size % 2)
    if not machines:
        raise ValueError(f"No machine-bearing members found in {path}")
    return {
        "machines": [f"0x{item:04x}" for item in sorted(machines)],
        "machine_names": [machine_name(item) for item in sorted(machines)],
    }


def is_system_import(name: str) -> bool:
    lower = name.lower()
    return (
        lower in SYSTEM_DLLS
        or lower.startswith("api-ms-")
        or lower.startswith("ext-ms-")
    )


def is_debug_runtime(name: str) -> bool:
    return bool(DEBUG_RUNTIME_RE.match(name))


def prerequisite_for(name: str) -> str | None:
    if VC_RUNTIME_RE.match(name):
        return "Microsoft Visual C++ Redistributable 2015-2022"
    return None
