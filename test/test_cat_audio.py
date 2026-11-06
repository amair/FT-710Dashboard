#!/usr/bin/env python3
"""
Phase 4 Test: Verify audio streaming over CAT (rigctld protocol).

TEST PROCEDURE
==============

Phase A — Protocol test (no audio route needed):
    python3 test_cat_audio.py --protocol-only
    Verifies: TCP connection, CAT command response, stream_audio acceptance.

Phase B — Full audio test (dashboard with audio routing enabled):
    1. Start the dashboard:
         ./build-mac-debug/FT710Dashboard-macOS --passcode test123
    2. Open https://localhost:8888/ in browser, login
    3. Enable "Receive Audio" in the dashboard (must show active)
    4. Run this test:
         python3 test_cat_audio.py
    5. Listen to cat_audio_test.wav to verify audio content

PACKET FORMAT
=============
    Binary packets: [uint32_t count LE][count × float32 LE samples]
    Text responses: ASCII, newline-terminated (standard rigctld)
    Sample rate: 48000 Hz mono
"""

import socket
import struct
import wave
import time
import sys
import math
import argparse
import array


def send_line(sock: socket.socket, line: str) -> None:
    """Send a text line (with trailing newline)."""
    sock.sendall((line + "\n").encode("ascii"))


def read_line(sock: socket.socket, timeout: float = 5.0) -> str:
    """Read a single line of text (up to and including newline)."""
    sock.settimeout(timeout)
    data = b""
    while not data.endswith(b"\n"):
        try:
            chunk = sock.recv(1024)
            if not chunk:
                break
            data += chunk
        except socket.timeout:
            break
    return data.decode("ascii", errors="replace")


def read_exact(sock: socket.socket, n: int, timeout: float = 1.0) -> bytes:
    """Read exactly n bytes from socket."""
    sock.settimeout(timeout)
    data = b""
    while len(data) < n:
        try:
            chunk = sock.recv(min(n - len(data), 65536))
            if not chunk:
                break
            data += chunk
        except socket.timeout:
            break
    return data


def test_frequency(sock: socket.socket) -> bool:
    """Test basic CAT protocol by reading frequency."""
    print("  [1/3] Testing CAT protocol (get frequency)...")
    send_line(sock, "f")
    response = read_line(sock, timeout=5.0)
    freq_line = response.strip().split("\n")[0].strip()
    try:
        freq = int(freq_line)
        if 100000 <= freq <= 60000000:
            print(f"        PASS - frequency: {freq} Hz ({freq/1e6:.4f} MHz)")
            return True
        else:
            print(f"        WARN - unexpected frequency value: {freq}")
            return True
    except ValueError:
        print(f"        FAIL - non-numeric response: '{freq_line}'")
        return False


def start_streaming(sock: socket.socket) -> bool:
    """Send stream_audio command and verify response."""
    print("  [2/3] Requesting audio streaming...")
    send_line(sock, "stream_audio")
    response = read_line(sock, timeout=5.0)
    if "RPRT 0" in response:
        print("        PASS - streaming accepted (RPRT 0)")
        return True
    else:
        print(f"        FAIL - unexpected response: '{response.strip()}'")
        return False


def collect_audio(sock: socket.socket, duration: float) -> tuple:
    """Read binary audio packets for the given duration (seconds)."""
    print(f"  [3/3] Collecting audio for {duration:.0f} seconds...")
    all_samples = []
    packet_count = 0
    start_time = time.monotonic()
    sock.settimeout(1.0)

    while time.monotonic() - start_time < duration:
        try:
            count_bytes = read_exact(sock, 4, timeout=1.0)
            if len(count_bytes) < 4:
                break
            count = struct.unpack("<I", count_bytes)[0]
            if count == 0 or count > 10000:
                print(f"        WARN - unexpected sample count: {count}")
                break

            sample_bytes = read_exact(sock, count * 4, timeout=1.0)
            if len(sample_bytes) < count * 4:
                break

            samples = struct.unpack(f"<{count}f", sample_bytes)
            all_samples.extend(samples)
            packet_count += 1

            elapsed = time.monotonic() - start_time
            rate = packet_count / elapsed if elapsed > 0 else 0
            print(f"\r        {packet_count} packets | {len(all_samples)} samples "
                  f"| {rate:.1f} pkt/s | last={max(samples[-16:]):+.4f}    ", end="", flush=True)

        except (socket.timeout, OSError):
            break

    print()
    return all_samples, packet_count


