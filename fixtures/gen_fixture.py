#!/usr/bin/env python3
"""Generate the self-authored playback fixture retrovert_selftest.ptcop.

A PTCOLLAGE-071119 project written from scratch: one PTVOICE instrument
whose waveform is a coordinate-drawn triangle, and one unit playing a
four-note arpeggio over eight measures at 120 BPM. Deterministic output
— the committed fixture and its sha256 in harness.toml must match what
this script emits.
"""

import struct
from pathlib import Path

OUT = Path(__file__).parent / "retrovert_selftest.ptcop"

BEAT_CLOCK = 480
BEAT_NUM = 4
BEAT_TEMPO = 120.0
MEASURES = 8  # 16 s at this tempo, comfortably past the 10 s smoke

MEASURE_CLOCK = BEAT_CLOCK * BEAT_NUM

# Event kinds, from pxtnEvelist.h.
ON, KEY, PAN_VOLUME, VELOCITY, VOLUME, VOICENO, GROUPNO = 1, 2, 3, 4, 5, 12, 13

BASE_KEY = 0x6000  # the replayer's default key; 0x100 per semitone
FIGURE = [0, 4, 7, 12]  # semitones above the base

# PTVOICE voice flags and data flags, from pxtnWoice.h.
VOICEFLAG_SMOOTH = 0x02
DATAFLAG_WAVE = 0x01
VOICE_COODINATE = 0

WAVE_RESO = 32  # x resolution of the drawn waveform
WAVE_POINTS = [(0, 0), (8, 127), (24, -127)]  # one triangle cycle


def varint(value):
    """The 7-bits-per-byte little-endian encoding pxtnData uses."""
    value &= 0xFFFFFFFF
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        out.append(byte | 0x80 if value else byte)
        if not value:
            return bytes(out)


def chunk(code, payload):
    return code + struct.pack("<I", len(payload)) + payload


def master():
    return chunk(
        b"MasterV5",
        struct.pack(
            "<hbfii",
            BEAT_CLOCK,
            BEAT_NUM,
            BEAT_TEMPO,
            0,  # repeat from the top
            MEASURE_CLOCK * MEASURES,
        ),
    )


def events():
    """One unit: set it up at clock 0, then a note every beat."""
    records = [
        (0, VOICENO, 0),
        (0, GROUPNO, 0),
        (0, VOLUME, 104),
        (0, VELOCITY, 104),
        (0, PAN_VOLUME, 64),
    ]
    for beat in range(BEAT_NUM * MEASURES):
        clock = beat * BEAT_CLOCK
        records.append((clock, KEY, BASE_KEY + FIGURE[beat % len(FIGURE)] * 0x100))
        records.append((clock, ON, BEAT_CLOCK))

    body = bytearray(struct.pack("<I", len(records)))
    previous = 0
    for clock, kind, value in records:
        body += varint(clock - previous)
        body += bytes((0, kind))  # unit 0
        body += varint(value)
        previous = clock
    return chunk(b"Event V5", bytes(body))


def ptvoice():
    """One PTVOICE material: a single coordinate-drawn voice, no envelope."""
    body = bytearray()
    body += varint(0)  # x3x basic key, unused
    body += varint(0)
    body += varint(0)
    body += varint(1)  # one voice

    body += varint(0x4500)  # basic key: A4
    body += varint(128)  # volume
    body += varint(64)  # pan, centred
    body += varint(struct.unpack("<I", struct.pack("<f", 1.0))[0])  # tuning
    body += varint(VOICEFLAG_SMOOTH)
    body += varint(DATAFLAG_WAVE)

    body += varint(VOICE_COODINATE)
    body += varint(len(WAVE_POINTS))
    body += varint(WAVE_RESO)
    for x, y in WAVE_POINTS:
        body += struct.pack("<Bb", x, y)

    voice = b"PTVOICE-" + struct.pack("<ii", 20060111, len(body)) + bytes(body)
    # The material header repeats the voice size; the enclosing chunk size
    # covers that header plus the voice itself.
    head = struct.pack("<HHfi", 0, 0, 0.0, len(voice))
    return chunk(b"matePTV ", head + voice)


def units():
    return chunk(b"num UNIT", struct.pack("<hh", 1, 0))


def build():
    out = bytearray(b"PTCOLLAGE-071119")
    out += struct.pack("<HH", 0x0500, 0)  # exe version, reserved
    out += master()
    out += events()
    out += ptvoice()
    out += units()
    out += chunk(b"pxtoneND", b"")
    return bytes(out)


def main():
    data = build()
    OUT.write_bytes(data)
    print(f"wrote {OUT} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
