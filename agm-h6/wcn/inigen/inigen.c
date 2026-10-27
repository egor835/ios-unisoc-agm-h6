#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#define SIPC_SUPPORT 1
#define GFP_KERNEL 0
#define kzalloc(n,f) calloc(1,n)
#define kfree free
#define wl_info(...) do{}while(0)
#define pr_info(...) do{}while(0)
static int kstrtol(const char *s, unsigned base, long *res){ char *e; *res=strtol(s,&e,base); return (*s && !*e)?0:-1; }
#ifndef __MARLIN3_RF_H__
#define __MARLIN3_RF_H__


struct nvm_cali_cmd {
	int8_t itm[64];
	int32_t par[512];
	int32_t num;
};

struct nvm_name_table {
	int8_t *itm;
	uint32_t mem_offset;
	int32_t type;
};
/*[Section 1: Version] */
struct version_t {
	uint16_t major;
	uint16_t minor;
};

/*[Section 2: Board Config]*/
struct board_config_t {
#ifndef SIPC_SUPPORT
	uint16_t calib_bypass;
	uint8_t txchain_mask;
	uint8_t rxchain_mask;
#else
	uint16_t calibration_bypass;
	uint8_t g2_chain_mask;
	uint8_t g5_chain_mask;
#endif
};

/*[Section 3: Board Config TPC]*/
struct board_config_tpc_t {
	uint8_t dpd_lut_idx[8];
	uint16_t tpc_goal_chain0[8];
	uint16_t tpc_goal_chain1[8];
};

struct tpc_element_lut_t {
	uint8_t rf_gain_idx;
	uint8_t pa_bias_idx;
	int8_t  dvga_offset;
	int8_t  residual_error;
};
/*[Section 4: TPC-LUT]*/
struct tpc_lut_t {
#ifndef SIPC_SUPPORT
	struct tpc_element_lut_t chain0_lut[8];
	struct tpc_element_lut_t chain1_lut[8];
#else
	struct tpc_element_lut_t g2_lut[8];
	struct tpc_element_lut_t g5_lut[8];
#endif
};

/*[Section 5: Board Config Frequency Compensation]*/
struct board_conf_freq_comp_t {
	int8_t channel_2g_chain0[14];
	int8_t channel_2g_chain1[14];
	int8_t channel_5g_chain0[25];
	int8_t channel_5g_chain1[25];
	int8_t reserved[2];
};

/*[Section 6: Rate To Power with BW 20M]*/
struct power_20m_t {
#ifndef SIPC_SUPPORT
	int8_t power_11b[4];
	int8_t power_11ag[8];
	int8_t power_11n[17];
	int8_t power_11ac[20];
	int8_t reserved[3];
#else
	int8_t power_11b[4];
	int8_t power_11g[8];
	int8_t power_11a[8];
	int8_t power_2g_11n[17];
	int8_t power_5g_11n[17];
	int8_t power_11ac[20];
	int8_t reserved[3];
#endif
};

/*[Section 7: Power Backoff]*/
struct power_backoff_t {
	int8_t green_wifi_offset;
#ifndef SIPC_SUPPORT
	int8_t ht40_power_offset;
#else
	int8_t ht40_2g_power_offset;
	int8_t ht40_5g_power_offset;
#endif
	int8_t vht40_power_offset;
	int8_t vht80_power_offset;
	int8_t sar_power_offset;
	int8_t mean_power_offset;
#ifdef SIPC_SUPPORT
	int8_t apc_mode;
	int8_t magic_word;
#endif
	int8_t reserved[2];
};

/*[Section 8: Reg Domain]*/
struct reg_domain_t {
	uint32_t reg_domain1;
	uint32_t reg_domain2;
};

/*[Section 9: Band Edge Power offset (MKK, FCC, ETSI)]*/
struct band_edge_power_offset_t {
	uint8_t bw20m[39];
	uint8_t bw40m[21];
	uint8_t bw80m[6];
	uint8_t reserved[2];
};

/*[Section 10: TX Scale]*/
struct tx_scale_t {
	int8_t chain0[39][16];
	int8_t chain1[39][16];
};

