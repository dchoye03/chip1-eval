# CHIP1 로드셀 평가 유닛 펌웨어 — 기술 스펙

NUCLEO-H533RE 1장으로 CHIP1(24bit ΔΣ ADC, HX711류 2선 인터페이스) 센서보드를
평가하는 펌웨어 + PC 자동화 도구의 기술 계약 문서. 코드 주석의 `SPEC.md §n`
참조는 본 문서의 섹션 번호를 가리킨다.

> **주:** "CHIP1"은 의도적으로 지은 가상 이름이다. 실제 대상은 미공개 칩이며
> 실명·데이터시트·회로도는 비공개 — 식별 가능한 내용은 전부 개명/제거했다.

## 0. 절대 규칙

- **CHIP1은 표준 SPI가 아님** (§3). STM32 SPI 페리페럴 금지, GPIO 비트뱅잉.
- 핀 할당은 `app/board_pins.h` 한 파일에만 정의. 단 PA4/PA5는 DAC1 출력으로
  칩에 고정 (변경 불가).
- PA5는 원래 LD2(사용자 LED) → DAC1_OUT2 전용으로 전환. LED 기능 전면 제거.
  (CubeMX 재생성 시 main.c에 BSP_LED_Init가 되살아나면 다시 삭제할 것.)

## 1. 하드웨어 구성

```
Nucleo-H533RE ── 점퍼선 6가닥 ──> 어댑터 보드(2.54mm) ──> CHIP1 센서보드
(자극원 = MCU 내장 DAC. 로드셀 신호선은 AINP/AINN에서 분리)
```

| Nucleo 쪽 | 상대측 | 역할 |
|---|---|---|
| 3V3 | DVDD | 전원 (3.3V 운용 — 레벨시프터 없이 직결하기 위함) |
| GND | DGND | 접지 |
| A0 (PA0) | SCK | 비트뱅잉 클럭 |
| A1 (PA1) | DOUT(SDA) | 비트뱅잉 데이터 |
| D1 (PA4, SB19 제거+SB22 실장 개조) | AINP | DAC ch1 |
| D13 (PA5) | AINN | DAC ch2 |
| A3 (PB0) | DVDD 노드 | meas: 공급 전압 측정 |
| A4 (PC1) | VDDA/REFIN 노드 | meas: 레퍼런스 측정 (기대 3.0V) |

- 콘솔: USART2 (PA2/PA3, ST-Link VCP), 115200 8N1.
- 보드 LED 없음(PA5를 DAC으로 사용) — 생존 확인은 UART 프롬프트로.
- 칩의 VDDA/REFIN은 내부 LDO 3.0V 출력 (외부 공급 금지). 전원 인가 →
  1.5ms 후부터 인터페이스 사용 가능.
- VDDA/REFIN 측정 탭은 보드 개조(레퍼런스 노드 인출)가 된 보드에서만 유효 —
  미개조 보드는 해당 홀이 고립 라인이라 부유 전압(1.2V대)이 측정되며 무효.

## 2. UART 명령 인터페이스 (CLI)

인자는 10진수 기본, `0x` 접두 시 16진수.

| 명령 | 동작 | 응답 |
|---|---|---|
| `wr <addr> <val>` | 레지스터 쓰기 (8bit) | `OK` |
| `rr <addr>` | 레지스터 읽기 | `0x??` + `OK` |
| `rd <count>` | count개 샘플 캡처 (줄당 1샘플 스트리밍) | 샘플들 + `OK` |
| `id` | 칩 ID 읽기 (0x3E,0x3F) | `0x9210` + `OK` |
| `dac init` | DAC1 양 채널 활성화 + 0V | `OK` |
| `dac set <ch> <uV>` | 보정 적용 후 µV 단위 출력 | `OK` / `ERR out of range (max 3300000)` |
| `dac cal <ch> <offset_uV> <gain_ppm>` | 채널 보정계수 설정 (RAM) | `OK` |
| `dac cal show` | 계수 + 채널별 플래시 저장 상태 | 채널별 1줄 + `flash: ...` + `OK` |
| `dac cal save [ch]` | 계수를 플래시에 영구 저장 (채널 지정 시 머지) | `OK` / `ERR flash save failed` |
| `uid` | MCU 96bit 고유 ID | `UID=XXXXXXXX-XXXXXXXX-XXXXXXXX` + `OK` |
| `meas` / `meas cal` | 노드 측정 / ADC 셀프캘 (§8) | 값 + `OK` |
| `help` | 명령 목록 | 목록 + `OK` |

