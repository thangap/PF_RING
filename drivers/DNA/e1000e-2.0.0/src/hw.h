/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2012 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _E1000_HW_H_
#define _E1000_HW_H_

#include "regs.h"
#include "defines.h"

struct e1000_hw;

#define E1000_DEV_ID_82571EB_COPPER		0x105E
#define E1000_DEV_ID_82571EB_FIBER		0x105F
#define E1000_DEV_ID_82571EB_SERDES		0x1060
#define E1000_DEV_ID_82571EB_SERDES_DUAL	0x10D9
#define E1000_DEV_ID_82571EB_SERDES_QUAD	0x10DA
#define E1000_DEV_ID_82571EB_QUAD_COPPER	0x10A4
#define E1000_DEV_ID_82571PT_QUAD_COPPER	0x10D5
#define E1000_DEV_ID_82571EB_QUAD_FIBER		0x10A5
#define E1000_DEV_ID_82571EB_QUAD_COPPER_LP	0x10BC
#define E1000_DEV_ID_82572EI_COPPER		0x107D
#define E1000_DEV_ID_82572EI_FIBER		0x107E
#define E1000_DEV_ID_82572EI_SERDES		0x107F
#define E1000_DEV_ID_82572EI			0x10B9
#define E1000_DEV_ID_82573E			0x108B
#define E1000_DEV_ID_82573E_IAMT		0x108C
#define E1000_DEV_ID_82573L			0x109A
#define E1000_DEV_ID_82574L			0x10D3
#define E1000_DEV_ID_82574LA			0x10F6
#define E1000_DEV_ID_82583V			0x150C
#define E1000_DEV_ID_80003ES2LAN_COPPER_DPT	0x1096
#define E1000_DEV_ID_80003ES2LAN_SERDES_DPT	0x1098
#define E1000_DEV_ID_80003ES2LAN_COPPER_SPT	0x10BA
#define E1000_DEV_ID_80003ES2LAN_SERDES_SPT	0x10BB
#define E1000_DEV_ID_ICH8_82567V_3		0x1501
#define E1000_DEV_ID_ICH8_IGP_M_AMT		0x1049
#define E1000_DEV_ID_ICH8_IGP_AMT		0x104A
#define E1000_DEV_ID_ICH8_IGP_C			0x104B
#define E1000_DEV_ID_ICH8_IFE			0x104C
#define E1000_DEV_ID_ICH8_IFE_GT		0x10C4
#define E1000_DEV_ID_ICH8_IFE_G			0x10C5
#define E1000_DEV_ID_ICH8_IGP_M			0x104D
#define E1000_DEV_ID_ICH9_IGP_M			0x10BF
#define E1000_DEV_ID_ICH9_IGP_M_AMT		0x10F5
#define E1000_DEV_ID_ICH9_IGP_M_V		0x10CB
#define E1000_DEV_ID_ICH9_IGP_AMT		0x10BD
#define E1000_DEV_ID_ICH9_BM			0x10E5
#define E1000_DEV_ID_ICH9_IGP_C			0x294C
#define E1000_DEV_ID_ICH9_IFE			0x10C0
#define E1000_DEV_ID_ICH9_IFE_GT		0x10C3
#define E1000_DEV_ID_ICH9_IFE_G			0x10C2
#define E1000_DEV_ID_ICH10_R_BM_LM		0x10CC
#define E1000_DEV_ID_ICH10_R_BM_LF		0x10CD
#define E1000_DEV_ID_ICH10_R_BM_V		0x10CE
#define E1000_DEV_ID_ICH10_D_BM_LM		0x10DE
#define E1000_DEV_ID_ICH10_D_BM_LF		0x10DF
#define E1000_DEV_ID_ICH10_D_BM_V		0x1525