/*[Section 11: misc]*/
struct misc_t {
	int8_t dfs_switch;
	int8_t power_save_switch;
#ifndef SIPC_SUPPORT
	int8_t fem_lan_param_setup;
#endif
	int8_t rssi_report_diff;
#ifdef SIPC_SUPPORT
	int8_t ex_fem_en;
	int8_t tx_en_ctrl;
	int8_t lna_en_ctrl;
	int8_t sw_en_ctrl;
	int8_t lna_gain;
	int8_t lna_bypass_gain;
	int8_t ex_fem_pdet;
#endif
};

/*[Section 12: debug reg]*/
struct debug_reg_t {
	uint32_t address[16];
	uint32_t value[16];
};

/*[Section 13:coex_config] */
struct coex_config_t {
	uint32_t bt_performance_cfg0;
	uint32_t bt_performance_cfg1;
	uint32_t wifi_performance_cfg0;
	uint32_t wifi_performance_cfg2;
	uint32_t strategy_cfg0;
	uint32_t strategy_cfg1;
	uint32_t strategy_cfg2;
	uint32_t compatibility_cfg0;
	uint32_t compatibility_cfg1;
	uint32_t ant_cfg0;
	uint32_t ant_cfg1;
	uint32_t isolation_cfg0;
	uint32_t isolation_cfg1;
	uint32_t reserved_cfg0;
	uint32_t reserved_cfg1;
	uint32_t reserved_cfg2;
	uint32_t reserved_cfg3;
	uint32_t reserved_cfg4;
	uint32_t reserved_cfg5;
	uint32_t reserved_cfg6;
	uint32_t reserved_cfg7;
};

struct rf_config_t {
	int rf_data_len;
	uint8_t rf_data[1500];
};

/*wifi config section1 struct*/
struct wifi_conf_sec1_t {
	struct version_t version;
	struct board_config_t board_config;
	struct board_config_tpc_t board_config_tpc;
	struct tpc_lut_t tpc_lut;
	struct board_conf_freq_comp_t board_conf_freq_comp;
	struct power_20m_t power_20m;
	struct power_backoff_t power_backoff;
	struct reg_domain_t reg_domain;
	struct band_edge_power_offset_t band_edge_power_offset;
};

/*wifi config section2 struct*/
struct wifi_conf_sec2_t {
	struct tx_scale_t tx_scale;
	struct misc_t misc;
	struct debug_reg_t debug_reg;
	struct coex_config_t coex_config;
};

struct roaming_param_t {
	uint8_t trigger;
	uint8_t delta;
	uint8_t band_5g_prefer;
};

struct wifi_config_param_t {
	struct roaming_param_t roaming_param;
};

/*wifi config struct*/
struct wifi_conf_t {
	struct version_t version;
	struct board_config_t board_config;
	struct board_config_tpc_t board_config_tpc;
	struct tpc_lut_t tpc_lut;
	struct board_conf_freq_comp_t board_conf_freq_comp;
	struct power_20m_t power_20m;
	struct power_backoff_t power_backoff;
	struct reg_domain_t reg_domain;
	struct band_edge_power_offset_t band_edge_power_offset;
	struct tx_scale_t tx_scale;
	struct misc_t misc;
	struct debug_reg_t debug_reg;
	struct coex_config_t coex_config;
	struct rf_config_t rf_config;
	struct wifi_config_param_t wifi_param;
};


#endif
#define CF_TAB(NAME, MEM_OFFSET, TYPE) \
	{ NAME, (size_t)(&(((struct wifi_conf_t *)(0))->MEM_OFFSET)), TYPE}

#define OFS_MARK_STRING \
	"#-----------------------------------------------------------------\r\n"

static struct nvm_name_table g_config_table[] = {
	/* [Section 1: Version]
	 */
	CF_TAB("Major", version.major, 2),
	CF_TAB("Minor", version.minor, 2),

	/* [SETCTION 2]Board Config: board_config_t
	 */
#ifndef SIPC_SUPPORT
	CF_TAB("Calib_Bypass", board_config.calib_bypass, 2),
	CF_TAB("TxChain_Mask", board_config.txchain_mask, 1),
	CF_TAB("RxChain_Mask", board_config.rxchain_mask, 1),
#else
	CF_TAB("Calibration_Bypass", board_config.calibration_bypass, 2),
	CF_TAB("2G_Chain_Mask", board_config.g2_chain_mask, 1),
	CF_TAB("5G_Chain_Mask", board_config.g5_chain_mask, 1),
#endif

