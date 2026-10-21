#ifndef E502_CTRL_H
#define E502_CTRL_H

#include <stdint.h>
#include <stdbool.h>
#include "x502api.h"
#include "e502api.h"

#define STAND_MAX_CHANNELS 32 /* Ограничение для удобства отображения на стенде (API поддерживает до 256) */

/* Структура настроек одного логического канала для контроля состояния */
typedef struct {
    uint32_t phy_ch;   /* Физический канал: 0..15 (дифф) или 0..31 (общая земля) */
    uint32_t mode;     /* Режим измерения (t_x502_lch_mode, например X502_LCH_MODE_COMM) */
    uint32_t range;    /* Диапазон измерения (t_x502_adc_range, например X502_ADC_RANGE_10) */
    uint32_t avg;      /* Коэффициент усреднения (0 - по умолчанию, 1..X502_LCH_AVG_SIZE_MAX) */
} E502_ChannelConfig;

/* Полный контекст устройства для стенда (используется и в консоли, и в будущем UI) */
typedef struct {
    t_x502_hnd hnd;                                  /* Описатель модуля L-Card */
    t_x502_info dev_info;                            /* Постоянная информация о модуле */
    bool is_connected;                               /* Статус соединения */
    bool config_dirty;                               /* Флаг: настройки изменены, но еще не переданы через X502_Configure */
    uint32_t lch_count;                              /* Количество активных логических каналов */
    E502_ChannelConfig lch_cfg[STAND_MAX_CHANNELS];  /* Текущая конфигурация логической таблицы */
    uint32_t pullups_mask;                           /* Маска подтягивающих резисторов */
    double dac_volts[2];                             /* Последние установленные напряжения на ЦАП1 и ЦАП2 */
    int32_t last_error;                              /* Код последней ошибки библиотеки */
} E502_StandDevice;

/* Инициализация структуры и управление связью */
void E502_StandInitStruct(E502_StandDevice *dev);
int  E502_StandListUSB(char serials[][X502_SERIAL_SIZE], uint32_t max_count, uint32_t *found_count);
int  E502_StandConnectUSB(E502_StandDevice *dev, const char *serial);
int  E502_StandConnectIP(E502_StandDevice *dev, uint32_t ip_addr, uint32_t tout_ms);
void E502_StandDisconnect(E502_StandDevice *dev);

/* Изменение параметров (с последующей записью в модуль) */
int  E502_StandSetChannelCount(E502_StandDevice *dev, uint32_t count);
int  E502_StandConfigChannel(E502_StandDevice *dev, uint32_t lch, uint32_t phy_ch,
                             uint32_t mode, uint32_t range, uint32_t avg);
int  E502_StandSetPullups(E502_StandDevice *dev, uint32_t pullups_mask);
int  E502_StandApplyConfig(E502_StandDevice *dev);

/* Оперативный контроль и управление вводом-выводом */
int  E502_StandReadAdcFrame(E502_StandDevice *dev, double *volts_buf, uint32_t tout_ms);
int  E502_StandWriteDac(E502_StandDevice *dev, uint32_t dac_ch, double volts);
int  E502_StandBlinkLed(E502_StandDevice *dev);
const char* E502_StandGetErrorStr(const E502_StandDevice *dev);

#endif /* E502_CTRL_H */