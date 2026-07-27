# 빌드 / 플래시 명령 레퍼런스

PC측 도구 명령 모음. 새 명령이 생기면 이 파일에 추가할 것.
(펌웨어 UART 명령은 SPEC.md 섹션 2 참조)

## 산출물 폴더 구조 (2026-07-23 재정리)

```
CHIP1/
├── config/                    ← 보드 고유 설정 (이 보드 전용 — 보드 바뀌면 재캘!)
│   ├── dac_cal.json           ← DAC 2점 캘 계수
│   └── meas_cal.json          ← meas(내장 ADC) 2점 캘 계수
├── results/
│   ├── CHIP1_ADC_validation.xlsx   ← 누적 워크북 (상온 시트 + 스윕 시트들)
│   ├── sweeps/
│   │   ├── 20260723_170629/   ← 스윕 1회 = 폴더 1개 (엑셀 블록 [0723_1706] 태그와 연결)
│   │   │   ├── run_info.txt   ← 조건 요약 (온도/포화/챔버/자극/캘계수) — 재현성
│   │   │   ├── sweep_report.csv
│   │   │   ├── sweep_*.log
│   │   │   └── captures/      ← 그 런의 원시 1024샘플 CSV
│   │   └── _legacy/           ← 재정리 전 과거 스윕 리포트/로그
│   └── manual/                ← 단발 캡처 (capture.py / Run test), _legacy/ 포함
└── (tools/ app/ docs/ 등)
```

- 경로는 tools/chip1_autotest.py 상단에서 단일 관리 (GUI/capture 공통).
- 구버전 위치 폴백 있음 — 흩어진 기존 파일 정리는: `python tools\migrate_layout.py`
  (여러 번 실행해도 안전. Excel이 파일을 잡고 있으면 그 항목만 건너뜀 → 닫고 재실행)
- 엑셀 역추적: 스윕 블록 헤더의 `[MMDD_HHMM]` = results/sweeps/ 폴더명 앞부분.

모든 도구는 STM32CubeCLT 1.22.0 번들 사용: `C:\ST\STM32CubeCLT_1.22.0\`

## 빌드

```powershell
# 최초 1회 (또는 CMakeLists 변경 시)
& "C:\ST\STM32CubeCLT_1.22.0\CMake\bin\cmake.exe" --preset Debug

