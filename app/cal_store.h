/**
  ******************************************************************************
  * @file    cal_store.h
  * @brief   보드 내 캘리브레이션 영구 저장소 — 내부 플래시 마지막 섹터.
  *
  *          목적: 캘 계수가 보드를 따라다니게 (어느 PC에 꽂아도 유효).
  *          위치: 0x0807E000 (bank2 sector 31, 8KB — 링커에서 코드 영역 제외).
  *          부팅 시 dac_internal이 자동 로드/적용, 'dac cal save'로 기록.
  ******************************************************************************
  */
#ifndef CAL_STORE_H
#define CAL_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CAL_STORE_OK          0
#define CAL_STORE_ERR_FLASH  (-1)
#define CAL_STORE_ERR_VERIFY (-2)

/* 저장 데이터 로드. 반환 = 채널 valid 마스크 (bit0=ch1, bit1=ch2, 0=없음).
   valid한 채널만 out 배열에 채워짐 (invalid 채널은 건드리지 않음).
   v1 블롭(플래그 이전)은 양 채널 valid로 해석 (하위 호환). */
uint8_t cal_store_load(int32_t off_uv[2], int32_t ppm[2]);

/* ch_mask(bit0=ch1, bit1=ch2)에 해당하는 채널만 갱신 저장 — 기존 저장분의
   다른 채널 계수/플래그는 보존(머지). 블로킹 수십 ms. */
int cal_store_save(const int32_t off_uv[2], const int32_t ppm[2],
                   uint8_t ch_mask);

/* 저장된 채널 마스크 (0=없음, 1=ch1만, 2=ch2만, 3=양채널). */
uint8_t cal_store_mask(void);

#ifdef __cplusplus
}
#endif

#endif /* CAL_STORE_H */
