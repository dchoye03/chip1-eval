"""temp_sweep 상태머신 단위검증 — MockChamber + 가상 시계 (하드웨어 불필요).

실행: python tools/test_temp_sweep.py
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from chamber import MockChamber
from temp_sweep import SweepConfig, TempSweep


class FakeClock:
    """가상 시계: sleep이 시간을 전진시켜 수 분짜리 스윕을 즉시 실행."""

    def __init__(self):
        self.t = 0.0

    def time(self):
        return self.t

    def sleep(self, s):
        self.t += max(s, 0.001)


def make_sweep(temps, clock, soak_min=1.0, rate=5.0, on_status=None,
               test_rows=1):
    chamber = MockChamber(start_c=25.0, rate_c_per_s=rate, time_fn=clock.time)
    chamber.power(True)
    calls = []

    def run_test_fn(temp_c):
        calls.append(temp_c)
        return [{"set_temp": temp_c, "row": k} for k in range(test_rows)]

    states = []

    def cb(st):
        if not states or states[-1] != st.state:
            states.append(st.state)
        if on_status:
            on_status(st, sweep)

    sweep = TempSweep(chamber, run_test_fn,
                      SweepConfig(temps=temps, soak_min=soak_min, tol_c=1.0,
                                  poll_s=10.0, wait_timeout_min=60.0),
                      status_cb=cb, time_fn=clock.time, sleep_fn=clock.sleep)
    return sweep, chamber, calls, states


def test_normal_flow():
    clock = FakeClock()
    sweep, chamber, calls, states = make_sweep([-40, 85], clock)
    st = sweep.run()
    assert st.state == "DONE", st
    assert calls == [-40, 85], calls
    assert len(st.rows) == 2, st.rows
    # 온도당 상태 순서 확인
    expect = ["SET_TEMP", "WAIT_TARGET", "SOAK", "TEST", "REPORT"] * 2 + ["DONE"]
    assert states == expect, states
    # 챔버에 실제 세팅 명령이 순서대로 나갔는지
    assert "TEMP,S-40" in chamber.log and "TEMP,S85" in chamber.log
    print("PASS test_normal_flow  (virtual %.0f min)" % (clock.t / 60))


def test_immediate_abort_in_wait():
    clock = FakeClock()

    def on_status(st, sweep):
        if st.state == "WAIT_TARGET":
            sweep.request_stop(immediate=True)

    sweep, chamber, calls, states = make_sweep([-40, 85], clock,
                                               on_status=on_status)
    st = sweep.run()
    assert st.state == "ABORTED", st
    assert calls == [], calls                 # 테스트 진입 전 중단
    assert "TEST" not in states, states
    print("PASS test_immediate_abort_in_wait")


def test_step_abort_finishes_current():
    clock = FakeClock()

    def on_status(st, sweep):
        if st.state == "TEST" and st.temp_idx == 0:
            sweep.request_stop(immediate=False)   # 현재 스텝 마치고 중단

    sweep, chamber, calls, states = make_sweep([-40, 85], clock,
                                               on_status=on_status)
    st = sweep.run()
    assert st.state == "ABORTED", st
    assert calls == [-40], calls              # 첫 온도만 완료
    assert len(st.rows) == 1, st.rows         # REPORT까지 마침
    assert states.count("REPORT") == 1, states
    print("PASS test_step_abort_finishes_current")


def test_wait_timeout():
    clock = FakeClock()
    sweep, chamber, calls, states = make_sweep([-40], clock, rate=0.0001)
    sweep.cfg.wait_timeout_min = 5.0
    st = sweep.run()
    assert st.state == "ABORTED" and "도달 실패" in st.message, st
    assert calls == [], calls
    print("PASS test_wait_timeout")


def test_tolerance_judgement():
    clock = FakeClock()
    sweep, chamber, calls, states = make_sweep([26.0], clock, rate=5.0)
    # 시작 25.0, 목표 26.0, tol ±1.0 -> 즉시 도달 판정이어야 함
    st = sweep.run()
    assert st.state == "DONE", st
    assert states.count("WAIT_TARGET") == 1
    print("PASS test_tolerance_judgement")


if __name__ == "__main__":
    test_normal_flow()
    test_immediate_abort_in_wait()
    test_step_abort_finishes_current()
    test_wait_timeout()
    test_tolerance_judgement()
    print("\nALL PASS")
