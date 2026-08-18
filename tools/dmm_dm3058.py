"""Rigol DM3058E 디지털 멀티미터 드라이버 — STOP(파워다운) 전류 측정용.

의존성: **설치 불필요** — pyvisa/pyvisa-py/pyusb/libusb가 tools/_vendor에
내장 (2026-08-11부터, pyserial/openpyxl과 동일 원칙).

⚠ 단 PC마다 1회, DMM의 USB 드라이버는 필요 (드라이버는 파이썬으로 배포
불가): Zadig 실행 → Options>List All Devices → "DM3000 SERIES"(USB ID
1AB1 09C4) 선택 → WinUSB 설치. (NI-VISA가 이미 깔린 PC는 이 단계 불필요.)

SCPI (DM3058 프로그래밍 매뉴얼, RIGOL 커맨드셋):
  *IDN?                         → Rigol Technologies,DM3058E,...
  :FUNCtion:CURRent:DC          → DC 전류 모드 전환
  :MEASure:CURRent:DC 0         → 레인지 선택 (0=200uA, 1=2mA, 2=20mA,
                                   3=200mA, 4=2A, 5=10A)  — STOP 전류는 uA대
                                   이므로 기본 0. 과범위가 의심되면 1로.
  :MEASure:CURRent:DC?          → 측정값 1개 (A 단위, 과학표기)

MOCK 모드: resource='MOCK' — 하드웨어 없이 GUI/흐름 리허설 (2~4uA 난수).

배선 주의: DMM은 전원 공급선과 **직렬**(전류 모드, uA 단자)로 들어가야 한다.
캐리어 지그 보드는 DVDD 라인에 2핀 점퍼 헤더(J207)가 있어 그 자리를 끊고
DMM을 삽입하면 된다 (J207 1번 ↔ 2번 사이에 DMM +/-).
"""
from __future__ import annotations

import random
import sys
import time
from pathlib import Path

# 내장 라이브러리 폴백 (단독 실행 대비 — GUI 경유 시엔 autotest가 이미 추가)
_VENDOR = str(Path(__file__).resolve().parent / "_vendor")
if Path(_VENDOR).exists() and _VENDOR not in sys.path:
    sys.path.append(_VENDOR)


class DmmError(Exception):
    """DMM 연결/통신 실패."""


class DM3058:
    RANGES = {0: "200uA", 1: "2mA", 2: "20mA", 3: "200mA", 4: "2A", 5: "10A"}

    def __init__(self, resource: str | None = None, timeout_ms: int = 3000):
        self._mock = (resource == "MOCK")
        self._inst = None
        if self._mock:
            self.idn = "MOCK,DM3058E,0,0.0"
            return

        try:
            import pyvisa
        except ImportError as exc:
            raise DmmError(
                "pyvisa 로드 실패 — tools/_vendor가 온전한지 확인 "
                "(정상이면 설치 불필요)") from exc

        # pyvisa-py(@py)의 USBTMC는 pyusb를 쓰는데, Windows에는 libusb DLL이
        # 기본 경로에 없다. libusb_package의 DLL로 백엔드를 미리 초기화해 두면
        # (pyusb가 캐시) 이후 pyvisa-py가 그대로 재사용한다. (8248 transport.py
        # 와 동일 기법 — 이거 없으면 장치가 있어도 list_resources가 빈 목록.)
        try:
            import libusb_package
            import usb.backend.libusb1
            usb.backend.libusb1.get_backend(
                find_library=libusb_package.find_library)
        except ImportError:
            pass    # NI-VISA 등 정식 VISA가 있으면 불필요

        rm = None
        errs = []
        for backend in ("", "@py"):        # 설치된 VISA 우선, 다음 pyvisa-py
            try:
                rm = pyvisa.ResourceManager(backend) if backend \
                    else pyvisa.ResourceManager()
                break
            except Exception as e:         # noqa: BLE001 - 백엔드별 다양한 예외
                errs.append(f"{backend or 'ivi'}: {e}")
        if rm is None:
            raise DmmError("VISA 백엔드 초기화 실패: " + " / ".join(errs))

        if resource is None:
            # Rigol VID: NI-VISA는 16진(0x1AB1), pyvisa-py는 10진(6833) 표기.
            # 시리얼(DM3R...)의 'DM3' 프리픽스도 DM3000 계열 식별자로 사용.
            try:
                cands = [r for r in rm.list_resources("USB?*::INSTR")
                         if "0X1AB1" in r.upper() or "::6833::" in r
                         or "DM3" in r.upper()]
            except Exception:
                cands = []
            if not cands:
                raise DmmError(
                    "DM3058을 USB에서 찾지 못함 — USB 케이블/전원 확인. "
                    "장치관리자에 'USB Test and Measurement Device'로 보여야 "
                    "합니다 (안 보이면 NI-VISA 설치 또는 Zadig 절차 필요).")
            resource = cands[0]

        try:
            self._inst = rm.open_resource(resource)
            self._inst.timeout = timeout_ms
            self.idn = self._inst.query("*IDN?").strip()
        except Exception as exc:           # noqa: BLE001
            raise DmmError(f"DMM 열기 실패 ({resource}): {exc}") from exc
        if "DM3058" not in self.idn.upper():
            raise DmmError(f"DM3058이 아님: {self.idn!r}")

    # ------------------------------------------------------------------
    def set_dci(self, range_code: int | None = None):
        """DC 전류 모드. range_code=None → **오토레인지** (값 크기에 맞는
        레인지를 계측기가 선택 — µA대는 µA 분해능, mA대는 mA 분해능).
        고정 레인지가 필요하면 RANGES 코드(0~5) 지정."""
        if self._mock:
            return
        self._inst.write(":FUNCtion:CURRent:DC")
        time.sleep(0.2)
        if range_code is None:
            self._inst.write(":MEASure AUTO")
        else:
            self._inst.write(f":MEASure:CURRent:DC {int(range_code)}")
        time.sleep(0.2)

    def read_current(self) -> float:
        """DC 전류 1회 측정 (A 단위, 절대값 기준 유효성 검사 포함).

        과범위/무효 시 계측기가 9.9E37류 센티널을 반환한다 (실사고:
        -4.5E+22가 CSV에 기록됨). |I| > 0.1A는 물리적으로 불가능한
        측정이므로 무효 처리하고 1회 재시도 후 실패 시 예외."""
        if self._mock:
            time.sleep(0.15)
            return random.uniform(2.0e-6, 4.0e-6)
        last = None
        for _attempt in range(2):
            for q in (":MEASure:CURRent:DC?", "MEAS:CURR:DC?"):
                try:
                    v = float(self._inst.query(q).strip())
                except Exception:          # noqa: BLE001 - 다음 표기/재시도
                    continue
                last = v
                if abs(v) < 0.1:           # 100mA 미만 = 정상 판독
                    return v
            time.sleep(0.4)                # 무효값 → 잠깐 쉬고 재시도
        raise DmmError(f"무효 판독 (과범위/센티널: {last!r}) — "
                       "레인지/배선 확인 필요")

    def close(self):
        if self._inst is not None:
            try:
                self._inst.close()
            except Exception:              # noqa: BLE001
                pass
            self._inst = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


if __name__ == "__main__":
    import sys
    res = sys.argv[1] if len(sys.argv) > 1 else None
    with DM3058(res) as d:
        print("IDN:", d.idn)
        d.set_dci(None)  # 오토레인지 (STOP 테스트 실전과 동일)
        for i in range(5):
            v = d.read_current()
            print(f"  {i + 1}: {v * 1e6:.3f} uA")