	/* [SETCTION 3]Board Config TPC: board_config_tpc_t
	 */
	CF_TAB("DPD_LUT_idx", board_config_tpc.dpd_lut_idx[0], 1),
	CF_TAB("TPC_Goal_Chain0", board_config_tpc.tpc_goal_chain0[0], 2),
	CF_TAB("TPC_Goal_Chain1", board_config_tpc.tpc_goal_chain1[0], 2),
	/* [SETCTION 4]TPC-LUT: tpc_lut_t
	 */
#ifndef SIPC_SUPPORT
	CF_TAB("Chain0_LUT_0", tpc_lut.chain0_lut[0], 1),
	CF_TAB("Chain0_LUT_1", tpc_lut.chain0_lut[1], 1),
	CF_TAB("Chain0_LUT_2", tpc_lut.chain0_lut[2], 1),
	CF_TAB("Chain0_LUT_3", tpc_lut.chain0_lut[3], 1),
	CF_TAB("Chain0_LUT_4", tpc_lut.chain0_lut[4], 1),
	CF_TAB("Chain0_LUT_5", tpc_lut.chain0_lut[5], 1),
	CF_TAB("Chain0_LUT_6", tpc_lut.chain0_lut[6], 1),
	CF_TAB("Chain0_LUT_7", tpc_lut.chain0_lut[7], 1),
	CF_TAB("Chain1_LUT_0", tpc_lut.chain1_lut[0], 1),
	CF_TAB("Chain1_LUT_1", tpc_lut.chain1_lut[1], 1),
	CF_TAB("Chain1_LUT_2", tpc_lut.chain1_lut[2], 1),
	CF_TAB("Chain1_LUT_3", tpc_lut.chain1_lut[3], 1),
	CF_TAB("Chain1_LUT_4", tpc_lut.chain1_lut[4], 1),
	CF_TAB("Chain1_LUT_5", tpc_lut.chain1_lut[5], 1),
	CF_TAB("Chain1_LUT_6", tpc_lut.chain1_lut[6], 1),
	CF_TAB("Chain1_LUT_7", tpc_lut.chain1_lut[7], 1),
#else
	CF_TAB("2G_LUT_0", tpc_lut.g2_lut[0], 1),
	CF_TAB("2G_LUT_1", tpc_lut.g2_lut[1], 1),
	CF_TAB("2G_LUT_2", tpc_lut.g2_lut[2], 1),
	CF_TAB("2G_LUT_3", tpc_lut.g2_lut[3], 1),
	CF_TAB("2G_LUT_4", tpc_lut.g2_lut[4], 1),
	CF_TAB("2G_LUT_5", tpc_lut.g2_lut[5], 1),
	CF_TAB("2G_LUT_6", tpc_lut.g2_lut[6], 1),
	CF_TAB("2G_LUT_7", tpc_lut.g2_lut[7], 1),
	CF_TAB("5G_LUT_0", tpc_lut.g5_lut[0], 1),
	CF_TAB("5G_LUT_1", tpc_lut.g5_lut[1], 1),
	CF_TAB("5G_LUT_2", tpc_lut.g5_lut[2], 1),
	CF_TAB("5G_LUT_3", tpc_lut.g5_lut[3], 1),
	CF_TAB("5G_LUT_4", tpc_lut.g5_lut[4], 1),
	CF_TAB("5G_LUT_5", tpc_lut.g5_lut[5], 1),
	CF_TAB("5G_LUT_6", tpc_lut.g5_lut[6], 1),
	CF_TAB("5G_LUT_7", tpc_lut.g5_lut[7], 1),
#endif

	/*[SETCTION 5]Board Config Frequency Compensation:
	 * board_conf_freq_comp_t
	 */
	CF_TAB("2G_Channel_Chain0",
			board_conf_freq_comp.channel_2g_chain0[0], 1),
	CF_TAB("2G_Channel_Chain1",
			board_conf_freq_comp.channel_2g_chain1[0], 1),
	CF_TAB("5G_Channel_Chain0",
			board_conf_freq_comp.channel_5g_chain0[0], 1),
	CF_TAB("5G_Channel_Chain1",
			board_conf_freq_comp.channel_5g_chain1[0], 1),

