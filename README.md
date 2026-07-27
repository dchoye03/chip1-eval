# CHIP1 ADC Evaluation Firmware & Test Automation

> **Note:** "CHIP1" is a deliberately fictional placeholder name. This project
> targets a real proprietary 24-bit ΔΣ ADC whose part name, datasheet, and
> schematics are confidential — everything identifying has been renamed or
> removed. The code, protocol timing, and architecture are the real work.

Firmware and PC-side automation for characterizing a 24-bit ΔΣ ADC (HX711-class
2-wire interface) on a single NUCLEO-H533RE board — replacing a lost-source
legacy two-board test setup.

The MCU does everything: bit-banged ADC interface, stimulus generation with the
internal DAC, self-measurement of supply/reference nodes, and on-board
persistence of calibration coefficients. The PC side adds a Tkinter GUI, Excel
reporting, a two-point DAC calibration wizard, and thermal-chamber sweep
automation.

## Highlights

- **Bit-banged 2-wire protocol driver** — the ADC is *not* SPI; a 27-pulse data
  frame extends to a 46-pulse register access frame, timed with the DWT cycle
  counter (`app/chip1.c`). Idle-clock discipline matters: >100 ms SCK-high
  puts the chip into power-down.
- **Internal DAC stimulus + calibration** (`app/dac_internal.c`) — 12-bit DAC1
  drives the ADC inputs differentially; per-channel offset/gain-ppm correction
  gets the output within ±1 mV of target.
- **Board-resident calibration** (`app/cal_store.c`) — coefficients live in the
  last flash sector (linker-reserved), with per-channel valid flags, merge-on
  partial save, CRC, and boot-time auto-load. Plug the board into any PC and
  `dac set` is already calibrated. Includes the obligatory STM32H5 ICACHE
  lessons (stale reads after flash program; HardFault on engineering-byte
  reads with cache enabled).
- **Self-measurement** (`app/meas.c`) — internal ADC reads the DUT supply and
  reference nodes, VDDA derived from the VREFINT factory calibration instead
  of assuming 3.3 V; `meas cal` re-runs ADCAL + VREFINT referencing.
- **Line-oriented CLI contract** (`app/cli.c`) — every command terminates with
  `OK` or `ERR <reason>`, plain ASCII, so the PC tools can parse it reliably;
  `rd` streams samples and aborts on ESC.
- **PC automation** (`tools/`) — CLI test runner and Tkinter GUI writing
  directly into an Excel report (openpyxl, formula-preserving block writer),
  two-point DAC calibration wizard with pass/fail verification, ESPEC
  thermal-chamber driver (RS485 / Prologix GPIB) and a temperature-sweep state
  machine with a mock chamber + fake clock for hardware-free unit tests.

## Layout

```
app/            firmware application (drivers, CLI, measurement, cal store)
Core/, Drivers/, cmake/   STM32CubeMX-generated code + HAL
tools/          PC-side automation (Python 3, pyserial + openpyxl)
SPEC.md         technical spec: wiring, protocol, CLI contract (Korean)
FLASHING.md     build / flash / tool command reference (Korean)
```

## Build & flash

STM32CubeCLT (CMake preset):

```powershell
cmake --preset Debug
cmake --build --preset Debug
STM32_Programmer_CLI -c port=SWD mode=UR -w build\Debug\CHIP1.elf -v -rst
```

## PC tools

```powershell
pip install -r requirements.txt
python tools\chip1_autotest.py     # CLI test runner
python tools\chip1_gui.pyw         # GUI (tests, cal wizards, temp sweep)
python tools\test_temp_sweep.py     # sweep state-machine unit tests (no HW)
```

Console: ST-Link VCP, 115200 8N1 — type `help` for the command list.

## Notes

- Board-specific details (a solder-bridge rework to expose PA4, a reference
  node tap on the sensor board) are documented in `SPEC.md` §1.
- `config/` and `results/` are created at runtime and are not tracked.
  On first run the tools copy `template/report_template.xlsx` (a blank,
  formula-only report layout) into `results/` and fill DUT blocks from there.