# 빌드 → build\Debug\CHIP1.elf
& "C:\ST\STM32CubeCLT_1.22.0\CMake\bin\cmake.exe" --build --preset Debug
```

## 플래시 (새 H533RE 보드)

```powershell
& "C:\ST\STM32CubeCLT_1.22.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR -w "build\Debug\CHIP1.elf" -v -rst
```

(프로젝트 루트에서 실행 기준 — 상대 경로)

- `-c port=SWD mode=UR` : SWD 연결 (under-reset 모드)
- `-w <elf>` : 쓰기 (elf는 주소 내장), `-v` : 검증, `-rst` : 플래시 후 리셋

빌드+플래시 한 번에:

```powershell
& "C:\ST\STM32CubeCLT_1.22.0\CMake\bin\cmake.exe" --build --preset Debug; if ($?) { & "C:\ST\STM32CubeCLT_1.22.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR -w "build\Debug\CHIP1.elf" -v -rst }
```

## 보드 식별 (플래시 전 확인)

```powershell
& "C:\ST\STM32CubeCLT_1.22.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR
```

출력의 `Board` / `Device name` 확인:
- **NUCLEO-H533RE, STM32H533 (Device ID 0x478)**

### ⚠ ST-Link 여러 개 연결 시 (기존 테스트 보드 공존)

반드시 시리얼로 대상 지정 — 안 하면 어느 보드에 붙을지 모름:

```powershell
# 연결된 ST-Link 목록
& "C:\ST\STM32CubeCLT_1.22.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -l

# SN 지정 연결/플래시
... -c port=SWD sn=<ST-LINK 시리얼> mode=UR -w ... -v -rst
```

## 캘 값 삭제 (플래시 캘 저장소 소거 — 미캘 시연/재캘용)

보드 플래시에 저장된 DAC 캘 계수만 지운다. 펌웨어 코드(504K)는 무관 — 소거
후에도 보드는 정상 동작하며, 리셋 후 `dac cal show`가 `flash: ch1=none
ch2=none`을 반환하고 GUI가 "⚠ 미캘" 안내를 띄운다.

```powershell
& "C:\ST\STM32CubeCLT_1.22.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR -e 63
```

- `-e 63` = bank2 sector31 (0x0807E000, 마지막 8KB) — 캘 저장소 전용 섹터.
- 복구: GUI 위저드로 재캘(PASS 시 자동 save) 또는 알고 있는 계수를
  `dac cal <ch> <off> <ppm>` → `dac cal save`로 수동 저장.
- ⚠ 펌웨어 재플래시(elf 쓰기)는 캘을 건드리지 않지만, **full chip erase는
  캘도 지운다** (SPEC.md §4).

## 샘플 캡처 → CSV (tools/chip1_capture.py)

`rd` 결과를 CSV로 저장 + 통계(mean/stdev/p-p/drift) 출력.
의존성: `pip install -r requirements.txt` (pyserial/openpyxl).

```powershell
# Internal Short 노이즈 1024샘플 (10SPS, PGA x64, 세틀링 60초 대기)
python tools\chip1_capture.py --port COM5 --count 1024 --setup "wr 0x03 0x02" --setup "wr 0x04 0x60" --settle 60 --label short

# Differential (DAC 자극 + 보정계수 재입력 포함)
python tools\chip1_capture.py --port COM5 --count 1024 --setup "dac init" --setup "dac cal 1 0 -9901" --setup "dac cal 2 400 -10685" --setup "dac set 1 1520000" --setup "dac set 2 1480000" --setup "wr 0x03 0x02" --setup "wr 0x04 0x00" --settle 60 --label diff
```

(cal 계수는 `dac_cal.json`의 최신 값으로 바꿔서 사용 — capture.py는 자동 적용 안 됨.
전압은 표준 자극: set 1=1.52V AINP / set 2=1.48V AINN)

- COM 포트는 장치 관리자에서 확인 (ST-Link VCP)
- `--settle 60`: 설정 직후 수십 초 과도 드리프트 실측됨 (SPEC.md §5) → 대기 권장
- `--discard N`: 저장 전 앞 N개 샘플 폐기 옵션

## 자동 테스트 (tools/chip1_autotest.py) — 유효성 검사용

구 chip1_autotest.py의 v4 펌웨어 이식판. DAC 셋업(cal 포함) → wr/rr 검증 →
Internal Short rd 1024 → Channel A rd 1024 → 엑셀 DUT#n 블록 기입 + CSV 저장.
의존성: `pip install pyserial openpyxl`

```powershell
# 기본: 포트 자동 탐지, 검증 워크북(CHIP1_ADC_validation.xlsx)의 첫 빈 DUT부터
python tools\chip1_autotest.py

# 포트/DUT/세틀링 지정
python tools\chip1_autotest.py --port COM5 --dut 3 --settle-sec 60
```

- 첫 실행 시 `template\report_template.xlsx`를 복사해
  **CHIP1_ADC_validation.xlsx**를 만든다 (템플릿 원본은 수정 안 함).
- CSV는 `captures\dut{n}_{mode}_{ts}.csv`로 병행 저장.
- 전압 (팀 확정 표준): ch1=1.52V(AINP)/ch2=1.48V(AINN) — 차동 +40mV,
  구 엑셀 데이터와 부호까지 일치 (Channel A 평균 +6.9M 근처 기대).

## GUI 테스트 (tools/chip1_gui.pyw)

자동 테스트의 GUI판. 더블클릭 실행 (콘솔 없음, 출력은 로그 창).

- 전압(µV)/샘플 수/세틀링 대기/DUT#/COM 포트를 입력창에서 조정
- DAC cal 계수 자동 입력, 검증 워크북 자동 생성/기입, 완료 시 엑셀 자동 열기
- Internal Short는 STD/ENOB, Channel A는 평균/Vin/정확도를 즉석 요약
- 엑셀 블록이 모자라면 DUT#11, #12... 블록을 템플릿 복사로 자동 확장
- ⚠ 입력창의 dac set 1 = **AINP**(PA4), dac set 2 = **AINN**(PA5) — 구 GUI와 반대
- **DAC Calibration 섹션 (2점 위저드)**: 채널 선택 → 저점(0.5V)/고점(3.0V) 출력·실측
  입력 → 계수 산출·전송 → 중간점(1.75V) ±2mV PASS/FAIL → `config/dac_cal.json` 저장.
  저장된 계수는 CLI/GUI 테스트 시작 시 자동 적용 (없으면 기본 계수)
- **ADC Calibration 섹션**: 기본은 **Self-cal 버튼** (배선 불필요, 수 초) —
  펌웨어 `meas cal`(ADCAL 오프셋 + VREFINT 게인 재기준) 실행 후 DMM으로 AVDD
  구멍 실측 입력 → ±5mV PASS/FAIL → meas_cal.json에 이력 기록.
  **Advanced… 버튼** = 외부 2점 위저드 (D1을 A3/A4에 임시 배선, 셀프캘 검증
  실패 시 백업. 끝나면 임시 배선 원상복구 필수)
- 스윕 옵션 "온도별 자동 셀프캘" (기본 ON): 온도마다 `meas cal` 재실행

## 온도 스윕 (GUI "Temperature Sweep" 섹션)

챔버 연동 자동화 (SPEC.md §8). GUI에서:

1. 온도 리스트(콤마 구분)/포화(분)/허용오차 입력
2. 챔버 프로파일 선택 — `SH662_RS485`(9600, LF만) / `SU661_GPIB`(115200,
   Prologix) / `MOCK`(하드웨어 없이 리허설) + 챔버 COM 포트
3. 라벨(EVM/칩) 입력 시 시트명/CSV명에 반영 — 스윕·보드 구분용
4. Start sweep → 온도마다 도달 대기 → 포화 → 테스트(+`meas` 기록) → 기록
5. 결과: **스윕 전용 새 시트** `SW_MMDD_HHMM[_라벨]` (블록=온도, 상온 검증
   시트는 불변) + `sweep_report_*[_라벨].csv`
6. 중단… 버튼: 즉시 / 현재 스텝 완료 후 선택
7. **종료 후 동작 선택**: `25C 복귀 후 정지`(기본, 정상 완료 시 상온 복귀 대기
   후 POWER,OFF — 최대 60분) / `즉시 정지` / `유지(수동)`. 중단·에러 시에는
   복귀 대기 없이 즉시 정지 (유지 선택 제외)

상태머신 단위검증 (하드웨어 불필요):

```powershell
python tools\test_temp_sweep.py    # MockChamber + 가상 시계, 5개 테스트
```

## 시리얼 콘솔

- ST-Link VCP (장치 관리자에서 `STMicroelectronics STLink Virtual COM Port` 확인)
- **115200 8N1**, 줄바꿈 CR 또는 CRLF
- Tera Term / PuTTY / `python -m serial.tools.miniterm COMx 115200`

---
변경 이력:
- 2026-07-16: 최초 작성 (빌드/플래시/식별/백업/콘솔)
- 2026-07-20: tools/chip1_capture.py (rd→CSV 캡처 헬퍼) 사용법 추가
- 2026-07-20: tools/chip1_autotest.py (자동 테스트 이식판) 사용법 추가
- 2026-07-20: tools/chip1_gui.pyw (GUI 이식판) 사용법 추가
- 2026-07-21: GUI에 2점 캘리브레이션 위저드 추가 (dac_cal.json 자동 적용)
- 2026-07-21: 온도 챔버 연동 — chamber.py/temp_sweep.py/GUI 스윕 섹션,
  펌웨어 meas 명령 (HAL ADC 추가, 재플래시 필요)
- 2026-07-23: 산출물 폴더 재정리 (config/results 구조, migrate_layout.py),
  ADC(meas) 2점 캘 위저드, 스윕 런폴더+run_info.txt, GUI 로그 분할화면
- 2026-07-23: pyserial/openpyxl을 tools/_vendor에 내장 — pip 설치 불필요
- 2026-07-23: meas 셀프캘 (`meas cal` 펌웨어 명령 — **재플래시 필요**, GUI
  Self-cal 버튼, 스윕 온도별 자동 셀프캘). 외부 2점 위저드는 Advanced로
- 2026-07-24: **보드 내 캘 영구저장** (`dac cal save`/`uid` — **재플래시 필요**,
  링커 504K로 변경). GUI 보드 감지(UID+미캘 안내), 엑셀 블록 메타 실측 기입,
  config/board_registry.json (PC별 보드 이력)