def report_stats(samples: list, packet_count: int, duration: float, protocol_only: bool) -> bool:
    """Print audio statistics. Returns True if test passed."""
    if protocol_only:
        print("\n  PROTOCOL TEST COMPLETE (no audio expected in protocol-only mode)")
        return True

    if not samples:
        print("\n  RESULT: NO AUDIO DATA")
        print("  The CAT streaming mechanism is working (command accepted,")
        print("  polling active) but no samples were received from the tap.")
        print("")
        print("  To get audio, ensure ALL of the following:")
        print("    1. Dashboard is running with the FT-710 connected")
        print("    2. 'Receive Audio' is ENABLED in the web dashboard")
        print("    3. The FT-710's USB Audio device is selected as input")
        print("    4. The radio is tuned to an active signal")
        print("")
        print("  Re-run this test after enabling audio routing.")
        return False

    sample_rate = 48000
    min_val = min(samples)
    max_val = max(samples)
    mean_val = sum(samples) / len(samples)
    rms = (sum(s * s for s in samples) / len(samples)) ** 0.5
    dbfs = 20 * math.log10(rms + 1e-10)

    print(f"\n  {'='*50}")
    print(f"  Audio Statistics")
    print(f"  {'='*50}")
    print(f"    Duration:     {duration:.1f}s")
    print(f"    Packets:      {packet_count} ({packet_count/duration:.1f}/s)")
    print(f"    Samples:      {len(samples)} ({len(samples)/duration/1000:.1f}k/s)")
    print(f"    Min:          {min_val:+.6f}")
    print(f"    Max:          {max_val:+.6f}")
    print(f"    RMS:          {rms:.6f} ({dbfs:.1f} dBFS)")
    print(f"    Sample rate:  {sample_rate} Hz mono")
    print(f"  {'='*50}")

    if abs(max_val) > 0.99:
        print("\n  WARNING: Audio clipping detected (max near ±1.0).")
        print("  Consider reducing receive gain in dashboard.")

    print(f"\n  RESULT: PASS - Audio streaming over CAT is working!")
    return True


def write_wav(samples: list, path: str, sample_rate: int = 48000) -> None:
    """Write samples to a 16-bit mono WAV file."""
    if not samples:
        return
    int_samples = array.array("h")
    for s in samples:
        s = max(-1.0, min(1.0, s))
        int_samples.append(int(s * 32767))

    with wave.open(path, "w") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(int_samples.tobytes())
    size_kb = os.path.getsize(path) / 1024
    print(f"\n  WAV saved:  {path} ({len(samples)/sample_rate:.1f}s, {size_kb:.0f} KB)")
    print(f"  Play with:  afplay {path}")


import os


def main():
    parser = argparse.ArgumentParser(description="Test audio streaming over CAT")
    parser.add_argument("--port", type=int, default=4532, help="CAT TCP port (default: 4532)")
    parser.add_argument("--host", type=str, default="127.0.0.1", help="Host (default: 127.0.0.1)")
    parser.add_argument("--duration", type=float, default=5.0, help="Capture duration seconds (default: 5)")
    parser.add_argument("--output", type=str, default="cat_audio_test.wav", help="Output WAV filename")
    parser.add_argument("--protocol-only", action="store_true",
                        help="Only test CAT protocol + stream_audio acceptance (no audio needed)")
    args = parser.parse_args()

    mode = "Protocol" if args.protocol_only else "Full Audio"
    print("=" * 56)
    print(f"  FT-710 Audio over CAT — Phase 4 Validation [{mode}]")
    print("=" * 56)
    print(f"  Target:     {args.host}:{args.port}")
    print(f"  Duration:   {args.duration}s")
    print()

    # Connect
    print("  Connecting...")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)
        sock.connect((args.host, args.port))
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        print(f"        OK")
    except ConnectionRefusedError:
        print(f"        FAIL - Connection refused. Is the dashboard running?")
        print(f"        Start: ./build-mac-debug/FT710Dashboard-macOS --demo --no-browser")
        sys.exit(1)
    except (socket.timeout, OSError) as e:
        print(f"        FAIL - {e}")
        sys.exit(1)

    passed = True
    try:
        # Step 1: Test CAT protocol
        freq_ok = test_frequency(sock)
        passed = freq_ok

        # Step 2: Start audio streaming
        stream_ok = start_streaming(sock)
        passed = passed and stream_ok

        if args.protocol_only:
            passed = report_stats([], 0, 0, protocol_only=True)
        else:
            # Step 3: Collect audio
            samples, packet_count = collect_audio(sock, args.duration)

            # Report
            passed = report_stats(samples, packet_count, args.duration, protocol_only=False)

            # Save WAV
            if samples:
                write_wav(samples, args.output)

    finally:
        try:
            sock.settimeout(1.0)
            send_line(sock, "quit")
        except (OSError, socket.timeout):
            pass
        sock.close()
        print("\n  Disconnected.")
        print("=" * 56)
        sys.exit(0 if passed else 1)


if __name__ == "__main__":
    main()
