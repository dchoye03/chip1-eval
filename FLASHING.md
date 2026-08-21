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

출력의 `Board` / `Device name` 확인. 새 개발 보드:
- **NUCLEO-H533RE, STM32H533 (Device ID 0x478), ST-Link SN `003400203133511735333335`**

### ⚠ ST-Link 여러 개 연결 시 (기존 테스트 보드 공존)

반드시 시리얼로 대상 지정 — 안 하면 어느 보드에 붙을지 모름:

```powershell
# 연결된 ST-Link 목록
& "C:\ST\STM32CubeCLT_1.22.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -l

# SN 지정 연결/플래시
... -c port=SWD sn=003400203133511735333335 mode=UR -w ... -v -rst
```

## 기존 보드 플래시 백업 (덤프)

**절대 규칙: 백업 완료 전에는 어떤 보드에도 플래싱 금지 (SPEC.md §0)**

```powershell
# 보드별로 파일명 구분해서 저장 (-u = 디바이스 → 파일 읽기)
& "C:\ST\STM32CubeCLT_1.22.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR -u 0x08000000 0x80000 "backup\dac_board_backup.bin"
& "C:\ST\STM32CubeCLT_1.22.0\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR -u 0x08000000 0x80000 "backup\test_board_backup.bin"
```

- `0x80000` = 512KB. 기존 보드 MCU의 플래시 크기에 맞게 조정
  (연결 시 출력되는 flash size 확인. 크게 잡으면 에러 나므로 칩 크기 이하로).

백업 복원 (원상복구):

```powershell
... -c port=SWD mode=UR -w "...\backup\test_board_backup.bin" 0x08000000 -v -rst
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

`rd` 결과를 CSV로 저장 + 통계(mean/stdev/p-p/drift) 출력. 기존 셋업 비교용.
의존성: 없음 — pyserial/openpyxl은 `tools/_vendor`에 내장 (2026-07-23부터).
파이썬 3.x만 있으면 됨. (시스템에 pip 설치본이 있으면 그쪽을 우선 사용)

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

# CHIP1A (I2C 버전 칩) 테스트 — 배선 동일, 인터페이스만 전환
python tools\chip1_autotest.py --iface i2c
```

- 첫 실행 시 `old test\26.7.14.adc.xlsx`를 복사해 데이터만 비운
  **CHIP1_ADC_validation.xlsx**를 만든다 (원본은 절대 수정 안 함).
- CSV는 `captures\dut{n}_{mode}_{ts}.csv`로 병행 저장.
- 전압 (팀 확정 표준): ch1=1.52V(AINP)/ch2=1.48V(AINN) — 차동 +40mV,
  구 엑셀 데이터와 부호까지 일치 (Channel A 평균 +6.9M 근처 기대).

## GUI 테스트 (tools/chip1_gui.pyw)

자동 테스트의 GUI판. 더블클릭 실행 (콘솔 없음, 출력은 로그 창).

- 전압(µV)/샘플 수/세틀링 대기/DUT#/COM 포트를 입력창에서 조정
- **Interface (칩 종류)** 셀렉션: SPI(CHIP1, 기본) / I2C(CHIP1A) — 선택
  즉시 Run/스윕/캘/보드확인 전 경로에 적용 (펌웨어 `iface` 명령 필요)
- 결과 시트 자동 분리: SPI=기본 시트 / I2C=`<시트>_I2C` (상온), 스윕
  시트명 `_I2C` 태그 — 어떤 인터페이스 칩의 결과인지 시트로 구별됨
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

## STOP 전류 측정 (GUI "Stop Current" 섹션 — 패키지 시료 비교)

파워다운 전류를 DM3058E(USB)로 자동 측정. 시퀀스는 팀 테스트 플랜
(SCK 0 → 2ms → SCK 1 → 500µs 유지 → VDD 전류) — SPEC.md §10.

1. 배선: 칩을 캐리어 지그 소켓에 장착, Nucleo에서 SCK/GND 점퍼 (+SDA 선택),
   DMM을 DVDD 공급선과 **직렬**(µA 단자, J207 자리)
2. 의존성: **설치 불필요** — pyvisa 계열도 tools\_vendor 내장 (2026-08-11).
   단 **PC마다 1회 DMM USB 드라이버**는 필요: Zadig → List All Devices →
   "DM3000 SERIES"(1AB1 09C4) → WinUSB 설치. (NI-VISA 있는 PC는 생략)