	/*[SETCTION 6]Rate To Power with BW 20M: power_20m_t
	 */
#ifndef SIPC_SUPPORT
	CF_TAB("11b_Power", power_20m.power_11b[0], 1),
	CF_TAB("11ag_Power", power_20m.power_11ag[0], 1),
	CF_TAB("11n_Power", power_20m.power_11n[0], 1),
	CF_TAB("11ac_Power", power_20m.power_11ac[0], 1),
#else
	CF_TAB("11b_Power", power_20m.power_11b[0], 1),
	CF_TAB("11g_Power", power_20m.power_11g[0], 1),
	CF_TAB("11a_Power", power_20m.power_11a[0], 1),
	CF_TAB("11n_2G_Power", power_20m.power_2g_11n[0], 1),
	CF_TAB("11n_5G_Power", power_20m.power_5g_11n[0], 1),
	CF_TAB("11ac_Power", power_20m.power_11ac[0], 1),
#endif

	/*[SETCTION 7]Power Backoff:power_backoff_t
	 */
	CF_TAB("Green_WIFI_offset", power_backoff.green_wifi_offset, 1),
#ifndef SIPC_SUPPORT
	CF_TAB("HT40_Power_offset", power_backoff.ht40_power_offset, 1),
#else
	CF_TAB("HT40_2G_Power_offset", power_backoff.ht40_2g_power_offset, 1),
	CF_TAB("HT40_5G_Power_offset", power_backoff.ht40_5g_power_offset, 1),
#endif
	CF_TAB("VHT40_Power_offset", power_backoff.vht40_power_offset, 1),
	CF_TAB("VHT80_Power_offset", power_backoff.vht80_power_offset, 1),
	CF_TAB("SAR_Power_offset", power_backoff.sar_power_offset, 1),
	CF_TAB("Mean_Power_offset", power_backoff.mean_power_offset, 1),
#ifdef SIPC_SUPPORT
	CF_TAB("APC_mode", power_backoff.apc_mode, 1),
	CF_TAB("MAGIC_word", power_backoff.magic_word, 1),
#endif

	/*[SETCTION 8]Reg Domain:reg_domain_t
	 */
	CF_TAB("reg_domain1", reg_domain.reg_domain1, 4),
	CF_TAB("reg_domain2", reg_domain.reg_domain2, 4),

	/*[SETCTION 9]Band Edge Power offset(MKK, FCC, ETSI):
	 * band_edge_power_offset_t
	 */
	CF_TAB("BW20M", band_edge_power_offset.bw20m[0], 1),
	CF_TAB("BW40M", band_edge_power_offset.bw40m[0], 1),
	CF_TAB("BW80M", band_edge_power_offset.bw80m[0], 1),

