#pragma once
#include "fake_runtime.h"
#define ADC_LL_CLKM_DIV_B_DEFAULT 1
#define ADC_LL_CLKM_DIV_A_DEFAULT 0
#define HAL_FORCE_READ_U32_REG_FIELD(reg,field) ((reg).field)
static void adc_ll_digi_trigger_disable(){APB_SARADC.ctrl2.timer_en=0;++timerDisables;}
static void adc_ll_digi_trigger_enable(){APB_SARADC.ctrl2.timer_sel=1;APB_SARADC.ctrl2.timer_en=1;}
static void adc_ll_digi_controller_clk_div(uint32_t num,uint32_t b,uint32_t a){assert(!APB_SARADC.ctrl2.timer_en);APB_SARADC.apb_adc_clkm_conf.clkm_div_num=num;APB_SARADC.apb_adc_clkm_conf.clkm_div_b=b;APB_SARADC.apb_adc_clkm_conf.clkm_div_a=a;}
static void adc_ll_digi_set_trigger_interval(uint32_t cycles){assert(!APB_SARADC.ctrl2.timer_en);APB_SARADC.ctrl2.timer_target=cycles;}