#define E1000_DEV_ID_PCH_M_HV_LM		0x10EA
#define E1000_DEV_ID_PCH_M_HV_LC		0x10EB
#define E1000_DEV_ID_PCH_D_HV_DM		0x10EF
#define E1000_DEV_ID_PCH_D_HV_DC		0x10F0
#define E1000_DEV_ID_PCH2_LV_LM			0x1502
#define E1000_DEV_ID_PCH2_LV_V			0x1503
#define E1000_DEV_ID_PCH_LPT_I217_LM		0x153A
#define E1000_DEV_ID_PCH_LPT_I217_V		0x153B
#define E1000_REVISION_0	0
#define E1000_REVISION_1	1
#define E1000_REVISION_3	3
#define E1000_REVISION_4	4

#define E1000_FUNC_0		0
#define E1000_FUNC_1		1

#define E1000_ALT_MAC_ADDRESS_OFFSET_LAN0	0
#define E1000_ALT_MAC_ADDRESS_OFFSET_LAN1	3

enum e1000_mac_type {
	e1000_undefined = 0,
	e1000_82571,
	e1000_82572,
	e1000_82573,
	e1000_82574,
	e1000_82583,
	e1000_80003es2lan,
	e1000_ich8lan,
	e1000_ich9lan,
	e1000_ich10lan,
	e1000_pchlan,
	e1000_pch2lan,
	e1000_pch_lpt,
	e1000_num_macs		/* List is 1-based, so subtract 1 for true count. */
};

enum e1000_media_type {
	e1000_media_type_unknown = 0,
	e1000_media_type_copper = 1,
	e1000_media_type_fiber = 2,
	e1000_media_type_internal_serdes = 3,
	e1000_num_media_types
};

enum e1000_nvm_type {
	e1000_nvm_unknown = 0,
	e1000_nvm_none,
	e1000_nvm_eeprom_spi,
	e1000_nvm_flash_hw,
	e1000_nvm_flash_sw
};

enum e1000_nvm_override {
	e1000_nvm_override_none = 0,
	e1000_nvm_override_spi_small,
	e1000_nvm_override_spi_large,
};

enum e1000_phy_type {
	e1000_phy_unknown = 0,
	e1000_phy_none,
	e1000_phy_m88,
	e1000_phy_igp,
	e1000_phy_igp_2,
	e1000_phy_gg82563,
	e1000_phy_igp_3,
	e1000_phy_ife,
	e1000_phy_bm,
	e1000_phy_82578,
	e1000_phy_82577,
	e1000_phy_82579,
	e1000_phy_i217,
};

enum e1000_bus_type {
	e1000_bus_type_unknown = 0,
	e1000_bus_type_pci,
	e1000_bus_type_pcix,
	e1000_bus_type_pci_express,
	e1000_bus_type_reserved
};

enum e1000_bus_speed {
	e1000_bus_speed_unknown = 0,
	e1000_bus_speed_33,
	e1000_bus_speed_66,
	e1000_bus_speed_100,
	e1000_bus_speed_120,
	e1000_bus_speed_133,
	e1000_bus_speed_2500,
	e1000_bus_speed_5000,
	e1000_bus_speed_reserved
};

enum e1000_bus_width {
	e1000_bus_width_unknown = 0,
	e1000_bus_width_pcie_x1,
	e1000_bus_width_pcie_x2,
	e1000_bus_width_pcie_x4 = 4,
	e1000_bus_width_pcie_x8 = 8,
	e1000_bus_width_32,
	e1000_bus_width_64,
	e1000_bus_width_reserved
};

enum e1000_1000t_rx_status {
	e1000_1000t_rx_status_not_ok = 0,
	e1000_1000t_rx_status_ok,
	e1000_1000t_rx_status_undefined = 0xFF
};

enum e1000_rev_polarity {
	e1000_rev_polarity_normal = 0,
	e1000_rev_polarity_reversed,
	e1000_rev_polarity_undefined = 0xFF
};

enum e1000_fc_mode {
	e1000_fc_none = 0,
	e1000_fc_rx_pause,
	e1000_fc_tx_pause,
	e1000_fc_full,
	e1000_fc_default = 0xFF
};

enum e1000_ms_type {
	e1000_ms_hw_default = 0,
	e1000_ms_force_master,
	e1000_ms_force_slave,
	e1000_ms_auto
};