	/*[SETCTION 10]TX Scale:tx_scale_t
	 */
	CF_TAB("Chain0_1", tx_scale.chain0[0][0], 1),
	CF_TAB("Chain1_1", tx_scale.chain1[0][0], 1),
	CF_TAB("Chain0_2", tx_scale.chain0[1][0], 1),
	CF_TAB("Chain1_2", tx_scale.chain1[1][0], 1),
	CF_TAB("Chain0_3", tx_scale.chain0[2][0], 1),
	CF_TAB("Chain1_3", tx_scale.chain1[2][0], 1),
	CF_TAB("Chain0_4", tx_scale.chain0[3][0], 1),
	CF_TAB("Chain1_4", tx_scale.chain1[3][0], 1),
	CF_TAB("Chain0_5", tx_scale.chain0[4][0], 1),
	CF_TAB("Chain1_5", tx_scale.chain1[4][0], 1),
	CF_TAB("Chain0_6", tx_scale.chain0[5][0], 1),
	CF_TAB("Chain1_6", tx_scale.chain1[5][0], 1),
	CF_TAB("Chain0_7", tx_scale.chain0[6][0], 1),
	CF_TAB("Chain1_7", tx_scale.chain1[6][0], 1),
	CF_TAB("Chain0_8", tx_scale.chain0[7][0], 1),
	CF_TAB("Chain1_8", tx_scale.chain1[7][0], 1),
	CF_TAB("Chain0_9", tx_scale.chain0[8][0], 1),
	CF_TAB("Chain1_9", tx_scale.chain1[8][0], 1),
	CF_TAB("Chain0_10", tx_scale.chain0[9][0], 1),
	CF_TAB("Chain1_10", tx_scale.chain1[9][0], 1),
	CF_TAB("Chain0_11", tx_scale.chain0[10][0], 1),
	CF_TAB("Chain1_11", tx_scale.chain1[10][0], 1),
	CF_TAB("Chain0_12", tx_scale.chain0[11][0], 1),
	CF_TAB("Chain1_12", tx_scale.chain1[11][0], 1),
	CF_TAB("Chain0_13", tx_scale.chain0[12][0], 1),
	CF_TAB("Chain1_13", tx_scale.chain1[12][0], 1),
	CF_TAB("Chain0_14", tx_scale.chain0[13][0], 1),
	CF_TAB("Chain1_14", tx_scale.chain1[13][0], 1),
	CF_TAB("Chain0_36", tx_scale.chain0[14][0], 1),
	CF_TAB("Chain1_36", tx_scale.chain1[14][0], 1),
	CF_TAB("Chain0_40", tx_scale.chain0[15][0], 1),
	CF_TAB("Chain1_40", tx_scale.chain1[15][0], 1),
	CF_TAB("Chain0_44", tx_scale.chain0[16][0], 1),
	CF_TAB("Chain1_44", tx_scale.chain1[16][0], 1),
	CF_TAB("Chain0_48", tx_scale.chain0[17][0], 1),
	CF_TAB("Chain1_48", tx_scale.chain1[17][0], 1),
	CF_TAB("Chain0_52", tx_scale.chain0[18][0], 1),
	CF_TAB("Chain1_52", tx_scale.chain1[18][0], 1),
	CF_TAB("Chain0_56", tx_scale.chain0[19][0], 1),
	CF_TAB("Chain1_56", tx_scale.chain1[19][0], 1),
	CF_TAB("Chain0_60", tx_scale.chain0[20][0], 1),
	CF_TAB("Chain1_60", tx_scale.chain1[20][0], 1),
	CF_TAB("Chain0_64", tx_scale.chain0[21][0], 1),
	CF_TAB("Chain1_64", tx_scale.chain1[21][0], 1),
	CF_TAB("Chain0_100", tx_scale.chain0[22][0], 1),
	CF_TAB("Chain1_100", tx_scale.chain1[22][0], 1),
	CF_TAB("Chain0_104", tx_scale.chain0[23][0], 1),
	CF_TAB("Chain1_104", tx_scale.chain1[23][0], 1),
	CF_TAB("Chain0_108", tx_scale.chain0[24][0], 1),
	CF_TAB("Chain1_108", tx_scale.chain1[24][0], 1),
	CF_TAB("Chain0_112", tx_scale.chain0[25][0], 1),
	CF_TAB("Chain1_112", tx_scale.chain1[25][0], 1),
	CF_TAB("Chain0_116", tx_scale.chain0[26][0], 1),
	CF_TAB("Chain1_116", tx_scale.chain1[26][0], 1),
	CF_TAB("Chain0_120", tx_scale.chain0[27][0], 1),
	CF_TAB("Chain1_120", tx_scale.chain1[27][0], 1),
	CF_TAB("Chain0_124", tx_scale.chain0[28][0], 1),
	CF_TAB("Chain1_124", tx_scale.chain1[28][0], 1),
	CF_TAB("Chain0_128", tx_scale.chain0[29][0], 1),
	CF_TAB("Chain1_128", tx_scale.chain1[29][0], 1),
	CF_TAB("Chain0_132", tx_scale.chain0[30][0], 1),
	CF_TAB("Chain1_132", tx_scale.chain1[30][0], 1),
	CF_TAB("Chain0_136", tx_scale.chain0[31][0], 1),
	CF_TAB("Chain1_136", tx_scale.chain1[31][0], 1),
	CF_TAB("Chain0_140", tx_scale.chain0[32][0], 1),
	CF_TAB("Chain1_140", tx_scale.chain1[32][0], 1),
	CF_TAB("Chain0_144", tx_scale.chain0[33][0], 1),
	CF_TAB("Chain1_144", tx_scale.chain1[33][0], 1),
	CF_TAB("Chain0_149", tx_scale.chain0[34][0], 1),
	CF_TAB("Chain1_149", tx_scale.chain1[34][0], 1),
	CF_TAB("Chain0_153", tx_scale.chain0[35][0], 1),
	CF_TAB("Chain1_153", tx_scale.chain1[35][0], 1),
	CF_TAB("Chain0_157", tx_scale.chain0[36][0], 1),
	CF_TAB("Chain1_157", tx_scale.chain1[36][0], 1),
	CF_TAB("Chain0_161", tx_scale.chain0[37][0], 1),
	CF_TAB("Chain1_161", tx_scale.chain1[37][0], 1),
	CF_TAB("Chain0_165", tx_scale.chain0[38][0], 1),
	CF_TAB("Chain1_165", tx_scale.chain1[38][0], 1),