- 순수 ASCII, 줄바꿈 `\r\n`, 수신은 `\r`/`\n`/`\r\n` 허용. flat 프롬프트 `> `.
- 모든 명령은 `OK` 또는 `ERR <이유>`로 종료 (PC측 파싱 계약).
- `rd`: 부호 있는 10진수(24bit two's complement → int32 부호확장), 줄마다 즉시
  전송, 진행 중 ESC(0x1B) 수신 시 `ERR aborted`. 샘플당 DRDY 타임아웃 500ms.
- 구현: UART RX 인터럽트 + 링버퍼, 라인 파싱.

## 3. CHIP1 드라이버 — ⚠ 표준 SPI 아님 (2선 펄스 프로토콜)

핀: SCK(출력), SDA(양방향 단선). GPIO 비트뱅잉, µs 딜레이는 DWT 사이클카운터.
tSCK min 125ns → High/Low 각 1µs 사용.

### 3.1 주의점 (§4 참조 표기가 이 섹션을 가리키기도 함)

- **SCK를 100ms 이상 High로 유지하면 Power-Down 진입** → SCK idle은 반드시
  Low. 해제는 SCK Low 후 350ms(tWU) 대기.
- 데이터 업데이트 구간(≈32µs) 중 클럭 금지 → SDA 하강엣지(DRDY) 감지 후
  읽으면 자동 회피.
- 채널 전환/입력 급변 후 2~3샘플 세틀링 무효.

### 3.2 데이터 읽기 프레임 (1샘플)

1. SDA 입력 모드, SDA Low 대기 = DRDY (타임아웃 500ms → `ERR`).
2. SCK 펄스 1~24: 상승엣지마다 1비트, MSB first → 24bit two's complement.
3. SCK 펄스 25~27: 상태비트 ST0/ST1 + SDA High 강제. 항상 27펄스까지 출력.
4. 부호확장: `if (raw & 0x800000) raw |= 0xFF000000;`

### 3.3 레지스터 접근 (데이터 프레임의 연장)

27펄스에 이어: 28~29 설정 모드 진입 / 30~35 주소 6bit (호스트 드라이브) /
36 R·/W / (37 방향 전환 갭) / 38~45 데이터 8bit / 46 SDA High 종료.
`wr` 설정은 다음 변환 사이클부터 적용, `rr` 검증은 한 프레임 뒤에.

### 3.4 레지스터 맵 (사용분)

| ADDR | 이름 | 내용 |
|---|---|---|
| 0x00~0x02 | DOUTH/M/L | ADC 데이터 |
| 0x03 | DSMC | [7:4] 샘플레이트 / [3:0] PGA |
| 0x04 | CSEL | [7:5] 입력 선택 (Differential/Single/Internal Short) |
| 0x05 | CFGR | [6:5] MODE / [0] DRDY |
| 0x3E/0x3F | IDH/IDL | 칩 ID = 0x92/0x10 (통신 검증용) |

참고 시퀀스: `wr 0x03 0x02`=10SPS+PGA×64, `wr 0x04 0x60`=Internal Short,
`wr 0x04 0x00`=Differential.

## 4. 내장 DAC1 드라이버 + 캘리브레이션

- STM32H533 DAC1: ch1=PA4, ch2=PA5, 12bit 버퍼드. HAL DAC 모듈 미사용 —
  레지스터 직접 제어 (MCR/CR/DHR12Rx). `app/dac_internal.c/h`.
- 변환: `code = round(uV * 4095 / 3300000)`, 0~4095 클램프.
- 채널별 보정: `uV_corrected = (uV + offset_uV) * (1 + gain_ppm/1e6)` →
  0~VDDA 클램프. `dac cal`은 계수만 변경 — `dac set` 재실행 시 반영.
- 표준 자극 (유효성 검사용): ch1=1.52V(AINP) / ch2=1.48V(AINN) → 차동 +40mV.

### 캘 계수 영구 저장 (보드 내 플래시)

- 저장소: 내부 플래시 마지막 8KB 섹터 (0x0807E000, bank2 sector31). 링커에서
  코드 영역 504K로 축소해 예약 (`app/cal_store.c`, magic+ver+계수+CRC 32B).
- 채널별 valid 플래그 — 한 채널만 저장해도 반대 채널 보존(머지).
- 부팅 시 자동 로드 → PC 주입 없이 `dac set`만으로 보정 출력.
- ⚠ H5 ICACHE: 플래시 프로그램 직후 같은 영역을 읽으면 캐시가 옛 내용을
  반환 → 프로그램 후 `HAL_ICACHE_Invalidate()` 필수.
- 캘 적용 우선순위: 보드 플래시 saved 채널 → PC json 주입 생략. 미저장
  채널만 json/기본 계수 폴백 (`dac_ready()`, CLI/GUI/스윕 공용).
- 2점 캘 위저드(GUI): 저점/고점 출력·실측 → `gain_ppm=(1/A−1)·1e6`,
  `offset_uV=−B` → 중간점 검증 ±2mV PASS/FAIL → PASS 시 보드에 자동 저장.

## 5. 코드 구조 / 관찰 사항

```
app/board_pins.h     핀 매핑 (유일한 하드웨어 정의 지점)
app/delay.c/h        µs 딜레이 (DWT)
app/chip1.c/h       비트뱅잉 드라이버
app/dac_internal.c/h 내장 DAC1 + 캘리브레이션
app/cal_store.c/h    플래시 캘 저장소
app/meas.c/h         내장 ADC 노드 측정
app/cli.c/h          라인 파서 + 명령 디스패치
tools/               PC측 자동화 (아래)
```

- ⚠ 설정 직후 `rd`에서 수십 초의 단조 드리프트 과도 구간이 실측됨 (PGA/설정
  재적용 + 세틀링). 캡처 전 세틀링 대기 또는 초반 샘플 폐기 권장.

## 8. 온도 챔버 연동 + meas

### 챔버 프로토콜 (ESPEC SH/SU 계열, tools/chamber.py)

| | SH-662 | SU-661 |
|---|---|---|
| 통신 | RS485, 9600 | Prologix GPIB-USB, 115200 |
| 종단 | LF만 (CR 불가) | LF/CR 모두 가능 |
| 접두 | `1,` | 없음 |
| 습도 | 지원 | 미지원 |

- 응답: `OK:<echo>` / `NA:<사유>`. `TEMP?` → `현재,목표,상한,하한`.
  `MON?` → `온도[,습도],운전모드,알람수` (습도 필드 3형태 모두 수용).
  운전모드: OFF/STANDBY/CONSTANT/RUN.
- ⚠ 실측: 일부 개체는 정수형 온도만 수용 (`S25` OK, `S25.0` → `NA:PARA ERR`)
  — 소수 온도 사용 금지.
- 명령 간 최소 간격 0.3s, 응답 수신 후 다음 명령, 타임아웃 2s + 재시도 2회.

### meas 명령 (내장 ADC1)

- 2노드 측정: DVDD(PB0/INP9), VDDA/REFIN(PC1/INP11). 16샘플 평균.
- VREFINT 공장 캘 기반 VDDA 실측 보정 — 3.3V 가정 없음.
- ⚠ H5 함정: ICACHE 켠 상태에서 엔지니어링 바이트(VREFINT 캘값/UID 영역)를
  읽으면 HardFault → 부팅 후 1회 ICACHE off로 캐시한 뒤 수동 계산.
- `meas cal` 셀프캘: ADCAL 재실행(오프셋) + VREFINT 재기준(게인/VDDA).
  H5에는 VSSA 내부 채널이 없어 ADCAL이 0V 포인트를 대체.
- 응답: `VDD=3.299V VREF=3.001V` + `OK`.

### 온도 스윕 (tools/temp_sweep.py + GUI)

- 상태머신: SET_TEMP → WAIT_TARGET(폴링, 타임아웃) → SOAK → TEST(캡처+meas)
  → REPORT → 다음 온도. 중단 2모드(즉시/스텝 완료 후).
- 종료 동작: 25°C 복귀 후 정지(기본) / 즉시 정지 / 유지. 중단·에러 시 즉시
  POWER,OFF.
- 스윕마다 전용 엑셀 시트(`SW_MMDD_HHMM[_라벨]`) 생성 — 레이아웃 복제+데이터
  클리어, 블록=온도. VREF 2.9~3.1V 밖이면 경고(접촉 불량/미개조 보드 감지).
- 단위검증: tools/test_temp_sweep.py (Mock 챔버 + 가상시계, 하드웨어 불필요).