enum e1000_smart_speed {
	e1000_smart_speed_default = 0,
	e1000_smart_speed_on,
	e1000_smart_speed_off
};

enum e1000_serdes_link_state {
	e1000_serdes_link_down = 0,
	e1000_serdes_link_autoneg_progress,
	e1000_serdes_link_autoneg_complete,
	e1000_serdes_link_forced_up
};

#ifndef __le16
#define __le16 u16
#endif
#ifndef __le32
#define __le32 u32
#endif
#ifndef __le64
#define __le64 u64
#endif
/* Receive Descriptor - Extended */
union e1000_rx_desc_extended {
	struct {
		__le64 buffer_addr;
		__le64 reserved;
	} read;
	struct {
		struct {
			__le32 mrq;	/* Multiple Rx Queues */
			union {
				__le32 rss;	/* RSS Hash */
				struct {
					__le16 ip_id;	/* IP id */
					__le16 csum;	/* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			__le32 status_error;	/* ext status/error */
			__le16 length;
			__le16 vlan;	/* VLAN tag */
		} upper;
	} wb;			/* writeback */
};

#define MAX_PS_BUFFERS 4
/* Receive Descriptor - Packet Split */
union e1000_rx_desc_packet_split {
	struct {
		/* one buffer for protocol header(s), three data buffers */
		__le64 buffer_addr[MAX_PS_BUFFERS];
	} read;
	struct {
		struct {
			__le32 mrq;	/* Multiple Rx Queues */
			union {
				__le32 rss;	/* RSS Hash */
				struct {
					__le16 ip_id;	/* IP id */
					__le16 csum;	/* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			__le32 status_error;	/* ext status/error */
			__le16 length0;	/* length of buffer 0 */
			__le16 vlan;	/* VLAN tag */
		} middle;
		struct {
			__le16 header_status;
			__le16 length[3];	/* length of buffers 1-3 */
		} upper;
		__le64 reserved;
	} wb;			/* writeback */
};

/* Transmit Descriptor */
struct e1000_tx_desc {
	__le64 buffer_addr;	/* Address of the descriptor's data buffer */
	union {
		__le32 data;
		struct {
			__le16 length;	/* Data buffer length */
			u8 cso;	/* Checksum offset */
			u8 cmd;	/* Descriptor control */
		} flags;
	} lower;
	union {
		__le32 data;
		struct {
			u8 status;	/* Descriptor status */
			u8 css;	/* Checksum start */
			__le16 special;
		} fields;
	} upper;
};

/* Offload Context Descriptor */
struct e1000_context_desc {
	union {
		__le32 ip_config;
		struct {
			u8 ipcss;	/* IP checksum start */
			u8 ipcso;	/* IP checksum offset */
			__le16 ipcse;	/* IP checksum end */
		} ip_fields;
	} lower_setup;
	union {
		__le32 tcp_config;
		struct {
			u8 tucss;	/* TCP checksum start */
			u8 tucso;	/* TCP checksum offset */
			__le16 tucse;	/* TCP checksum end */
		} tcp_fields;
	} upper_setup;
	__le32 cmd_and_length;
	union {
		__le32 data;
		struct {
			u8 status;	/* Descriptor status */
			u8 hdr_len;	/* Header length */
			__le16 mss;	/* Maximum segment size */
		} fields;
	} tcp_seg_setup;
};

/* Offload data descriptor */
struct e1000_data_desc {
	__le64 buffer_addr;	/* Address of the descriptor's buffer address */
	union {
		__le32 data;
		struct {
			__le16 length;	/* Data buffer length */
			u8 typ_len_ext;
			u8 cmd;
		} flags;
	} lower;
	union {
		__le32 data;
		struct {
			u8 status;	/* Descriptor status */
			u8 popts;	/* Packet Options */
			__le16 special;
		} fields;
	} upper;
};

/* Statistics counters collected by the MAC */
struct e1000_hw_stats {
	u64 crcerrs;
	u64 algnerrc;
	u64 symerrs;
	u64 rxerrc;
	u64 mpc;
	u64 scc;
	u64 ecol;
	u64 mcc;
	u64 latecol;
	u64 colc;
	u64 dc;
	u64 tncrs;
	u64 sec;
	u64 cexterr;
	u64 rlec;
	u64 xonrxc;
	u64 xontxc;
	u64 xoffrxc;
	u64 xofftxc;
	u64 fcruc;
	u64 prc64;
	u64 prc127;
	u64 prc255;
	u64 prc511;
	u64 prc1023;
	u64 prc1522;
	u64 gprc;
	u64 bprc;
	u64 mprc;
	u64 gptc;
	u64 gorc;
	u64 gotc;
	u64 rnbc;
	u64 ruc;
	u64 rfc;
	u64 roc;
	u64 rjc;
	u64 mgprc;
	u64 mgpdc;
	u64 mgptc;
	u64 tor;
	u64 tot;
	u64 tpr;
	u64 tpt;
	u64 ptc64;
	u64 ptc127;
	u64 ptc255;
	u64 ptc511;
	u64 ptc1023;
	u64 ptc1522;
	u64 mptc;
	u64 bptc;
	u64 tsctc;
	u64 tsctfc;
	u64 iac;
	u64 icrxptc;
	u64 icrxatc;
	u64 ictxptc;
	u64 ictxatc;
	u64 ictxqec;
	u64 ictxqmtc;
	u64 icrxdmtc;
	u64 icrxoc;
	u64 doosync;
};

struct e1000_phy_stats {
	u32 idle_errors;
	u32 receive_errors;
};

struct e1000_host_mng_dhcp_cookie {
	u32 signature;
	u8 status;
	u8 reserved0;
	u16 vlan_id;
	u32 reserved1;
	u16 reserved2;
	u8 reserved3;
	u8 checksum;
};

/* Host Interface "Rev 1" */
struct e1000_host_command_header {
	u8 command_id;
	u8 command_length;
	u8 command_options;
	u8 checksum;
};

#define E1000_HI_MAX_DATA_LENGTH	252
struct e1000_host_command_info {
	struct e1000_host_command_header command_header;
	u8 command_data[E1000_HI_MAX_DATA_LENGTH];
};

/* Host Interface "Rev 2" */
struct e1000_host_mng_command_header {
	u8 command_id;
	u8 checksum;
	u16 reserved1;
	u16 reserved2;
	u16 command_length;
};

#define E1000_HI_MAX_MNG_DATA_LENGTH	0x6F8
struct e1000_host_mng_command_info {
	struct e1000_host_mng_command_header command_header;
	u8 command_data[E1000_HI_MAX_MNG_DATA_LENGTH];
};

#include "mac.h"
#include "phy.h"
#include "nvm.h"
#include "manage.h"

struct e1000_mac_operations {
	/* Function pointers for the MAC. */
	s32(*init_params) (struct e1000_hw *);
	s32(*id_led_init) (struct e1000_hw *);
	s32(*blink_led) (struct e1000_hw *);
	s32(*check_for_link) (struct e1000_hw *);
	bool (*check_mng_mode) (struct e1000_hw *hw);
	 s32(*cleanup_led) (struct e1000_hw *);
	void (*clear_hw_cntrs) (struct e1000_hw *);
	void (*clear_vfta) (struct e1000_hw *);
	 s32(*get_bus_info) (struct e1000_hw *);
	void (*set_lan_id) (struct e1000_hw *);
	 s32(*get_link_up_info) (struct e1000_hw *, u16 *, u16 *);
	 s32(*led_on) (struct e1000_hw *);
	 s32(*led_off) (struct e1000_hw *);
	void (*update_mc_addr_list) (struct e1000_hw *, u8 *, u32);
	 s32(*reset_hw) (struct e1000_hw *);
	 s32(*init_hw) (struct e1000_hw *);
	 s32(*setup_link) (struct e1000_hw *);
	 s32(*setup_physical_interface) (struct e1000_hw *);
	 s32(*setup_led) (struct e1000_hw *);
	void (*write_vfta) (struct e1000_hw *, u32, u32);
	void (*config_collision_dist) (struct e1000_hw *);
	void (*rar_set) (struct e1000_hw *, u8 *, u32);
	 s32(*read_mac_addr) (struct e1000_hw *);
	 s32(*validate_mdi_setting) (struct e1000_hw *);
	 s32(*mng_host_if_write) (struct e1000_hw *, u8 *, u16, u16, u8 *);
	 s32(*mng_write_cmd_header) (struct e1000_hw *hw,
				     struct e1000_host_mng_command_header *);
	 s32(*mng_enable_host_if) (struct e1000_hw *);
	 s32(*wait_autoneg) (struct e1000_hw *);
	 s32(*acquire_swfw_sync) (struct e1000_hw *, u16);
	void (*release_swfw_sync) (struct e1000_hw *, u16);
};

/*
 * When to use various PHY register access functions:
 *
 *                 Func   Caller
 *   Function      Does   Does    When to use
 *   ~~~~~~~~~~~~  ~~~~~  ~~~~~~  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *   X_reg         L,P,A  n/a     for simple PHY reg accesses
 *   X_reg_locked  P,A    L       for multiple accesses of different regs
 *                                on different pages
 *   X_reg_page    A      L,P     for multiple accesses of different regs
 *                                on the same page
 *
 * Where X=[read|write], L=locking, P=sets page, A=register access
 *
 */
struct e1000_phy_operations {
	s32(*init_params) (struct e1000_hw *);
	s32(*acquire) (struct e1000_hw *);
	s32(*cfg_on_link_up) (struct e1000_hw *);
	s32(*check_polarity) (struct e1000_hw *);
	s32(*check_reset_block) (struct e1000_hw *);
	s32(*commit) (struct e1000_hw *);
	s32(*force_speed_duplex) (struct e1000_hw *);
	s32(*get_cfg_done) (struct e1000_hw *hw);
	s32(*get_cable_length) (struct e1000_hw *);
	s32(*get_info) (struct e1000_hw *);
	s32(*set_page) (struct e1000_hw *, u16);
	s32(*read_reg) (struct e1000_hw *, u32, u16 *);
	s32(*read_reg_locked) (struct e1000_hw *, u32, u16 *);
	s32(*read_reg_page) (struct e1000_hw *, u32, u16 *);
	void (*release) (struct e1000_hw *);
	 s32(*reset) (struct e1000_hw *);
	 s32(*set_d0_lplu_state) (struct e1000_hw *, bool);
	 s32(*set_d3_lplu_state) (struct e1000_hw *, bool);
	 s32(*write_reg) (struct e1000_hw *, u32, u16);
	 s32(*write_reg_locked) (struct e1000_hw *, u32, u16);
	 s32(*write_reg_page) (struct e1000_hw *, u32, u16);
	void (*power_up) (struct e1000_hw *);
	void (*power_down) (struct e1000_hw *);
};

struct e1000_nvm_operations {
	s32(*init_params) (struct e1000_hw *);
	s32(*acquire) (struct e1000_hw *);
	s32(*read) (struct e1000_hw *, u16, u16, u16 *);
	void (*release) (struct e1000_hw *);
	void (*reload) (struct e1000_hw *);
	 s32(*update) (struct e1000_hw *);
	 s32(*valid_led_default) (struct e1000_hw *, u16 *);
	 s32(*validate) (struct e1000_hw *);
	 s32(*write) (struct e1000_hw *, u16, u16, u16 *);
};

struct e1000_mac_info {
	struct e1000_mac_operations ops;
	u8 addr[ETH_ALEN];
	u8 perm_addr[ETH_ALEN];