	/*[SETCTION 11]misc:misc_t
	 */
	CF_TAB("DFS_switch", misc.dfs_switch, 1),
	CF_TAB("power_save_switch", misc.power_save_switch, 1),
#ifndef SIPC_SUPPORT
	CF_TAB("ex-Fem_and_ex-LNA_param_setup", misc.fem_lan_param_setup, 1),
#endif
	CF_TAB("rssi_report_diff", misc.rssi_report_diff, 1),
#ifdef SIPC_SUPPORT
	CF_TAB("Ex_FEM_En", misc.ex_fem_en, 1),
	CF_TAB("TX_En_Ctrl", misc.tx_en_ctrl, 1),
	CF_TAB("LNA_En_Ctrl", misc.lna_en_ctrl, 1),
	CF_TAB("SW_En_Ctrl", misc.sw_en_ctrl, 1),
	CF_TAB("LNA_Gain", misc.lna_gain, 1),
	CF_TAB("LNA_Bypass_Gain", misc.lna_bypass_gain, 1),
	CF_TAB("Ex_FEM_PDET", misc.ex_fem_pdet, 1),
#endif
	/*[SETCTION 12]debug reg:debug_reg_t
	 */
	CF_TAB("address", debug_reg.address[0], 4),
	CF_TAB("value", debug_reg.value[0], 4),

	/*[SETCTION 13]coex_config:coex_config_t
	 */
	CF_TAB("bt_performance_cfg0", coex_config.bt_performance_cfg0, 4),
	CF_TAB("bt_performance_cfg1", coex_config.bt_performance_cfg1, 4),
	CF_TAB("wifi_performance_cfg0", coex_config.wifi_performance_cfg0, 4),
	CF_TAB("wifi_performance_cfg2", coex_config.wifi_performance_cfg2, 4),
	CF_TAB("strategy_cfg0", coex_config.strategy_cfg0, 4),
	CF_TAB("strategy_cfg1", coex_config.strategy_cfg1, 4),
	CF_TAB("strategy_cfg2", coex_config.strategy_cfg2, 4),
	CF_TAB("compatibility_cfg0", coex_config.compatibility_cfg0, 4),
	CF_TAB("compatibility_cfg1", coex_config.compatibility_cfg1, 4),
	CF_TAB("ant_cfg0", coex_config.ant_cfg0, 4),
	CF_TAB("ant_cfg1", coex_config.ant_cfg1, 4),
	CF_TAB("isolation_cfg0", coex_config.isolation_cfg0, 4),
	CF_TAB("isolation_cfg1", coex_config.isolation_cfg1, 4),
	CF_TAB("reserved_cfg0", coex_config.reserved_cfg0, 4),
	CF_TAB("reserved_cfg1", coex_config.reserved_cfg1, 4),
	CF_TAB("reserved_cfg2", coex_config.reserved_cfg2, 4),
	CF_TAB("reserved_cfg3", coex_config.reserved_cfg3, 4),
	CF_TAB("reserved_cfg4", coex_config.reserved_cfg4, 4),
	CF_TAB("reserved_cfg5", coex_config.reserved_cfg5, 4),
	CF_TAB("reserved_cfg6", coex_config.reserved_cfg6, 4),
	CF_TAB("reserved_cfg7", coex_config.reserved_cfg7, 4),

	/*
	 * [SETCTION 14] rf_config:rf_config_t
	 */
	CF_TAB("rf_config", rf_config.rf_data, 1),

	/* [SECTION 15] wifi_param:wifi_config_param_t */
	CF_TAB("roaming_trigger", wifi_param.roaming_param.trigger, 1),
	CF_TAB("roaming_delta", wifi_param.roaming_param.delta, 1),
	CF_TAB("roaming_5g_prefer", wifi_param.roaming_param.band_5g_prefer, 1),
};