3. GUI에서 시료 라벨(예: 기존#1/신규#1)과 측정 횟수, **VDD 조건** 선택 →
   **측정** 버튼 — 자동으로 깨우기+id 확인 → PD 진입 → 안정화 대기 →
   N회 읽기 → **PD 유지로 종료** (깨우기 = 다음 측정 시 자동)
   - **VDD = 5V(풀업) 고정** (2026-08-18 단순화): 전원 5V 급전 + 4.7k
     풀업(J206 캡 또는 외부 저항) 전제, `stop pd ext` 자동 사용. 3.3V/
     레벨시프터 모드는 GUI에서 제거 — 펌웨어 명령(stop pd / stop pd pp)은
     유지되어 필요 시 터미널로 가능
4. 결과: `results\stop_current.csv` 누적 (평균/min/max µA + vdd 조건) —
   기존 3개 vs 신규 3개 라벨별로 쌓아서 비교
5. DMM 없이 흐름 리허설: DMM 칸에 `MOCK`

터미널 수동 실행: `stop pd`(내부 풀업, 3.3V) / `stop pd ext`(외부 풀업,
5V+캡) / `stop pd pp`(푸시풀 — 레벨시프터용) → DMM 읽기 → `stop wake`

보고표 생성 (테스트 플랜 형식 엑셀):

```powershell
python tools\stop_report.py         # stop_current.csv -> STOP_current_report.xlsx
python tools\stop_report.py 로트B   # 런(그룹)별: stop_current_로트B.csv -> ..._로트B.xlsx
```

- 라벨 규칙('기존*'=기존 그룹/'빈소켓'=베이스라인/그 외=신규 그룹)대로 측정 후
  실행하면 베이스라인 차감·그룹 평균·판정까지 자동 계산. 그룹 크기는 가변 —
  시료 행은 실제 측정된 라벨로 구성. 측정 추가 시 재실행 = 갱신 (수동 입력 보존).

**새 테스트 그룹 (2026-08-18):**

- **STOP**: GUI "런 이름" 입력 → CSV/보고표가 `stop_current_<이름>.csv` /
  `STOP_current_report_<이름>.xlsx`로 완전 분리 (빈칸 = 기본 캠페인).
  이름 지정 런의 요약 시트는 기준(<10µA)으로 PASS/FAIL 자동 판정.
- **보고표 파일 직접 지정 (2026-08-18)**: GUI "보고표 파일" 찾아보기로 임의 xlsx 선택/생성 — CSV도 같은 이름(.csv)으로 짝 저장, 런 이름보다 우선. CLI: `python tools\stop_report.py 경로\파일.xlsx`.
- **VDD 5V(풀업) 고정 (2026-08-18)**: GUI STOP 측정은 항상 `stop pd ext`
  (전원 5V + 4.7k 풀업 전제). 이전의 3.3V/레벨시프터 선택지는 UI에서 제거
  (펌웨어 `stop pd`/`stop pd pp`는 터미널용으로 잔존).
- **ADC (Run test)**: GUI "결과 시트" 입력 → 그 이름의 시트에 기록 (없으면
  기본 시트 레이아웃을 복제해 새 시트 자동 생성 — 데이터 비움, 수식 보존).
  빈칸 = 기존 자동 라우팅(SPI/I2C 시트).
- **빈 엑셀 자동 시딩 (2026-08-18)**: 대상 엑셀이 없거나, 사용자가 만든
  **빈 통합문서**면 템플릿 레이아웃을 자동으로 채워 넣음. 값이 있는데 DUT
  블록이 없는 파일은 데이터 보호를 위해 안내 후 중단. (STOP 보고표는 원래
  CSV에서 전부 자동 생성이라 해당 없음)

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
- 2026-07-29: **CHIP1A(I2C) 지원** — 펌웨어 `iface`/`iscan` 명령(**재플래시
  필요**), autotest `--iface`, GUI Interface 셀렉션 + 로고.
  실기 검증: A칩 EVM에서 iscan 0x2A + id 0x9210 확인 (EVM R14 풀다운은
  SCL 푸시풀 구동으로 해결 — SPEC.md §3.5)
- 2026-08-07: 엑셀 시트 인터페이스별 분리(`*_SPI`/`*_I2C`, 중복 생성 방지),
  블록 헤더 날짜 실측 갱신
- 2026-08-10: **STOP 전류 측정** — 펌웨어 `stop pd|wake`(**재플래시 필요**),
  DM3058E 드라이버(tools/dmm_dm3058.py), GUI "Stop Current" 섹션,
  results/stop_current.csv (SPEC.md §10)
- 2026-08-11: STOP 측정 최종 구성 — 보드 분리(오프셋≈0), SCK 오픈드레인
  구동(캐리어 내장 풀업으로 High=5V, **재플래시 필요**), 보고표 생성기
  tools/stop_report.py (상세: (내부 기록))