	enum e1000_mac_type type;

	u32 collision_delta;
	u32 ledctl_default;
	u32 ledctl_mode1;
	u32 ledctl_mode2;
	u32 mc_filter_type;
	u32 tx_packet_delta;
	u32 txcw;

	u16 current_ifs_val;
	u16 ifs_max_val;
	u16 ifs_min_val;
	u16 ifs_ratio;
	u16 ifs_step_size;
	u16 mta_reg_count;

	/* Maximum size of the MTA register table in all supported adapters */
#define MAX_MTA_REG 128
	u32 mta_shadow[MAX_MTA_REG];
	u16 rar_entry_count;

	u8 forced_speed_duplex;

	bool adaptive_ifs;
	bool has_fwsm;
	bool arc_subsystem_valid;
	bool autoneg;
	bool autoneg_failed;
	bool get_link_status;
	bool in_ifs_mode;
	enum e1000_serdes_link_state serdes_link_state;
	bool serdes_has_link;
	bool tx_pkt_filtering;
};

struct e1000_phy_info {
	struct e1000_phy_operations ops;
	enum e1000_phy_type type;

	enum e1000_1000t_rx_status local_rx;
	enum e1000_1000t_rx_status remote_rx;
	enum e1000_ms_type ms_type;
	enum e1000_ms_type original_ms_type;
	enum e1000_rev_polarity cable_polarity;
	enum e1000_smart_speed smart_speed;