static int find_type(char key, char *str, int *index_ptr)
{
	int index = *index_ptr;
	if ((key >= 'a' && key <= 'w') ||
		(key >= 'y' && key <= 'z') ||
		(key >= 'A' && key <= 'W') ||
		(key >= 'Y' && key <= 'Z') ||
		('_' == key))
		return 1;
	if ((key >= '0' && key <= '9') ||
		('-' == key))
		return 2;
	if (('x' == key) ||
		('X' == key) ||
		('.' == key))
		return 3;
	if ((index > 0) && (str[index - 1] == ',') &&
		(key == '\r') && (str[index + 1] == '\n')) {
		*index_ptr = index + 1;
		return 0;
	}
	if ((key == '\0') ||
		('\r' == key) ||
		('\n' == key) ||
		('#' == key))
		return 4;
	return 0;
}

static int wifi_nvm_set_cmd(struct nvm_name_table *pTable,
	struct nvm_cali_cmd *cmd, void *p_data)
{
	int i;
	unsigned char *p;

	if ((1 != pTable->type) &&
		(2 != pTable->type) &&
		(4 != pTable->type))
		return -1;

	p = (unsigned char *)(p_data) + pTable->mem_offset;

	wl_info("[g_table]%s, offset:%u, num:%u, value:\
			%d %d %d %d %d %d %d %d %d %d \n",
			pTable->itm, pTable->mem_offset, cmd->num,
			cmd->par[0], cmd->par[1], cmd->par[2],
			cmd->par[3], cmd->par[4], cmd->par[5],
			cmd->par[6], cmd->par[7], cmd->par[8],
			cmd->par[9]);

	for (i = 0; i < cmd->num; i++) {
		if (1 == pTable->type)
			*((unsigned char *)p + i)
			= (unsigned char)(cmd->par[i]);
		else if (2 == pTable->type)
			*((unsigned short *)p + i)
			= (unsigned short)(cmd->par[i]);
		else if (4 == pTable->type)
			*((unsigned int *)p + i)
			= (unsigned int)(cmd->par[i]);
		else
			wl_info("%s, type err\n", __func__);
	}
	return 0;
}

static void get_cmd_par(char *str, struct nvm_cali_cmd *cmd)
{
	int i, j, bufType, cType, flag;
	char tmp[64];
	char c;
	long val;

	bufType = -1;
	cType = 0;
	flag = 0;
	memset(cmd, 0, sizeof(struct nvm_cali_cmd));

	for (i = 0, j = 0;; i++) {
		c = str[i];
		cType = find_type(c, str, &i);
		if ((1 == cType) ||
			(2 == cType) ||
			(3 == cType)) {
			tmp[j] = c;
			j++;
			if (-1 == bufType) {
				if (2 == cType)
					bufType = 2;
				else
					bufType = 1;
			} else if (2 == bufType) {
				if (1 == cType)
					bufType = 1;
			}
			continue;
		}
		if (-1 != bufType) {
			tmp[j] = '\0';

			if ((1 == bufType) && (0 == flag)) {
				strcpy(cmd->itm, tmp);
				flag = 1;
			} else {
				if (kstrtol(tmp, 0, &val))
					pr_info(" %s ", tmp);
			/* pr_err("kstrtol %s: error\n", tmp); */
				cmd->par[cmd->num] = val & 0xFFFFFFFF;
				cmd->num++;
			}
			bufType = -1;
			j = 0;
		}
		if (0 == cType)
			continue;
		if (4 == cType)
			return;
	}
}

static struct nvm_name_table *cf_table_match(struct nvm_cali_cmd *cmd)
{
	int i;
	struct nvm_name_table *pTable = NULL;
	int len = sizeof(g_config_table) / sizeof(struct nvm_name_table);

	if (NULL == cmd)
		return NULL;
	for (i = 0; i < len; i++) {
		if (NULL == g_config_table[i].itm)
			continue;
		if (0 != strcmp(g_config_table[i].itm, cmd->itm))
			continue;
		pTable = &g_config_table[i];
		break;
	}
	return pTable;
}

