"""
온도 챔버 드라이버 (PC측) — CHIP1 온도 스윕 자동화용.

프로토콜 근거: docs/"Block diagram_Test system for CHIP1_251204A.pdf" p.3
(팀 실측 기록). ESPEC 매뉴얼 원본은 미확보 — 실측 기록이 우선 소스.

프로파일 2종:
  SU661_GPIB : 115200, Prologix GPIB-USB 컨버터 경유.
               초기화 ++mode 1 / ++auto 1 / ++eos 3 / ++addr 1.
               명령 접두 없음 (TEMP,S50). 습도 명령은 NA 응답 — 미사용.
               MON? 응답의 습도 자리는 빈 값 ("6.7,,OFF,0").
  SH662_RS485: 9600, 종단 LF만 (CR 보내면 동작 안 함!). 명령 접두 "1,".
               (1,TEMP,S50 / 1,MON? ...) 습도 명령 지원.

응답 형식 (실측):
  set 계열  -> "OK:<echo>" 또는 "NA:<사유>" (예: NA:CONTROLLER NOT READY-1)
  TEMP?     -> "22.9,50.0,165.0,-70.0"  (현재, 목표, 상한, 하한)
  MON?      -> "23.1,79,OFF,0"          (온도, 습도, 운전상태, 알람수)

사용:
    from chamber import Chamber
    ch = Chamber("COM7", profile="SH662_RS485")
    ch.power(True); ch.set_temp(-40)
    cur, target = ch.get_temp()
    temp, humi, status = ch.get_mon()
"""

import sys
import time
from pathlib import Path

_VENDOR = str(Path(__file__).resolve().parent / "_vendor")
if Path(_VENDOR).exists() and _VENDOR not in sys.path:
    sys.path.append(_VENDOR)   # 내장 라이브러리 폴백 (pip 설치 불필요)

try:
    import serial
except ImportError:
    serial = None   # MockChamber만 쓰는 단위테스트 환경 허용


class ChamberError(RuntimeError):
    pass


PROFILES = {
    "SU661_GPIB": {
        "baud": 115200,
        "eol": b"\n",          # LF/CR 모두 동작 (실측) — LF 사용
        "prefix": "",
        "prologix_init": ["++mode 1", "++auto 1", "++eos 3", "++addr 1"],
        "has_humidity": False,
    },
    "SH662_RS485": {
        "baud": 9600,
        "eol": b"\n",          # ⚠ LF만 — CR 포함 금지 (실측: CR 동작 안 됨)
        "prefix": "1,",
        "prologix_init": [],
        "has_humidity": True,
    },
}

MIN_CMD_GAP_S = 0.3      # 명령 간 최소 간격 (ESPEC 매뉴얼 p.47: 권고 0.1~1s)
REPLY_TIMEOUT_S = 2.0
RETRIES = 2


