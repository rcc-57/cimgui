#include <string.h>
#include "e502_ctrl.h"

void E502_StandInitStruct(E502_StandDevice *dev) {
    memset(dev, 0, sizeof(E502_StandDevice));
    dev->lch_count = 1;
    /* Настройка по умолчанию для 0-го логического канала: вход 0, общая земля, диапазон +/-10В */
    dev->lch_cfg[0].phy_ch = 0;
    dev->lch_cfg[0].mode   = X502_LCH_MODE_COMM;
    dev->lch_cfg[0].range  = X502_ADC_RANGE_10;
    dev->lch_cfg[0].avg    = 0;
    dev->config_dirty      = true;
}

int E502_StandListUSB(char serials[][X502_SERIAL_SIZE], uint32_t max_count, uint32_t *found_count) {
    int32_t res = E502_UsbGetSerialList(serials, max_count, X502_GETDEVS_FLAGS_ONLY_NOT_OPENED, found_count);
    return (res < 0) ? res : X502_ERR_OK;
}

int E502_StandConnectUSB(E502_StandDevice *dev, const char *serial) {
    E502_StandInitStruct(dev);
    dev->hnd = X502_Create();
    if (dev->hnd == NULL) {
        dev->last_error = -1;
        return -1;
    }

    dev->last_error = E502_OpenUsb(dev->hnd, (serial && serial[0] != '\0') ? serial : NULL);
    if (dev->last_error != X502_ERR_OK) {
        /* Согласно документации, при ряде ошибок (например, X502_ERR_FPGA_NOT_LOADED)
           соединение может остаться открытым, поэтому закрываем его перед освобождением */
        X502_Close(dev->hnd);
        X502_Free(dev->hnd);
        dev->hnd = NULL;
        return dev->last_error;
    }

    dev->is_connected = true;
    X502_GetDevInfo(dev->hnd, &dev->dev_info);
    return E502_StandApplyConfig(dev);
}

int E502_StandConnectIP(E502_StandDevice *dev, uint32_t ip_addr, uint32_t tout_ms) {
    E502_StandInitStruct(dev);
    dev->hnd = X502_Create();
    if (dev->hnd == NULL) {
        dev->last_error = -1;
        return -1;
    }

    dev->last_error = E502_OpenByIpAddr(dev->hnd, ip_addr, 0, tout_ms);
    if (dev->last_error != X502_ERR_OK) {
        X502_Close(dev->hnd);
        X502_Free(dev->hnd);
        dev->hnd = NULL;
        return dev->last_error;
    }

    dev->is_connected = true;
    X502_GetDevInfo(dev->hnd, &dev->dev_info);
    return E502_StandApplyConfig(dev);
}

void E502_StandDisconnect(E502_StandDevice *dev) {
    if (dev->hnd != NULL) {
        if (dev->is_connected) {
            X502_Close(dev->hnd);
        }
        X502_Free(dev->hnd);
        dev->hnd = NULL;
    }
    dev->is_connected = false;
}

int E502_StandSetChannelCount(E502_StandDevice *dev, uint32_t count) {
    if (!dev->is_connected || count == 0 || count > STAND_MAX_CHANNELS) return -1;
    dev->last_error = X502_SetLChannelCount(dev->hnd, count);
    if (dev->last_error == X502_ERR_OK) {
        dev->lch_count = count;
        dev->config_dirty = true;
    }
    return dev->last_error;
}

int E502_StandConfigChannel(E502_StandDevice *dev, uint32_t lch, uint32_t phy_ch,
                            uint32_t mode, uint32_t range, uint32_t avg) {
    if (!dev->is_connected || lch >= STAND_MAX_CHANNELS) return -1;
    dev->last_error = X502_SetLChannel(dev->hnd, lch, phy_ch, mode, range, avg);
    if (dev->last_error == X502_ERR_OK) {
        dev->lch_cfg[lch].phy_ch = phy_ch;
        dev->lch_cfg[lch].mode   = mode;
        dev->lch_cfg[lch].range  = range;
        dev->lch_cfg[lch].avg    = avg;
        dev->config_dirty = true;
    }
    return dev->last_error;
}

int E502_StandSetPullups(E502_StandDevice *dev, uint32_t pullups_mask) {
    if (!dev->is_connected) return -1;
    dev->last_error = X502_SetDigInPullup(dev->hnd, pullups_mask);
    if (dev->last_error == X502_ERR_OK) {
        dev->pullups_mask = pullups_mask;
        dev->config_dirty = true;
    }
    return dev->last_error;
}

int E502_StandApplyConfig(E502_StandDevice *dev) {
    if (!dev->is_connected) return -1;

    /* Записываем количество каналов и конфигурацию каждого канала в описатель */
    dev->last_error = X502_SetLChannelCount(dev->hnd, dev->lch_count);
    if (dev->last_error != X502_ERR_OK) return dev->last_error;

    for (uint32_t i = 0; i < dev->lch_count; i++) {
        dev->last_error = X502_SetLChannel(dev->hnd, i,
                                           dev->lch_cfg[i].phy_ch,
                                           dev->lch_cfg[i].mode,
                                           dev->lch_cfg[i].range,
                                           dev->lch_cfg[i].avg);
        if (dev->last_error != X502_ERR_OK) return dev->last_error;
    }

    dev->last_error = X502_SetDigInPullup(dev->hnd, dev->pullups_mask);
    if (dev->last_error != X502_ERR_OK) return dev->last_error;

    /* Передаем все установленные настройки непосредственно в регистры модуля */
    dev->last_error = X502_Configure(dev->hnd, 0);
    if (dev->last_error == X502_ERR_OK) {
        dev->config_dirty = false;
    }
    return dev->last_error;
}

int E502_StandReadAdcFrame(E502_StandDevice *dev, double *volts_buf, uint32_t tout_ms) {
    if (!dev->is_connected) return -1;
    /* Автоматически применяем настройки, если они менялись до чтения */
    if (dev->config_dirty) {
        int err = E502_StandApplyConfig(dev);
        if (err != X502_ERR_OK) return err;
    }
    dev->last_error = X502_AsyncGetAdcFrame(dev->hnd, X502_PROC_FLAGS_VOLT, tout_ms, volts_buf);
    return dev->last_error;
}

int E502_StandWriteDac(E502_StandDevice *dev, uint32_t dac_ch, double volts) {
    if (!dev->is_connected) return -1;
    uint32_t flags = X502_DAC_FLAGS_VOLT | X502_DAC_FLAGS_CALIBR;
    dev->last_error = X502_AsyncOutDac(dev->hnd, dac_ch, volts, flags);
    if (dev->last_error == X502_ERR_OK) {
        if (dac_ch == X502_DAC_CH1) dev->dac_volts[0] = volts;
        if (dac_ch == X502_DAC_CH2) dev->dac_volts[1] = volts;
    }
    return dev->last_error;
}

int E502_StandBlinkLed(E502_StandDevice *dev) {
    if (!dev->is_connected) return -1;
    dev->last_error = X502_LedBlink(dev->hnd);
    return dev->last_error;
}

const char* E502_StandGetErrorStr(const E502_StandDevice *dev) {
    return X502_GetErrorString(dev->last_error);
}