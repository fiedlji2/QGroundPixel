#!/usr/bin/env python3
"""Analyze a QGroundPixel raw RTP capture (*.qgprtp).

File format (written by GstSourceFactory's rtpCaptureProbe):
    8 bytes magic "QGPRTP1\\n"
    repeated: u64 wall-clock microseconds (LE) | u32 length (LE) | RTP packet bytes

What it reports:
  * stream overview (duration, packets, bitrate, fps, keyframe interval / GOP)
  * every sequence-number gap (lost packets) with wall-clock time and how long the
    decoder had to wait for the next keyframe (IDR/CRA) afterwards
  * silence windows (no packets at all) – the fingerprint of an RF fade; anything
    longer than QGC's 3 s source watchdog will have torn the pipeline down
  * a per-second timeline (--timeline) of packets / bytes / lost / keyframes / max gap

Usage:
    python analyze_rtp_capture.py capture.qgprtp [--timeline] [--events N] [--silence-ms 250]
"""

from __future__ import annotations

import argparse
import statistics
import struct
import sys
from dataclasses import dataclass, field
from datetime import datetime, timezone

MAGIC = b"QGPRTP1\n"

# H.265 NAL unit types (RFC 7798)
NAL_IDR_W_RADL = 19
NAL_IDR_N_LP = 20
NAL_CRA = 21
NAL_VPS, NAL_SPS, NAL_PPS = 32, 33, 34
NAL_AP, NAL_FU = 48, 49
KEYFRAME_TYPES = {16, 17, 18, 19, 20, 21}  # BLA*, IDR*, CRA


@dataclass
class Packet:
    wall_us: int
    seq: int          # raw 16-bit
    ext_seq: int      # unwrapped
    rtp_ts: int
    marker: bool
    size: int
    nal_types: list = field(default_factory=list)   # NAL types that START in this packet
    keyframe_start: bool = False
    param_set: bool = False
    payload: bytes = b""


def parse_h265_payload(payload: bytes):
    """Return (list of NAL types starting in this packet)."""
    if len(payload) < 2:
        return []
    ptype = (payload[0] >> 1) & 0x3F
    if ptype == NAL_FU:
        if len(payload) < 3:
            return []
        fu = payload[2]
        start = bool(fu & 0x80)
        return [fu & 0x3F] if start else []
    if ptype == NAL_AP:
        types = []
        pos = 2
        while pos + 2 <= len(payload):
            size = struct.unpack_from(">H", payload, pos)[0]
            pos += 2
            if size < 2 or pos + size > len(payload):
                break
            types.append((payload[pos] >> 1) & 0x3F)
            pos += size
        return types
    return [ptype]


# H.264 NAL unit types (RFC 6184)
H264_IDR = 5
H264_SPS, H264_PPS = 7, 8
H264_STAP_A, H264_FU_A = 24, 28
H264_KEYFRAME_TYPES = {5}
H264_PARAM_TYPES = {7, 8}


def parse_h264_payload(payload: bytes):
    """Return (list of NAL types starting in this packet) for an H.264 RTP payload."""
    if len(payload) < 1:
        return []
    ptype = payload[0] & 0x1F
    if ptype == H264_FU_A:
        if len(payload) < 2:
            return []
        fu = payload[1]
        return [fu & 0x1F] if fu & 0x80 else []
    if ptype == H264_STAP_A:
        types = []
        pos = 1
        while pos + 2 <= len(payload):
            size = struct.unpack_from(">H", payload, pos)[0]
            pos += 2
            if size < 1 or pos + size > len(payload):
                break
            types.append(payload[pos] & 0x1F)
            pos += size
        return types
    return [ptype]