	u32 addr;
	u32 id;
	u32 reset_delay_us;	/* in usec */
	u32 revision;

	enum e1000_media_type media_type;

	u16 autoneg_advertised;
	u16 autoneg_mask;
	u16 cable_length;
	u16 max_cable_length;
	u16 min_cable_length;

	u8 mdix;

	bool disable_polarity_correction;
	bool is_mdix;
	bool polarity_correction;
	bool speed_downgraded;
	bool autoneg_wait_to_complete;
};

struct e1000_nvm_info {
	struct e1000_nvm_operations ops;
	enum e1000_nvm_type type;
	enum e1000_nvm_override override;

	u32 flash_bank_size;
	u32 flash_base_addr;

	u16 word_size;
	u16 delay_usec;
	u16 address_bits;
	u16 opcode_bits;
	u16 page_size;
};

struct e1000_bus_info {
	enum e1000_bus_type type;
	enum e1000_bus_speed speed;
	enum e1000_bus_width width;

	u16 func;
	u16 pci_cmd_word;
};

struct e1000_fc_info {
	u32 high_water;		/* Flow control high-water mark */
	u32 low_water;		/* Flow control low-water mark */
	u16 pause_time;		/* Flow control pause timer */
	u16 refresh_time;	/* Flow control refresh timer */
	bool send_xon;		/* Flow control send XON */
	bool strict_ieee;	/* Strict IEEE mode */
	enum e1000_fc_mode current_mode;	/* FC mode in effect */
	enum e1000_fc_mode requested_mode;	/* FC mode requested by caller */
};

struct e1000_dev_spec_82571 {
	bool laa_is_present;
	u32 smb_counter;
};

struct e1000_dev_spec_80003es2lan {
	bool mdic_wa_enable;
};

struct e1000_shadow_ram {
	u16 value;
	bool modified;
};

#define E1000_ICH8_SHADOW_RAM_WORDS  2048

struct e1000_dev_spec_ich8lan {
	bool kmrn_lock_loss_workaround_enabled;
	struct e1000_shadow_ram shadow_ram[E1000_ICH8_SHADOW_RAM_WORDS];
	bool nvm_k1_enabled;
	bool eee_disable;
	u16 eee_lp_ability;
};

struct e1000_hw {
	struct e1000_adapter *adapter;

	void __iomem *hw_addr;
	void __iomem *flash_address;

	struct e1000_mac_info mac;
	struct e1000_fc_info fc;
	struct e1000_phy_info phy;
	struct e1000_nvm_info nvm;
	struct e1000_bus_info bus;
	struct e1000_host_mng_dhcp_cookie mng_cookie;

	union {
		struct e1000_dev_spec_82571 e82571;
		struct e1000_dev_spec_80003es2lan e80003es2lan;
		struct e1000_dev_spec_ich8lan ich8lan;
	} dev_spec;
};

#include "82571.h"
#include "80003es2lan.h"
#include "ich8lan.h"

/* These functions must be implemented by drivers */
s32 e1000_read_pcie_cap_reg(struct e1000_hw *hw, u32 reg, u16 *value);

#endif