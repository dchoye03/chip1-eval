#!/usr/bin/env python3
"""CHIP1 캡처 헬퍼: 'rd <count>' 결과를 CSV로 저장 + 기초 통계 출력.

기존 DAC1220 셋업 결과(엑셀)와 비교하기 위한 데이터 수집용.

사용 예:
  # Internal Short 노이즈 캡처 (10SPS, PGA x64) — 약 102초
  python tools/chip1_capture.py --port COM5 --count 1024 \
      --setup "wr 0x03 0x02" --setup "wr 0x04 0x60" \
      --settle 60 --label short

  # Differential 캡처 (DAC 보정계수 재입력 포함)
  python tools/chip1_capture.py --port COM5 --count 1024 \
      --setup "dac init" --setup "dac cal 1 0 -10137" --setup "dac cal 2 0 -10529" \
      --setup "dac set 1 1480000" --setup "dac set 2 1520000" \
      --setup "wr 0x03 0x02" --setup "wr 0x04 0x00" \
      --settle 60 --label diff

의존성: pip install pyserial
출력: rd_<label>_<timestamp>.csv (index,code 두 컬럼) + 콘솔 통계 요약.

주의:
  - 설정 변경 직후 수십 초의 과도 드리프트가 실측됨 (SPEC.md §5) →
    --settle <초> 로 캡처 전 대기 권장 (대기 중 변환은 버림).
  - 펌웨어 계약: 모든 명령은 'OK' 또는 'ERR ...'로 끝남. rd는 샘플별 한 줄.
"""

import argparse
import statistics
import sys
import time
from datetime import datetime
from pathlib import Path

_VENDOR = str(Path(__file__).resolve().parent / "_vendor")
if Path(_VENDOR).exists() and _VENDOR not in sys.path:
    sys.path.append(_VENDOR)   # 내장 라이브러리 폴백 (pip 설치 불필요)

try:
    import serial
except ImportError:
    sys.exit("pyserial이 필요합니다 (tools/_vendor 누락?): pip install pyserial")

# 단발 캡처 기본 저장 위치 (폴더 구조: FLASHING.md 참조)
MANUAL_DIR = Path(__file__).resolve().parent.parent / "results" / "manual"


def read_line(ser, timeout_s):
    """한 줄 수신 (\\r\\n 종료). 프롬프트 '> ' 잔여물은 벗겨낸다."""
    deadline = time.time() + timeout_s
    buf = b""
    while time.time() < deadline:
        chunk = ser.read(1)
        if not chunk:
            continue
        if chunk == b"\n":
            line = buf.decode(errors="replace").strip()
            while line.startswith("> "):
                line = line[2:].strip()
            if line:
                return line
            buf = b""
            deadline = time.time() + timeout_s
            continue
        if chunk != b"\r":
            buf += chunk
    raise TimeoutError(f"응답 타임아웃 ({timeout_s}s)")


def send_cmd(ser, cmd, timeout_s=5.0, on_sample=None):
    """명령 전송 후 OK까지 수신. 숫자 줄은 on_sample 콜백으로 전달."""
    ser.reset_input_buffer()
    ser.write((cmd + "\r").encode())
    while True:
        line = read_line(ser, timeout_s)
        if line == cmd:               # 펌웨어 에코
            continue
        if line == "OK":
            return
        if line.startswith("ERR"):
            raise RuntimeError(f"'{cmd}' 실패: {line}")
        try:
            value = int(line, 0)
        except ValueError:
            continue                  # 배너 등 무관한 줄 무시
        if on_sample:
            on_sample(value)


def main():
    p = argparse.ArgumentParser(description="CHIP1 rd 캡처 → CSV")
    p.add_argument("--port", required=True, help="시리얼 포트 (예: COM5)")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("--count", type=int, default=1024, help="샘플 수 (기본 1024)")
    p.add_argument("--setup", action="append", default=[],
                   help="캡처 전 실행할 명령 (반복 지정 가능)")
    p.add_argument("--settle", type=float, default=0,
                   help="설정 후 캡처 전 대기 초 (과도 드리프트 회피)")
    p.add_argument("--discard", type=int, default=0,
                   help="캡처 결과 앞쪽 N개 샘플 폐기")
    p.add_argument("--label", default="capture", help="출력 파일명 라벨")
    p.add_argument("--out", default=None, help="CSV 경로 (기본: 자동 파일명)")
    args = p.parse_args()

    if args.out:
        out_path = Path(args.out)
    else:
        MANUAL_DIR.mkdir(parents=True, exist_ok=True)
        out_path = MANUAL_DIR / (
            f"rd_{args.label}_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv")

    with serial.Serial(args.port, args.baud, timeout=0.2) as ser:
        time.sleep(0.3)
        # 재동기화: 이전 세션이 rd 중에 끊겼으면 잔여 샘플이 이번 캡처에 섞임
        ser.write(b"\x1b")            # 진행 중 rd 중단 (ESC)
        ser.flush()
        time.sleep(0.6)
        ser.reset_input_buffer()

        for cmd in args.setup:
            print(f"[setup] {cmd}")
            send_cmd(ser, cmd, timeout_s=5.0)

        if args.settle > 0:
            print(f"[settle] {args.settle:.0f}s 대기 (과도 구간 회피)")
            time.sleep(args.settle)

        samples = []
        t0 = time.time()

        def on_sample(v):
            samples.append(v)
            if len(samples) % 50 == 0:
                print(f"  {len(samples)}/{args.count}")

        print(f"[capture] rd {args.count} 시작 "
              f"(10SPS 기준 약 {args.count / 10:.0f}초)")
        # 샘플당 DRDY 타임아웃 500ms + 여유 → 줄 간 타임아웃 5s
        send_cmd(ser, f"rd {args.count}", timeout_s=5.0, on_sample=on_sample)
        elapsed = time.time() - t0

    if args.discard:
        samples = samples[args.discard:]

    with open(out_path, "w", newline="") as f:
        f.write("index,code\n")
        for i, v in enumerate(samples):
            f.write(f"{i},{v}\n")

    n = len(samples)
    print(f"\n[done] {n}개 저장 → {out_path}  ({elapsed:.1f}s)")
    if n >= 2:
        mean = statistics.fmean(samples)
        stdev = statistics.stdev(samples)
        print(f"  mean   = {mean:,.1f}")
        print(f"  stdev  = {stdev:,.1f}  (RMS noise)")
        print(f"  min/max= {min(samples):,} / {max(samples):,} "
              f"(p-p {max(samples) - min(samples):,})")
        drift = statistics.fmean(samples[n // 2:]) - statistics.fmean(samples[:n // 2])
        print(f"  drift  = {drift:,.1f}  (후반 평균 - 전반 평균; 큰 값이면 세틀링 부족)")


if __name__ == "__main__":
    main()