def detect_codec(payloads) -> str:
    """Guess H.264 vs H.265 from the RTP payload headers of the first packets."""
    h264 = h265 = 0
    for p in payloads:
        if len(p) < 2:
            continue
        # H.264: forbidden bit 0, nal_ref_idc in bits 5-6, type 1..28 (FU-A 28 / STAP-A 24 dominate)
        t264 = p[0] & 0x1F
        if (p[0] & 0x80) == 0 and t264 in (1, 5, 7, 8, 24, 28):
            h264 += 1
        # H.265: 2-byte header, forbidden bit 0, layer id 0 -> second byte 0x01 (TID 1)
        t265 = (p[0] >> 1) & 0x3F
        if (p[0] & 0x80) == 0 and (p[1] & 0xF8) == 0 and (p[1] & 0x07) == 1 and t265 in (0, 1, 19, 20, 21, 32, 33, 34, 48, 49):
            h265 += 1
    return "h265" if h265 > h264 else "h264"


def read_capture(path: str, codec: str = "auto") -> tuple[list[Packet], str]:
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(MAGIC):
        sys.exit(f"{path}: not a QGPRTP1 capture (bad magic)")

    packets: list[Packet] = []
    pos = len(MAGIC)
    last_seq = None
    cycles = 0
    truncated = False
    while pos + 12 <= len(data):
        wall_us, length = struct.unpack_from("<QI", data, pos)
        pos += 12
        if pos + length > len(data):
            truncated = True
            break
        pkt = data[pos:pos + length]
        pos += length
        if length < 12 or (pkt[0] >> 6) != 2:
            continue  # not RTP v2
        cc = pkt[0] & 0x0F
        ext = bool(pkt[0] & 0x10)
        marker = bool(pkt[1] & 0x80)
        seq, rtp_ts = struct.unpack_from(">HI", pkt, 2)
        hdr = 12 + 4 * cc
        if ext and hdr + 4 <= length:
            ext_len = struct.unpack_from(">H", pkt, hdr + 2)[0]
            hdr += 4 + 4 * ext_len
        payload = pkt[hdr:]

        if last_seq is not None:
            if seq < 0x4000 and last_seq > 0xC000:
                cycles += 1
            elif seq > 0xC000 and last_seq < 0x4000 and cycles > 0:
                cycles -= 1  # late reordered packet from before the wrap
        last_seq = seq
        ext_seq = cycles * 65536 + seq

        p = Packet(wall_us, seq, ext_seq, rtp_ts, marker, length)
        p.payload = payload
        packets.append(p)

    if codec == "auto":
        codec = detect_codec(p.payload for p in packets[:500])

    for p in packets:
        if codec == "h264":
            p.nal_types = parse_h264_payload(p.payload)
            p.keyframe_start = any(t in H264_KEYFRAME_TYPES for t in p.nal_types)
            p.param_set = any(t in H264_PARAM_TYPES for t in p.nal_types)
        else:
            p.nal_types = parse_h265_payload(p.payload)
            p.keyframe_start = any(t in KEYFRAME_TYPES for t in p.nal_types)
            p.param_set = any(t in (NAL_VPS, NAL_SPS, NAL_PPS) for t in p.nal_types)
        p.payload = b""

    if truncated:
        print("note: capture ends mid-record (app was probably killed while writing) – ignored tail")
    return packets, codec


def fmt_wall(us: int, t0: int) -> str:
    rel = (us - t0) / 1e6
    clock = datetime.fromtimestamp(us / 1e6, tz=timezone.utc).astimezone().strftime("%H:%M:%S.%f")[:-3]
    return f"{rel:9.3f}s ({clock})"