class Chamber:
    """실챔버 드라이버. 프로파일별 배선/명령 차이를 흡수한다."""

    def __init__(self, port: str, profile: str = "SH662_RS485"):
        if profile not in PROFILES:
            raise ChamberError(f"unknown profile: {profile} "
                               f"(choose from {list(PROFILES)})")
        if serial is None:
            raise ChamberError("pyserial이 필요합니다: pip install pyserial")
        self.profile_name = profile
        self.p = PROFILES[profile]
        self.ser = serial.Serial(port, self.p["baud"], timeout=0.2)
        self._last_cmd_t = 0.0
        time.sleep(0.3)
        self.ser.reset_input_buffer()

        # Prologix 컨버터 초기화 (++ 명령은 컨버터가 소비, 챔버 응답 없음)
        for cmd in self.p["prologix_init"]:
            self._write_raw(cmd)
            time.sleep(0.1)
        self.ser.reset_input_buffer()

    # ------------- low level -------------

    def _write_raw(self, cmd: str):
        """최소 간격 강제 후 전송. 종단은 프로파일의 eol만 사용."""
        gap = time.time() - self._last_cmd_t
        if gap < MIN_CMD_GAP_S:
            time.sleep(MIN_CMD_GAP_S - gap)
        self.ser.reset_input_buffer()
        self.ser.write(cmd.encode("ascii") + self.p["eol"])
        self.ser.flush()
        self._last_cmd_t = time.time()

    def _read_line(self, timeout_s: float = REPLY_TIMEOUT_S) -> str:
        deadline = time.time() + timeout_s
        buf = b""
        while time.time() < deadline:
            b = self.ser.read(1)
            if not b:
                continue
            if b in (b"\r", b"\n"):
                if buf:
                    return buf.decode(errors="replace").strip()
                continue
            buf += b
        raise ChamberError(f"챔버 응답 타임아웃 ({timeout_s:.0f}s)")

    def _cmd(self, body: str, expect_ok: bool) -> str:
        """접두 붙여 전송, 응답 1줄 반환. 재시도 포함.
        expect_ok=True면 OK:/NA: 검사 (NA -> ChamberError)."""
        full = self.p["prefix"] + body
        last_err = None
        for attempt in range(1 + RETRIES):
            try:
                self._write_raw(full)
                reply = self._read_line()
                if expect_ok:
                    if reply.startswith("OK:"):
                        return reply
                    if reply.startswith("NA:"):
                        raise ChamberError(f"'{full}' 거부됨: {reply}")
                    raise ChamberError(f"'{full}' 예상외 응답: {reply!r}")
                return reply
            except ChamberError as e:
                last_err = e
                if "거부됨" in str(e):
                    raise            # NA는 재시도해도 같음 — 즉시 전달
                time.sleep(0.5)
        raise ChamberError(f"'{full}' 실패 (재시도 {RETRIES}회): {last_err}")

    # ------------- public API -------------

    def power(self, on: bool):
        self._cmd(f"POWER,{'ON' if on else 'OFF'}", expect_ok=True)

    def start(self):
        """운전 시작 시퀀스 (C# 예제 준수): POWER,ON + 습도 알람 한계 L0/H100.
        습도 미지원 프로파일(SU661)은 POWER,ON만."""
        self.power(True)
        if self.p["has_humidity"]:
            self._cmd("HUMI,L0", expect_ok=True)
            self._cmd("HUMI,H100", expect_ok=True)

    def set_temp(self, temp_c: float):
        """목표 온도 설정. 예: set_temp(-40) -> TEMP,S-40

        ⚠ 실측 (2026-07-23, SH-662 실기): 소수점 포함 형식(S25.0)은 매뉴얼과
        달리 NA:PARA ERR로 거부됨 — 정수형(S25, S-40)만 수용. :g 포맷이
        정수 온도를 자동으로 정수형으로 보내므로 정수 온도만 사용할 것.
        목표는 챔버의 절대 상한/하한(TEMP? 3,4번째 필드) 사이여야 함."""
        if temp_c != int(temp_c):
            raise ChamberError(f"소수 온도 미지원 (실측): {temp_c} — 정수 온도만 사용")
        self._cmd(f"TEMP,S{temp_c:g}", expect_ok=True)

    def get_temp(self) -> tuple[float, float]:
        """(현재온도, 목표온도). TEMP? -> '22.9,50.0,165.0,-70.0'"""
        reply = self._cmd("TEMP?", expect_ok=False)
        parts = reply.split(",")
        if len(parts) < 2:
            raise ChamberError(f"TEMP? 파싱 실패: {reply!r}")
        return float(parts[0]), float(parts[1])

    def get_mon(self) -> tuple[float, float | None, str]:
        """(온도, 습도|None, 운전상태).

        응답 형식 (ESPEC 매뉴얼 Table 3.10): '온도[,습도],운전모드,알람수'
        운전모드: OFF / STANDBY / CONSTANT / RUN.
        습도 자리 처리 3형태 모두 수용:
          '23.1,79,OFF,0'   (SH662, 습도 있음)
          '6.7,,OFF,0'      (SU661 실측 — 빈 필드)
          '23.5, CONSTANT, 0' (매뉴얼: SU 챔버는 습도 필드 자체가 생략됨)
        """
        reply = self._cmd("MON?", expect_ok=False)
        parts = [p.strip() for p in reply.split(",")]
        if len(parts) < 3:
            raise ChamberError(f"MON? 파싱 실패: {reply!r}")
        temp = float(parts[0])

        def _num(s: str) -> bool:
            try:
                float(s)
                return True
            except ValueError:
                return False

        if parts[1] and _num(parts[1]):
            humi, status = float(parts[1]), parts[2]     # 습도 있음
        elif parts[1] == "":
            humi, status = None, parts[2]                # 빈 필드 (실측 SU661)
        else:
            humi, status = None, parts[1]                # 필드 생략 (매뉴얼 SU)
        return temp, humi, status

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class MockChamber:
    """모의 챔버 — 상태머신 단위검증용. Chamber와 동일 API.

    1차 지연계 근사: 매 폴링 시점에 목표를 향해 rate_c_per_s * dt 만큼 접근.
    time_fn 주입으로 가상 시간 테스트 가능.
    """

    def __init__(self, start_c: float = 25.0, rate_c_per_s: float = 1.0,
                 time_fn=time.time):
        self.profile_name = "MOCK"
        self.current = start_c
        self.target = start_c
        self.powered = False
        self.rate = rate_c_per_s
        self._time_fn = time_fn
        self._last_t = time_fn()
        self.log: list[str] = []       # 검증용 호출 기록

    def _advance(self):
        now = self._time_fn()
        dt = now - self._last_t
        self._last_t = now
        if not self.powered:
            return
        step = self.rate * dt
        if abs(self.target - self.current) <= step:
            self.current = self.target
        else:
            self.current += step if self.target > self.current else -step

    def power(self, on: bool):
        self._advance()
        self.powered = on
        self.log.append(f"POWER,{'ON' if on else 'OFF'}")

    def start(self):
        self.power(True)
        self.log.append("HUMI,L0")
        self.log.append("HUMI,H100")

    def set_temp(self, temp_c: float):
        self._advance()
        self.target = float(temp_c)
        self.log.append(f"TEMP,S{temp_c:g}")

    def get_temp(self):
        self._advance()
        return round(self.current, 1), self.target

    def get_mon(self):
        self._advance()
        return round(self.current, 1), 50.0, "ON" if self.powered else "OFF"

    def close(self):
        self.log.append("CLOSE")
