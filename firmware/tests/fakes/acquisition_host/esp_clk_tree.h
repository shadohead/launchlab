#pragma once
#include "fake_runtime.h"
enum soc_module_clk_t {SOC_MOD_CLK_APB=1};
enum esp_clk_tree_src_freq_precision_t {ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,ESP_CLK_TREE_SRC_FREQ_PRECISION_EXACT};
static esp_err_t esp_clk_tree_src_get_freq_hz(soc_module_clk_t,esp_clk_tree_src_freq_precision_t,uint32_t *hz){*hz=fakeApbHz;return ESP_OK;}
