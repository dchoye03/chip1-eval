"""
온도 스윕 상태머신 — 챔버 + CHIP1 테스트 자동화 (SPEC.md §8).

SET_TEMP → WAIT_TARGET(폴링) → SOAK(포화) → TEST → REPORT → 다음 온도

GUI와 분리된 순수 로직: 챔버 객체 / 테스트 함수 / 시계 / 콜백을 주입받아
MockChamber + 가상 시계로 하드웨어 없이 단위검증 가능.

중단(안전 정지) 2모드:
  request_stop(immediate=True)  : 폴링/카운트다운 지점에서 즉시 중단
  request_stop(immediate=False) : 현재 온도 스텝(REPORT까지) 마치고 중단
"""

import threading
import time
from dataclasses import dataclass, field


class SweepAbort(Exception):
    pass


@dataclass
class SweepConfig:
    temps: list          # 섭씨 온도 리스트 (예: [-40,-20,0,10,25,40,70,85])
    soak_min: float      # 목표 도달 후 포화 시간 (분)
    tol_c: float = 1.0   # 도달 판정 허용오차 (±°C)
    poll_s: float = 10.0  # WAIT_TARGET/SOAK 중 MON? 폴링 주기
    wait_timeout_min: float = 90.0   # 목표 도달 최대 대기 (안전장치)


@dataclass
class SweepStatus:
    state: str = "IDLE"
    temp_idx: int = 0          # 진행 중 온도 인덱스 (0-base)
    temp_total: int = 0
    set_temp: float = 0.0
    chamber_temp: float | None = None
    soak_remaining_s: float = 0.0
    message: str = ""
    rows: list = field(default_factory=list)   # REPORT에서 축적된 CSV 행들


class TempSweep:
    def __init__(self, chamber, run_test_fn, config: SweepConfig,
                 status_cb=None, time_fn=time.time, sleep_fn=None):
        """
        chamber     : Chamber/MockChamber (set_temp/get_mon/power API)
        run_test_fn : run_test_fn(temp_c) -> list[dict] — 한 온도에서 테스트
                      실행 후 CSV 행(dict) 목록 반환 (GUI가 실제 구현 주입)
        status_cb   : status_cb(SweepStatus) — 진행 상태 통지 (GUI 표시용)
        time_fn/sleep_fn : 가상 시계 주입용 (단위테스트)
        """
        self.chamber = chamber
        self.run_test_fn = run_test_fn
        self.cfg = config
        self.status_cb = status_cb or (lambda s: None)
        self.time = time_fn
        self.sleep = sleep_fn or time.sleep
        self.status = SweepStatus(temp_total=len(config.temps))
        self._stop_event = threading.Event()
        self._stop_immediate = False

    # ---------------- control ----------------

    def request_stop(self, immediate: bool):
        self._stop_immediate = immediate
        self._stop_event.set()

    def _check_abort(self):
        """즉시 중단 요청 시 SweepAbort. 스텝 후 중단은 온도 루프에서 처리."""
        if self._stop_event.is_set() and self._stop_immediate:
            raise SweepAbort("즉시 중단 요청")

    # ---------------- helpers ----------------

    def _set_state(self, state: str, msg: str = ""):
        self.status.state = state
        self.status.message = msg
        self.status_cb(self.status)

    def _poll_chamber(self) -> float:
        temp, _humi, _st = self.chamber.get_mon()
        self.status.chamber_temp = temp
        self.status_cb(self.status)
        return temp

    def _wait_poll(self):
        """폴링 주기만큼 쉬되, 중단 요청에 1초 단위로 반응."""
        end = self.time() + self.cfg.poll_s
        while self.time() < end:
            self._check_abort()
            self.sleep(min(1.0, max(0.0, end - self.time())))

    # ---------------- states ----------------

    def _wait_target(self, target: float):
        self._set_state("WAIT_TARGET", f"{target:g}C 도달 대기")
        deadline = self.time() + self.cfg.wait_timeout_min * 60.0
        while True:
            self._check_abort()
            temp = self._poll_chamber()
            if abs(temp - target) <= self.cfg.tol_c:
                return
            if self.time() > deadline:
                raise SweepAbort(
                    f"{target:g}C 도달 실패 ({self.cfg.wait_timeout_min:g}분 초과, "
                    f"현재 {temp:g}C)")
            self._wait_poll()

    def _soak(self):
        self._set_state("SOAK", f"포화 {self.cfg.soak_min:g}분")
        end = self.time() + self.cfg.soak_min * 60.0
        while True:
            self._check_abort()
            remaining = end - self.time()
            self.status.soak_remaining_s = max(0.0, remaining)
            self._poll_chamber()
            if remaining <= 0:
                self.status.soak_remaining_s = 0.0
                return
            self._wait_poll()

    # ---------------- main ----------------

    def run(self) -> SweepStatus:
        """스윕 실행. 반환: 최종 SweepStatus (state=DONE/ABORTED/ERROR)."""
        try:
            for i, target in enumerate(self.cfg.temps):
                self.status.temp_idx = i
                self.status.set_temp = target

                self._set_state("SET_TEMP", f"TEMP,S{target:g}")
                self._check_abort()
                self.chamber.set_temp(target)

                self._wait_target(target)
                self._soak()

                self._set_state("TEST", f"{target:g}C 테스트 실행")
                self._check_abort()
                rows = self.run_test_fn(target)

                self._set_state("REPORT", f"{target:g}C 결과 기록")
                self.status.rows.extend(rows or [])
                self.status_cb(self.status)

                # '현재 스텝 후 중단' 처리 지점
                if self._stop_event.is_set():
                    self._set_state("ABORTED",
                                    f"스텝 완료 후 중단 ({i + 1}/{len(self.cfg.temps)})")
                    return self.status

            self.status.temp_idx = len(self.cfg.temps)
            self._set_state("DONE", "전체 온도 완료")
            return self.status

        except SweepAbort as e:
            self._set_state("ABORTED", str(e))
            return self.status
        except Exception as e:                      # noqa: BLE001 — GUI에 보고
            self._set_state("ERROR", f"{type(e).__name__}: {e}")
            return self.status