static int wifi_nvm_buf_operate(char *pBuf, int file_len, void *p_data)
{
	int i, p;
	struct nvm_cali_cmd *cmd;
	struct wifi_conf_t *conf;
	struct nvm_name_table *pTable = NULL;

	if ((NULL == pBuf) || (0 == file_len))
		return -1;

	cmd = kzalloc(sizeof(struct nvm_cali_cmd), GFP_KERNEL);
	for (i = 0, p = 0; i < file_len; i++) {
		if (('\n' == *(pBuf + i)) ||
			('\r' == *(pBuf + i)) ||
			('\0' == *(pBuf + i))) {
			if (5 <= (i - p)) {
				get_cmd_par((pBuf + p), cmd);
				pTable = cf_table_match(cmd);

				if (NULL != pTable) {
					wifi_nvm_set_cmd(pTable, cmd, p_data);
					if (strcmp(pTable->itm, "rf_config") == 0) {
						conf = (struct wifi_conf_t *)p_data;
						conf->rf_config.rf_data_len = cmd->num;
					}
				}
			}
			p = i + 1;
		}
	}

	kfree(cmd);
	return 0;
}


static const uint16_t CRC_table[] = {0x0000,0xCC01,0xD801,0x1400,0xF001,0x3C00,0x2800,0xE401,0xA001,0x6C00,0x7800,0xB401,0x5000,0x9C01,0x8801,0x4400};
static uint16_t CRC16(const uint8_t *buf, uint32_t len){ uint16_t CRC=0xFFFF; for(uint32_t i=0;i<len;i++){ uint8_t ch=buf[i]; CRC=CRC_table[(ch^CRC)&15]^(CRC>>4); CRC=CRC_table[((ch>>4)^CRC)&15]^(CRC>>4);} return CRC; }
static void emit(FILE *o, const char *name, const uint8_t *p, size_t n){
  fprintf(o,"static const uint8_t %s[%zu] = {", name, n);
  for(size_t i=0;i<n;i++) fprintf(o,"%s0x%02x,", i%16?"":"\n\t", p[i]);
  fprintf(o,"\n};\n");
}
int main(int argc,char**argv){
  FILE *f=fopen(argv[1],"rb"); fseek(f,0,2); long n=ftell(f); fseek(f,0,0);
  char *buf=malloc(n+1); fread(buf,1,n,f); buf[n]=0;
  struct wifi_conf_t *w=calloc(1,sizeof(*w));
  wifi_nvm_buf_operate(buf,n,w);
  FILE *o=fopen(argv[2],"w");
  fprintf(o,"/* generated by agm-h6/wcn/inigen from %s (vendor rf_marlin3.c parser, SIPC_SUPPORT) */\n", argv[1]);
  emit(o,"wifi_ini_sec1",(uint8_t*)w,sizeof(struct wifi_conf_sec1_t));
  emit(o,"wifi_ini_sec2",(uint8_t*)&w->tx_scale,sizeof(struct wifi_conf_sec2_t));
  emit(o,"wifi_ini_sec3",w->rf_config.rf_data,w->rf_config.rf_data_len);
  emit(o,"wifi_ini_sec4",(uint8_t*)&w->wifi_param,sizeof(struct wifi_config_param_t));
  fprintf(o,"static const uint16_t wifi_ini_crc[4] = { %#x, %#x, %#x, %#x };\n",
    CRC16((uint8_t*)w,sizeof(struct wifi_conf_sec1_t)), CRC16((uint8_t*)&w->tx_scale,sizeof(struct wifi_conf_sec2_t)),
    CRC16(w->rf_config.rf_data,w->rf_config.rf_data_len), CRC16((uint8_t*)&w->wifi_param,sizeof(struct wifi_config_param_t)));
  fclose(o);
  fprintf(stderr,"sec1 %zu sec2 %zu sec3 %d sec4 %zu; version %u.%u calib_bypass %u masks %u/%u reg %#x/%#x\n",
    sizeof(struct wifi_conf_sec1_t), sizeof(struct wifi_conf_sec2_t), w->rf_config.rf_data_len, sizeof(struct wifi_config_param_t),
    w->version.major, w->version.minor, w->board_config.calibration_bypass, w->board_config.g2_chain_mask, w->board_config.g5_chain_mask,
    w->reg_domain.reg_domain1, w->reg_domain.reg_domain2);
  return 0;
}
