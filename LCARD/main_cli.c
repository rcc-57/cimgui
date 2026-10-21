#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "e502_ctrl.h"

/* Вспомогательная расшифровка констант диапазона для вывода в консоль */
static const char* range_to_str(uint32_t range) {
    switch (range) {
        case X502_ADC_RANGE_10:  return "+/-10 V";
        case X502_ADC_RANGE_5:   return "+/-5 V";
        case X502_ADC_RANGE_2:   return "+/-2 V";
        case X502_ADC_RANGE_1:   return "+/-1 V";
        case X502_ADC_RANGE_05:  return "+/-0.5 V";
        case X502_ADC_RANGE_02:  return "+/-0.2 V";
        default:                 return "UNKNOWN";
    }
}

/* Вспомогательная расшифровка режима канала */
static const char* mode_to_str(uint32_t mode) {
    switch (mode) {
        case X502_LCH_MODE_COMM: return "COMM (Общая земля)";
        case X502_LCH_MODE_DIFF: return "DIFF (Дифференц.)";
        case X502_LCH_MODE_ZERO: return "ZERO (Изм. нуля)";
        default:                 return "UNKNOWN";
    }
}

static void print_help(void) {
    printf("\n--- Команды управления стендом L-Card E-502 ---\n"
           "  scan                          - Найти доступные модули E-502 по USB\n"
           "  open [serial]                 - Открыть связь по USB (без аргумента - первый доступный)\n"
           "  info                          - Вывести аппаратную информацию о модуле и статус настроек\n"
           "  ch_cnt <N>                    - Задать число логических каналов АЦП (1..%d)\n"
           "  ch_set <lch> <phy> <m> <r> <a>- Настроить логический канал <lch>:\n"
           "                                  <phy>: физ. канал (0..31 для COMM, 0..15 для DIFF)\n"
           "                                  <m>  : режим (0=COMM, 1=DIFF, 2=ZERO)\n"
           "                                  <r>  : диапазон (0=10V, 1=5V, 2=2V, 3=1V, 4=0.5V, 5=0.2V)\n"
           "                                  <a>  : усреднение (0=авто, 1..128)\n"
           "  pullup <hex_mask>             - Задать маску подтяжек входов (например, 0x04 для SYN1)\n"
           "  apply                         - Записать текущие настройки в модуль (X502_Configure)\n"
           "  adc [count]                   - Считать и вывести 1 или [count] кадров АЦП (в Вольтах)\n"
           "  dac <1|2> <volts>             - Выдать напряжение <volts> В на канал ЦАП 1 или 2\n"
           "  blink                         - Моргнуть светодиодом LED1 на корпусе прибора\n"
           "  close                         - Закрыть соединение с модулем\n"
           "  help                          - Показать эту справку\n"
           "  exit                          - Выйти из программы\n\n", STAND_MAX_CHANNELS);
}

static void print_status(const E502_StandDevice *dev) {
    if (!dev->is_connected) {
        printf("[Статус] Модуль НЕ подключен.\n");
        return;
    }
    printf("\n=== Паспорт и состояние модуля %s (S/N: %s) ===\n",
           dev->dev_info.name, dev->dev_info.serial);
    printf("  Версия ПЛИС       : %u.%u\n",
           (dev->dev_info.fpga_ver >> 8) & 0xFF, dev->dev_info.fpga_ver & 0xFF);
    printf("  Ревизия платы     : %u (PLDA: %u)\n",
           dev->dev_info.board_rev, dev->dev_info.plda_ver);
    printf("  Прошивка Cortex-M4: 0x%08X\n", dev->dev_info.mcu_firmware_ver);
    printf("  Flash-память      : %u байт\n", dev->dev_info.flash_size);
    printf("  Процессор BlackFin: %s\n",
           (dev->dev_info.devflags & X502_DEVFLAGS_BF_PRESENT) ? "Присутствует" : "Отсутствует");
    printf("  Синхронизация     : %s\n",
           dev->config_dirty ? "ТРЕБУЕТСЯ APPLY (настройки изменены)" : "ОК (записано в ПЛИС)");
    printf("  Подтяжки (pullups): 0x%02X\n", dev->pullups_mask);
    printf("  Выходы ЦАП        : DAC1 = %.3f V | DAC2 = %.3f V\n",
           dev->dac_volts[0], dev->dac_volts[1]);

    printf("\n--- Таблица логических каналов АЦП (активно: %u) ---\n", dev->lch_count);
    for (uint32_t i = 0; i < dev->lch_count; i++) {
        printf("  LCH[%2u] -> Физ.канал: %2u | Режим: %-20s | Диапазон: %-8s | Усредн.: %u\n",
               i, dev->lch_cfg[i].phy_ch,
               mode_to_str(dev->lch_cfg[i].mode),
               range_to_str(dev->lch_cfg[i].range),
               dev->lch_cfg[i].avg);
    }
    printf("==================================================\n");
}

