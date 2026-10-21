#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "e502_ctrl.h"

/* Helper to convert ADC range constants to readable string */
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

/* Helper to convert input connection mode to readable string */
static const char* mode_to_str(uint32_t mode) {
    switch (mode) {
        case X502_LCH_MODE_COMM: return "COMM (Common Ground)";
        case X502_LCH_MODE_DIFF: return "DIFF (Differential)";
        case X502_LCH_MODE_ZERO: return "ZERO (Zero Calibration)";
        default:                 return "UNKNOWN";
    }
}

/* Print command line reference */
static void print_help(void) {
    printf("\n==================== L-Card E-502 CLI Commands ====================\n"
           "  scan                           - Scan USB bus for available modules\n"
           "  open [serial]                  - Connect via USB (first found if omitted)\n"
           "  info                           - Show hardware info, board rev, and status\n"
           "  ch_cnt <N>                     - Set number of active logical channels (1..%d)\n"
           "  ch_set <lch> <phy> <m> <r> <a> - Configure logical channel <lch>:\n"
           "                                   <phy>: physical ch (0..31 COMM, 0..15 DIFF)\n"
           "                                   <m>  : mode (0=COMM, 1=DIFF, 2=ZERO)\n"
           "                                   <r>  : range (0=10V, 1=5V, 2=2V, 3=1V, 4=0.5V, 5=0.2V)\n"
           "                                   <a>  : averaging factor (0=off/auto, 1..128)\n"
           "  pullup <hex_mask>              - Set pull-up/pull-down bitmask (e.g. 0x04 for SYN1)\n"
           "  apply                          - Commit and write configuration to FPGA registers\n"
           "  adc [count]                    - Read and display 1 or [count] ADC frames (in Volts)\n"
           "  dac <1|2> <volts>              - Output voltage <volts> to DAC channel 1 or 2\n"
           "  blink                          - Blink LED1 on front panel for visual identification\n"
           "  close                          - Disconnect and release device handle\n"
           "  help                           - Show this help message\n"
           "  exit                           - Disconnect and exit CLI\n"
           "===================================================================\n\n", STAND_MAX_CHANNELS);
}

/* Print current hardware info and channel mapping */
static void print_status(const E502_StandDevice *dev) {
    if (!dev->is_connected) {
        printf("[STATUS] Device is NOT connected.\n");
        return;
    }

    printf("\n=== Device Information (S/N: %s) ===\n", dev->dev_info.serial);
    printf("  Model Name        : %s\n", dev->dev_info.name);
    printf("  FPGA Version      : %u.%u\n",
           (dev->dev_info.fpga_ver >> 8) & 0xFF, dev->dev_info.fpga_ver & 0xFF);
    printf("  Board Revision    : %u (PLDA: %u)\n",
           dev->dev_info.board_rev, dev->dev_info.plda_ver);
    printf("  Cortex-M4 Firmware: %u.%u.%u.%u\n",
           (dev->dev_info.mcu_firmware_ver >> 24) & 0xFF,
           (dev->dev_info.mcu_firmware_ver >> 16) & 0xFF,
           (dev->dev_info.mcu_firmware_ver >> 8) & 0xFF,
           dev->dev_info.mcu_firmware_ver & 0xFF);
    printf("  BlackFin DSP      : %s\n",
           (dev->dev_info.devflags & X502_DEVFLAGS_BF_PRESENT) ? "PRESENT" : "ABSENT");
    printf("  FPGA Config Sync  : %s\n",
           dev->config_dirty ? "DIRTY (needs 'apply')" : "SYNCHRONIZED");
    printf("  Digital Pull-ups  : 0x%02X\n", dev->pullups_mask);
    printf("  DAC Outputs       : DAC1 = %.3f V | DAC2 = %.3f V\n",
           dev->dac_volts[0], dev->dac_volts[1]);

    printf("\n--- Active Logical Channels (%u channel(s)) ---\n", dev->lch_count);
    for (uint32_t i = 0; i < dev->lch_count; i++) {
        printf("  LCH[%2u] -> PhyInput: %2u | Mode: %-22s | Range: %-8s | Avg: %u\n",
               i,
               dev->lch_cfg[i].phy_ch,
               mode_to_str(dev->lch_cfg[i].mode),
               range_to_str(dev->lch_cfg[i].range),
               dev->lch_cfg[i].avg);
    }
    printf("===================================================\n\n");
}