def analyze(packets: list[Packet], codec: str, silence_ms: int, max_events: int, timeline: bool):
    if len(packets) < 2:
        sys.exit("capture contains fewer than 2 RTP packets")

    t0 = packets[0].wall_us
    t_end = packets[-1].wall_us
    duration = (t_end - t0) / 1e6
    total_bytes = sum(p.size for p in packets)
    frames = sum(1 for p in packets if p.marker)
    keyframes = [p for p in packets if p.keyframe_start]
    param_sets = [p for p in packets if p.param_set]

    print("=" * 78)
    print("STREAM OVERVIEW")
    print("=" * 78)
    print(f"codec        : {codec.upper()}  (SPS/PPS/VPS packets seen: {len(param_sets)}, "
          f"first keyframe after {((keyframes[0].wall_us - t0) / 1e6) if keyframes else float('nan'):.2f} s)")
    print(f"start        : {datetime.fromtimestamp(t0 / 1e6).strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"duration     : {duration:.1f} s")
    print(f"packets      : {len(packets)}  ({total_bytes / 1e6:.1f} MB, avg {total_bytes / len(packets):.0f} B/packet)")
    print(f"bitrate      : {total_bytes * 8 / duration / 1e6:.2f} Mbit/s average")
    print(f"frames       : {frames}  ({frames / duration:.1f} fps by RTP marker)")
    if len(keyframes) >= 2:
        gops = [(b.wall_us - a.wall_us) / 1e6 for a, b in zip(keyframes, keyframes[1:])]
        print(f"keyframes    : {len(keyframes)}  interval mean {statistics.mean(gops):.2f} s, "
              f"median {statistics.median(gops):.2f} s, max {max(gops):.2f} s")
    else:
        print(f"keyframes    : {len(keyframes)} (not enough to measure GOP)")

    # ---- sequence gaps -------------------------------------------------------------------
    print()
    print("=" * 78)
    print("PACKET LOSS (RTP sequence gaps)")
    print("=" * 78)
    events = []
    reordered = 0
    duplicates = 0
    lost_total = 0
    for i in range(1, len(packets)):
        prev, cur = packets[i - 1], packets[i]
        delta = cur.ext_seq - prev.ext_seq
        if delta == 1:
            continue
        if delta <= 0:
            if delta == 0:
                duplicates += 1
            else:
                reordered += 1
            continue
        lost = delta - 1
        lost_total += lost
        # time until the next keyframe start at or after this packet
        next_kf = next((p for p in packets[i:] if p.keyframe_start), None)
        recover_s = (next_kf.wall_us - cur.wall_us) / 1e6 if next_kf else None
        events.append((cur.wall_us, lost, prev.ext_seq, cur.ext_seq, recover_s, (cur.wall_us - prev.wall_us) / 1e3))

    expected = packets[-1].ext_seq - packets[0].ext_seq + 1
    print(f"loss events  : {len(events)}   lost packets: {lost_total} of {expected} "
          f"({100.0 * lost_total / expected:.3f} %)   reordered: {reordered}   duplicates: {duplicates}")
    if events:
        sizes = [e[1] for e in events]
        print(f"gap sizes    : median {statistics.median(sizes):.0f}, max {max(sizes)} packets")
        recs = [e[4] for e in events if e[4] is not None]
        if recs:
            print(f"recovery     : after a loss the next keyframe arrived after median {statistics.median(recs):.2f} s, "
                  f"max {max(recs):.2f} s")
        print()
        print(f"{'time':>28}  {'lost':>5}  {'seq range':>17}  {'hole ms':>8}  {'next keyframe':>13}")
        for wall, lost, s0, s1, rec, hole_ms in events[:max_events]:
            rec_txt = f"{rec:6.2f} s" if rec is not None else "   none"
            print(f"{fmt_wall(wall, t0):>28}  {lost:5d}  {s0:8d}->{s1:<8d}  {hole_ms:8.1f}  {rec_txt:>13}")
        if len(events) > max_events:
            print(f"  ... {len(events) - max_events} more (use --events N)")

    # ---- silence windows -----------------------------------------------------------------
    print()
    print("=" * 78)
    print(f"SILENCE WINDOWS (no packets for > {silence_ms} ms)")
    print("=" * 78)
    silences = []
    for i in range(1, len(packets)):
        gap_ms = (packets[i].wall_us - packets[i - 1].wall_us) / 1e3
        if gap_ms > silence_ms:
            silences.append((packets[i - 1].wall_us, gap_ms, packets[i].ext_seq - packets[i - 1].ext_seq - 1))
    if not silences:
        print("none – packets kept flowing the whole time")
    else:
        print(f"{len(silences)} window(s); longest {max(s[1] for s in silences) / 1e3:.2f} s")
        print(f"{'starts at':>28}  {'length':>9}  {'packets lost meanwhile':>24}")
        for wall, gap_ms, lost in silences[:max_events]:
            flag = "  <-- exceeds QGC 3 s source watchdog -> pipeline teardown + backoff" if gap_ms > 3000 else ""
            print(f"{fmt_wall(wall, t0):>28}  {gap_ms / 1e3:7.2f} s  {lost:24d}{flag}")

    # ---- interpretation hints -------------------------------------------------------------
    print()
    print("=" * 78)
    print("READING THE RESULTS")
    print("=" * 78)
    long_silence = [s for s in silences if s[1] > 3000]
    if long_silence:
        print(f"* {len(long_silence)} silence window(s) longer than 3 s: the RF link (or wfb-ng RX) delivered nothing.")
        print("  QGC's watchdog tears the pipeline down after 3 s and reconnects with 1/2/4/8 s backoff,")
        print("  so each of these costs the fade + backoff + wait-for-keyframe. Fix: keep the pipeline")
        print("  alive for the local WFB source instead of tearing it down.")
    if events and not long_silence:
        print("* Losses occurred while packets kept flowing: black/frozen video until the next keyframe is")
        print("  decoder-side recovery. Shorter GOP (majestic gopSize) or keyframe-on-loss shortens it.")
    if not events and not silences:
        print("* Clean capture: no loss, no silence. If video still dropped, the problem is after the")
        print("  UDP source (decoder / display path) – capture the QGC log with GStreamer debug level 3.")

    # ---- timeline ------------------------------------------------------------------------
    if timeline:
        print()
        print("=" * 78)
        print("PER-SECOND TIMELINE")
        print("=" * 78)
        print(f"{'sec':>5}  {'packets':>7}  {'kB':>7}  {'frames':>6}  {'lost':>5}  {'keyfr':>5}  {'max gap ms':>10}")
        sec = 0
        bucket = []
        idx = 0
        lost_by_sec = {}
        for wall, lost, _s0, _s1, _rec, _hole in events:
            lost_by_sec[int((wall - t0) // 1_000_000)] = lost_by_sec.get(int((wall - t0) // 1_000_000), 0) + lost
        while idx < len(packets):
            bucket = []
            limit = t0 + (sec + 1) * 1_000_000
            while idx < len(packets) and packets[idx].wall_us < limit:
                bucket.append(packets[idx])
                idx += 1
            if bucket:
                gaps = [(b.wall_us - a.wall_us) / 1e3 for a, b in zip(bucket, bucket[1:])]
                print(f"{sec:5d}  {len(bucket):7d}  {sum(p.size for p in bucket) / 1e3:7.1f}  "
                      f"{sum(1 for p in bucket if p.marker):6d}  {lost_by_sec.get(sec, 0):5d}  "
                      f"{sum(1 for p in bucket if p.keyframe_start):5d}  {max(gaps) if gaps else 0:10.1f}")
            else:
                print(f"{sec:5d}  {'-':>7}  {'-':>7}  {'-':>6}  {lost_by_sec.get(sec, 0):5d}  {'-':>5}  {'SILENT':>10}")
            sec += 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("capture", help="*.qgprtp file written by QGroundPixel")
    ap.add_argument("--timeline", action="store_true", help="print a per-second table")
    ap.add_argument("--events", type=int, default=40, help="max loss/silence rows to print (default 40)")
    ap.add_argument("--silence-ms", type=int, default=250, help="silence threshold in ms (default 250)")
    ap.add_argument("--codec", choices=["auto", "h264", "h265"], default="auto", help="payload format (default auto)")
    args = ap.parse_args()
    packets, codec = read_capture(args.capture, args.codec)
    analyze(packets, codec, args.silence_ms, args.events, args.timeline)


if __name__ == "__main__":
    main()
