#pragma once

#include "mipi_dsi_driver.h"

class Tab5DsiDriver : public MipiDsiDriver {
public:
    void init() override;
    bool isAvailable() const override;
    void setBacklightBrightness(uint8_t brightness) override;
    void configureLVGL(lv_display_t* display, uint8_t rotation) override;
    bool asyncFlush() const override;

protected:
    const mipi_dsi_init_cmd_t* getInitCommands() const override;
    size_t getInitCommandCount() const override;
    const char* getLogTag() const override;
    MipiDsiTimingConfig getTimingConfig() const override;
};