int main(void) {
    E502_StandDevice stand;
    E502_StandInitStruct(&stand);

    char line[256];
    printf("Консоль лабораторного стенда L-Card E-502 запущена. Введите 'help' для списка команд.\n");

    while (1) {
        printf("E502%s> ", stand.is_connected ? (stand.config_dirty ? "* (не прим.)" : " (онлайн)") : " (оффлайн)");
        if (fgets(line, sizeof(line), stdin) == NULL) break;

        char cmd[32] = {0};
        if (sscanf(line, "%31s", cmd) != 1) continue;

        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            break;
        } else if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "scan") == 0) {
            char serials[8][X502_SERIAL_SIZE];
            uint32_t fnd = 0;
            if (E502_StandListUSB(serials, 8, &fnd) == X502_ERR_OK) {
                printf("Найдено свободных USB-модулей E-502: %u\n", fnd);
                for (uint32_t i = 0; i < fnd && i < 8; i++) {
                    printf("  [%u] S/N: %s\n", i, serials[i]);
                }
            } else {
                printf("Ошибка поиска USB.\n");
            }
        } else if (strcmp(cmd, "open") == 0) {
            char serial[X502_SERIAL_SIZE] = {0};
            sscanf(line, "%*s %31s", serial);
            if (stand.is_connected) E502_StandDisconnect(&stand);
            if (E502_StandConnectUSB(&stand, serial[0] ? serial : NULL) == X502_ERR_OK) {
                printf("Успешно подключено к %s (S/N: %s)\n", stand.dev_info.name, stand.dev_info.serial);
            } else {
                printf("Ошибка открытия: %s\n", E502_StandGetErrorStr(&stand));
            }
        } else if (strcmp(cmd, "info") == 0 || strcmp(cmd, "status") == 0) {
            print_status(&stand);
        } else if (strcmp(cmd, "ch_cnt") == 0) {
            uint32_t cnt = 0;
            if (sscanf(line, "%*s %u", &cnt) == 1) {
                if (E502_StandSetChannelCount(&stand, cnt) == X502_ERR_OK) {
                    printf("Количество логических каналов установлено в %u (выполните 'apply' или 'adc').\n", cnt);
                } else {
                    printf("Ошибка установки количества каналов: %s\n", E502_StandGetErrorStr(&stand));
                }
            }
        } else if (strcmp(cmd, "ch_set") == 0) {
            uint32_t lch, phy, m_idx, r_idx, avg;
            if (sscanf(line, "%*s %u %u %u %u %u", &lch, &phy, &m_idx, &r_idx, &avg) == 5) {
                uint32_t modes[3]  = {X502_LCH_MODE_COMM, X502_LCH_MODE_DIFF, X502_LCH_MODE_ZERO};
                uint32_t ranges[6] = {X502_ADC_RANGE_10, X502_ADC_RANGE_5, X502_ADC_RANGE_2,
                                      X502_ADC_RANGE_1, X502_ADC_RANGE_05, X502_ADC_RANGE_02};
                if (m_idx < 3 && r_idx < 6) {
                    if (E502_StandConfigChannel(&stand, lch, phy, modes[m_idx], ranges[r_idx], avg) == X502_ERR_OK) {
                        printf("Канал LCH[%u] настроен. Выполните 'apply' для записи в модуль.\n", lch);
                    } else {
                        printf("Ошибка настройки канала: %s\n", E502_StandGetErrorStr(&stand));
                    }
                } else {
                    printf("Неверный индекс режима (0..2) или диапазона (0..5).\n");
                }
            } else {
                printf("Формат: ch_set <lch> <phy> <mode 0..2> <range 0..5> <avg>\n");
            }
        } else if (strcmp(cmd, "pullup") == 0) {
            uint32_t mask = 0;
            if (sscanf(line, "%*s %x", &mask) == 1) {
                if (E502_StandSetPullups(&stand, mask) == X502_ERR_OK) {
                    printf("Маска подтяжек 0x%02X задана.\n", mask);
                } else {
                    printf("Ошибка: %s\n", E502_StandGetErrorStr(&stand));
                }
            }
        } else if (strcmp(cmd, "apply") == 0) {
            if (E502_StandApplyConfig(&stand) == X502_ERR_OK) {
                printf("Настройки успешно записаны в регистры модуля E-502!\n");
            } else {
                printf("Ошибка конфигурации: %s\n", E502_StandGetErrorStr(&stand));
            }
        } else if (strcmp(cmd, "adc") == 0) {
            uint32_t frames = 1;
            sscanf(line, "%*s %u", &frames);
            double volts[STAND_MAX_CHANNELS];
            for (uint32_t f = 0; f < frames; f++) {
                if (E502_StandReadAdcFrame(&stand, volts, 1000) == X502_ERR_OK) {
                    printf("Кадр #%u: ", f + 1);
                    for (uint32_t c = 0; c < stand.lch_count; c++) {
                        printf("[LCH%u: %+8.4f V] ", c, volts[c]);
                    }
                    printf("\n");
                } else {
                    printf("Ошибка чтения АЦП: %s\n", E502_StandGetErrorStr(&stand));
                    break;
                }
            }
        } else if (strcmp(cmd, "dac") == 0) {
            uint32_t ch_num = 0;
            double v = 0.0;
            if (sscanf(line, "%*s %u %lf", &ch_num, &v) == 2 && (ch_num == 1 || ch_num == 2)) {
                uint32_t dac_const = (ch_num == 1) ? X502_DAC_CH1 : X502_DAC_CH2;
                if (E502_StandWriteDac(&stand, dac_const, v) == X502_ERR_OK) {
                    printf("На ЦАП%u подано напряжение %.3f В.\n", ch_num, v);
                } else {
                    printf("Ошибка записи в ЦАП: %s\n", E502_StandGetErrorStr(&stand));
                }
            } else {
                printf("Формат: dac <1|2> <volts>\n");
            }
        } else if (strcmp(cmd, "blink") == 0) {
            if (E502_StandBlinkLed(&stand) == X502_ERR_OK) {
                printf("Команда моргания светодиодом LED1 отправлена.\n");
            } else {
                printf("Ошибка: %s\n", E502_StandGetErrorStr(&stand));
            }
        } else if (strcmp(cmd, "close") == 0) {
            E502_StandDisconnect(&stand);
            printf("Соединение закрыто.\n");
        } else {
            printf("Неизвестная команда '%s'. Введите 'help'.\n", cmd);
        }
    }

    if (stand.is_connected) {
        E502_StandDisconnect(&stand);
    }
    return 0;
}