int main(void) {
    E502_StandDevice stand;
    E502_StandInitStruct(&stand);

    char line[256];
    printf("L-Card E-502 Console Interface initialized.\nType 'help' to view available commands.\n\n");

    while (1) {
        /* Prompt reflects current online/offline and dirty configuration state */
        printf("E502%s> ", stand.is_connected ? (stand.config_dirty ? "* (unapplied)" : " (online)") : " (offline)");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            break;
        }

        char cmd[32] = {0};
        if (sscanf(line, "%31s", cmd) != 1) {
            continue;
        }

        if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
            break;
        } else if (strcmp(cmd, "help") == 0) {
            print_help();
        } else if (strcmp(cmd, "scan") == 0) {
            char serials[8][X502_SERIAL_SIZE];
            uint32_t found = 0;
            if (E502_StandListUSB(serials, 8, &found) == X502_ERR_OK) {
                printf("Found %u free USB device(s):\n", found);
                for (uint32_t i = 0; i < found && i < 8; i++) {
                    printf("  [%u] S/N: %s\n", i, serials[i]);
                }
            } else {
                printf("[ERROR] Failed to query USB devices.\n");
            }
        } else if (strcmp(cmd, "open") == 0) {
            char serial[X502_SERIAL_SIZE] = {0};
            sscanf(line, "%*s %31s", serial);
            if (stand.is_connected) {
                E502_StandDisconnect(&stand);
            }
            if (E502_StandConnectUSB(&stand, serial[0] ? serial : NULL) == X502_ERR_OK) {
                printf("[SUCCESS] Connected to %s (S/N: %s)\n",
                       stand.dev_info.name, stand.dev_info.serial);
            } else {
                printf("[ERROR] Connection failed: %s (code %d)\n",
                       E502_StandGetErrorStr(&stand), stand.last_error);
            }
        } else if (strcmp(cmd, "info") == 0 || strcmp(cmd, "status") == 0) {
            print_status(&stand);
        } else if (strcmp(cmd, "ch_cnt") == 0) {
            uint32_t cnt = 0;
            if (sscanf(line, "%*s %u", &cnt) == 1) {
                if (E502_StandSetChannelCount(&stand, cnt) == X502_ERR_OK) {
                    printf("[OK] Logical channel count set to %u. Run 'apply' to write to FPGA.\n", cnt);
                } else {
                    printf("[ERROR] Failed to set channel count: %s\n", E502_StandGetErrorStr(&stand));
                }
            } else {
                printf("Usage: ch_cnt <count>\n");
            }
        } else if (strcmp(cmd, "ch_set") == 0) {
            uint32_t lch, phy, m_idx, r_idx, avg;
            if (sscanf(line, "%*s %u %u %u %u %u", &lch, &phy, &m_idx, &r_idx, &avg) == 5) {
                uint32_t modes[3]  = {X502_LCH_MODE_COMM, X502_LCH_MODE_DIFF, X502_LCH_MODE_ZERO};
                uint32_t ranges[6] = {X502_ADC_RANGE_10, X502_ADC_RANGE_5, X502_ADC_RANGE_2,
                                      X502_ADC_RANGE_1, X502_ADC_RANGE_05, X502_ADC_RANGE_02};
                if (m_idx < 3 && r_idx < 6) {
                    if (E502_StandConfigChannel(&stand, lch, phy, modes[m_idx], ranges[r_idx], avg) == X502_ERR_OK) {
                        printf("[OK] Channel LCH[%u] configured. Run 'apply' to write to FPGA.\n", lch);
                    } else {
                        printf("[ERROR] Channel config failed: %s\n", E502_StandGetErrorStr(&stand));
                    }
                } else {
                    printf("[ERROR] Invalid mode index (0..2) or range index (0..5).\n");
                }
            } else {
                printf("Usage: ch_set <lch> <phy> <mode 0..2> <range 0..5> <avg>\n");
            }
        } else if (strcmp(cmd, "pullup") == 0) {
            uint32_t mask = 0;
            if (sscanf(line, "%*s %x", &mask) == 1) {
                if (E502_StandSetPullups(&stand, mask) == X502_ERR_OK) {
                    printf("[OK] Pull-up mask set to 0x%02X. Run 'apply' to write to FPGA.\n", mask);
                } else {
                    printf("[ERROR] Pull-up setting failed: %s\n", E502_StandGetErrorStr(&stand));
                }
            } else {
                printf("Usage: pullup <hex_mask>\n");
            }
        } else if (strcmp(cmd, "apply") == 0) {
            if (E502_StandApplyConfig(&stand) == X502_ERR_OK) {
                printf("[SUCCESS] Configuration written to FPGA successfully.\n");
            } else {
                printf("[ERROR] Configuration failed: %s\n", E502_StandGetErrorStr(&stand));
            }
        } else if (strcmp(cmd, "adc") == 0) {
            uint32_t frames = 1;
            sscanf(line, "%*s %u", &frames);
            double volts[STAND_MAX_CHANNELS];
            for (uint32_t f = 0; f < frames; f++) {
                if (E502_StandReadAdcFrame(&stand, volts, 1000) == X502_ERR_OK) {
                    printf("Frame #%03u: ", f + 1);
                    for (uint32_t c = 0; c < stand.lch_count; c++) {
                        printf("[CH%u: %+8.4f V] ", c, volts[c]);
                    }
                    printf("\n");
                } else {
                    printf("[ERROR] ADC read failed: %s\n", E502_StandGetErrorStr(&stand));
                    break;
                }
            }
        } else if (strcmp(cmd, "dac") == 0) {
            uint32_t ch_num = 0;
            double v = 0.0;
            if (sscanf(line, "%*s %u %lf", &ch_num, &v) == 2 && (ch_num == 1 || ch_num == 2)) {
                uint32_t dac_const = (ch_num == 1) ? X502_DAC_CH1 : X502_DAC_CH2;
                if (E502_StandWriteDac(&stand, dac_const, v) == X502_ERR_OK) {
                    printf("[OK] DAC%u output set to %.3f V\n", ch_num, v);
                } else {
                    printf("[ERROR] DAC write failed: %s\n", E502_StandGetErrorStr(&stand));
                }
            } else {
                printf("Usage: dac <1|2> <volts>\n");
            }
        } else if (strcmp(cmd, "blink") == 0) {
            if (E502_StandBlinkLed(&stand) == X502_ERR_OK) {
                printf("[OK] LED1 blink command sent.\n");
            } else {
                printf("[ERROR] Blink failed: %s\n", E502_StandGetErrorStr(&stand));
            }
        } else if (strcmp(cmd, "close") == 0) {
            E502_StandDisconnect(&stand);
            printf("[OK] Connection closed.\n");
        } else {
            printf("[UNKNOWN COMMAND] '%s'. Type 'help' for command list.\n", cmd);
        }
    }

    if (stand.is_connected) {
        E502_StandDisconnect(&stand);
    }
    printf("CLI terminated.\n");
    return 0;
}