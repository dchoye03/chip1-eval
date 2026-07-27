/**
  ******************************************************************************
  * @file    cal_store.c
  * @brief   플래시 마지막 섹터(0x0807E000)의 캘 저장소.
  *
  *          레이아웃 (32B = 쿼드워드 2개, H5 프로그래밍 단위 128bit):
  *            magic(4) ver(2) rsvd(2) off_uv[2](8) ppm[2](8) pad(4) crc(4)
  *          안전성: 코드와 다른 뱅크(bank2 끝)라 실행 정지 없음, 쓰기 실패
  *          최악의 경우 = "저장 안 됨" (벽돌 위험 없음). 쓰기 빈도는 캘
  *          이벤트뿐이라 수명(1만 회급) 무관.
  ******************************************************************************
  */
#include "cal_store.h"

#include <string.h>
#include "stm32h5xx_hal.h"

#define STORE_ADDR   0x0807E000UL          /* bank2 sector 31 */
#define STORE_SECTOR 31U
#define STORE_MAGIC  0x41435747UL          /* 'GWCA' */
#define STORE_VER    2U                    /* v2: ch_valid 채널별 플래그 추가 */

typedef struct
{
  uint32_t magic;
  uint16_t version;
  uint16_t ch_valid;                       /* bit0=ch1, bit1=ch2 (v1에선 rsvd=0) */
  int32_t  off_uv[2];
  int32_t  ppm[2];
  uint32_t pad;
  uint32_t crc;                            /* 앞 28바이트(워드 7개)의 XOR */
} cal_blob_t;                              /* 32 bytes */

static uint32_t blob_crc(const cal_blob_t *b)
{
  const uint32_t *w = (const uint32_t *)b;
  uint32_t crc = 0x5A5A5A5AUL;
  for (int i = 0; i < 7; i++)              /* crc 필드 제외 */
  {
    crc ^= w[i];
  }
  return crc;
}

static const cal_blob_t *valid_blob(void)
{
  const cal_blob_t *b = (const cal_blob_t *)STORE_ADDR;
  if (b->magic != STORE_MAGIC || b->crc != blob_crc(b))
  {
    return NULL;
  }
  if (b->version != 1U && b->version != STORE_VER)
  {
    return NULL;
  }
  return b;
}

static uint8_t blob_mask(const cal_blob_t *b)
{
  if (b == NULL)
  {
    return 0U;
  }
  /* v1(플래그 이전)은 항상 양 채널 저장이었음 -> 0x3으로 해석 (하위 호환) */
  return (b->version == 1U) ? 0x3U : (uint8_t)(b->ch_valid & 0x3U);
}

uint8_t cal_store_mask(void)
{
  return blob_mask(valid_blob());
}

uint8_t cal_store_load(int32_t off_uv[2], int32_t ppm[2])
{
  const cal_blob_t *b = valid_blob();
  uint8_t mask = blob_mask(b);

  if (mask & 0x1U)
  {
    off_uv[0] = b->off_uv[0];  ppm[0] = b->ppm[0];
  }
  if (mask & 0x2U)
  {
    off_uv[1] = b->off_uv[1];  ppm[1] = b->ppm[1];
  }
  return mask;
}

uint32_t cal_store_last_err = 0;   /* 진단용: 마지막 실패의 HAL_FLASH_GetError() */

int cal_store_save(const int32_t off_uv[2], const int32_t ppm[2],
                   uint8_t ch_mask)
{
  cal_blob_t blob;
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t bad_sector = 0;
  const cal_blob_t *old = valid_blob();

  /* 머지: 기존 저장분에서 시작해 요청된 채널만 갱신 (반대 채널 보존) */
  memset(&blob, 0, sizeof(blob));
  blob.magic    = STORE_MAGIC;
  blob.version  = STORE_VER;
  blob.ch_valid = blob_mask(old);
  if (old != NULL)
  {
    blob.off_uv[0] = old->off_uv[0];  blob.off_uv[1] = old->off_uv[1];
    blob.ppm[0]    = old->ppm[0];     blob.ppm[1]    = old->ppm[1];
  }
  if (ch_mask & 0x1U)
  {
    blob.off_uv[0] = off_uv[0];  blob.ppm[0] = ppm[0];
  }
  if (ch_mask & 0x2U)
  {
    blob.off_uv[1] = off_uv[1];  blob.ppm[1] = ppm[1];
  }
  blob.ch_valid |= (uint16_t)(ch_mask & 0x3U);
  blob.crc = blob_crc(&blob);

  if (HAL_FLASH_Unlock() != HAL_OK)
  {
    return CAL_STORE_ERR_FLASH;
  }

  erase.TypeErase = FLASH_TYPEERASE_SECTORS;
  erase.Banks     = FLASH_BANK_2;
  erase.Sector    = STORE_SECTOR;
  erase.NbSectors = 1;
  if (HAL_FLASHEx_Erase(&erase, &bad_sector) != HAL_OK)
  {
    cal_store_last_err = HAL_FLASH_GetError();
    HAL_FLASH_Lock();
    return CAL_STORE_ERR_FLASH;
  }

  for (uint32_t i = 0; i < sizeof(blob); i += 16U)   /* 쿼드워드 단위 */
  {
    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD, STORE_ADDR + i,
                          (uint32_t)((const uint8_t *)&blob + i)) != HAL_OK)
    {
      cal_store_last_err = HAL_FLASH_GetError();
      HAL_FLASH_Lock();
      return CAL_STORE_ERR_FLASH;
    }
  }
  HAL_FLASH_Lock();

  /* ⚠ 방금 쓴 영역을 ICACHE가 옛 내용(소거 전/0xFF)으로 물고 있어 아래 memcmp가
     거짓 실패했음 (2026-07-24 실측: HAL 에러 0인데 verify 실패). 무효화 필수. */
  HAL_ICACHE_Invalidate();

  /* 검증: 읽어서 비교 */
  if (memcmp((const void *)STORE_ADDR, &blob, sizeof(blob)) != 0)
  {
    return CAL_STORE_ERR_VERIFY;
  }
  return CAL_STORE_OK;
}
