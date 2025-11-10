/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014-2020 NXP Semiconductors, All Rights Reserved.
 * Copyright 2021 GOODIX
 */


/** Filename: tfa9896_tfafieldnames.h
 *	This file was generated automatically on 08/15/16 at 09:43:38.
 *	Source file: TFA9896_N1B1_I2C_regmap_V16.xlsx
 */

#ifndef _TFA9896_TFAFIELDNAMES_H
#define _TFA9896_TFAFIELDNAMES_H


#define TFA9896_I2CVERSION	16

enum TFA9896BfEnumList {
	TFA9896_BF_VDDS	= 0x0000,	/*!< Power-on-reset flag (auto clear by reading)		*/
	TFA9896_BF_PLLS	= 0x0010,	/*!< PLL lock to programmed frequency			        */
	TFA9896_BF_OTDS	= 0x0020,	/*!< Over Temperature Protection alarm			       */
	TFA9896_BF_OVDS	= 0x0030,	/*!< Over Voltage Protection alarm			           */
	TFA9896_BF_UVDS	= 0x0040,	/*!< Under Voltage Protection alarm			          */
	TFA9896_BF_OCDS	= 0x0050,	/*!< Over Current Protection alarm			           */
	TFA9896_BF_CLKS	= 0x0060,	/*!< Clocks stable flag			                      */
	TFA9896_BF_CLIPS = 0x0070,	/*!< Amplifier clipping				                    */
	TFA9896_BF_MTPB	= 0x0080,	/*!< MTP busy copying data to/from I2C registers		*/
	TFA9896_BF_NOCLK = 0x0090,	/*!< lost clock detection (reference input clock)		*/
	TFA9896_BF_SPKS	= 0x00a0,	/*!< Speaker error			                           */
	TFA9896_BF_ACS	= 0x00b0,	/*!< Cold Start required			                      */
	TFA9896_BF_SWS	= 0x00c0,	/*!< Amplifier engage (Amp Switching)			         */
	TFA9896_BF_WDS	= 0x00d0,	/*!< watchdog reset (activates reset)			         */
	TFA9896_BF_AMPS	= 0x00e0,	/*!< Amplifier is enabled by manager			         */
	TFA9896_BF_AREFS = 0x00f0,	/*!< References are enabled by manager				     */
	TFA9896_BF_BATS	= 0x0109,	/*!< Battery voltage from ADC readout			        */
	TFA9896_BF_TEMPS = 0x0208,	/*!< Temperature readout from the temperature sensor ( C) */
	TFA9896_BF_REV	= 0x030f,	/*!< Device revision information			              */
	TFA9896_BF_RCV	= 0x0420,	/*!< Enable receiver mode			                     */
	TFA9896_BF_CHS12 = 0x0431,	/*!< Channel Selection TDM input for Coolflux			*/
	TFA9896_BF_INPLVL= 0x0450,	/*!< Input level selection attenuator (				    */
	TFA9896_BF_CHSA	= 0x0461,	/*!< Input selection for amplifier			           */
	TFA9896_BF_AUDFS = 0x04c3,	/*!< Audio sample rate setting				             */
	TFA9896_BF_BSSCR = 0x0501,	/*!< Batteery protection attack time				       */
	TFA9896_BF_BSST	= 0x0523,	/*!< Battery protection threshold level			      */
	TFA9896_BF_BSSRL = 0x0561,	/*!< Battery protection maximum reduction				  */
	TFA9896_BF_BSSRR = 0x0582,	/*!< Battery protection release time				       */
	TFA9896_BF_BSSHY = 0x05b1,	/*!< Battery Protection Hysteresis				         */
	TFA9896_BF_BSSR	= 0x05e0,	/*!< Battery voltage value for read out (only)		  */
	TFA9896_BF_BSSBY = 0x05f0,	/*!< Bypass clipper battery protection				     */
	TFA9896_BF_DPSA	= 0x0600,	/*!< Enable dynamic powerstage activation (DPSA)		*/
	TFA9896_BF_ATTEN = 0x0613,	/*!< Gain attenuation setting				              */
	TFA9896_BF_CFSM	= 0x0650,	/*!< Soft mute in CoolFlux			                   */
	TFA9896_BF_BSSS	= 0x0670,	/*!< Battery sense steepness			                 */
	TFA9896_BF_VOL	= 0x0687,	/*!< Coolflux volume control			                  */
	TFA9896_BF_DCVO2 = 0x0702,	/*!< Second Boost Voltage				                  */
	TFA9896_BF_DCMCC = 0x0733,	/*!< Max boost coil current - step of 175 mA			 */
	TFA9896_BF_DCVO1 = 0x0772,	/*!< First Boost Voltage				                   */
	TFA9896_BF_DCIE	= 0x07a0,	/*!< Adaptive boost mode			                     */
	TFA9896_BF_DCSR	= 0x07b0,	/*!< Soft Rampup/down mode for DCDC controller		  */
	TFA9896_BF_DCPAVG= 0x07c0,	/*!< ctrl_peak2avg for analog part of DCDC				 */
	TFA9896_BF_DCPWM = 0x07d0,	/*!< DCDC PWM only mode				                    */
	TFA9896_BF_TROS	= 0x0800,	/*!< Selection ambient temperature for speaker calibration  */
	TFA9896_BF_EXTTS = 0x0818,	/*!< External temperature for speaker calibration (C)	*/
	TFA9896_BF_PWDN	= 0x0900,	/*!< powerdown selection			                     */
	TFA9896_BF_I2CR	= 0x0910,	/*!< All I2C registers reset to default			      */
	TFA9896_BF_CFE	= 0x0920,	/*!< Enable CoolFlux			                          */
	TFA9896_BF_AMPE	= 0x0930,	/*!< Enable Amplifier			                        */
	TFA9896_BF_DCA	= 0x0940,	/*!< Enable DCDC Boost converter			              */
	TFA9896_BF_SBSL	= 0x0950,	/*!< Coolflux configured			                     */
	TFA9896_BF_AMPC	= 0x0960,	/*!< Selection if Coolflux enables amplifier			 */
	TFA9896_BF_DCDIS = 0x0970,	/*!< DCDC boost converter not connected				    */
	TFA9896_BF_PSDR	= 0x0980,	/*!< IDDQ amplifier test selection			           */
	TFA9896_BF_INTPAD= 0x09c1,	/*!< INT pad (interrupt bump output) configuration		*/
	TFA9896_BF_IPLL	= 0x09e0,	/*!< PLL input reference clock selection			     */
	TFA9896_BF_DCTRIP= 0x0a04,	/*!< Adaptive boost trip levels (effective only when boost_intel is set to 1) */
	TFA9896_BF_DCHOLD= 0x0a54,	/*!< Hold time for DCDC booster (effective only when boost_intel is set to 1) */
	TFA9896_BF_MTPK	= 0x0b07,	/*!< KEY2 to access key2 protected registers (default for engineering) */
	TFA9896_BF_CVFDLY= 0x0c25,	/*!< Fractional delay adjustment between current and voltage sense */
	TFA9896_BF_OPENMTP= 0x0ec0,	/*!< Enable programming of the MTP memory				  */
	TFA9896_BF_TDMPRF= 0x1011,	/*!< TDM usecase selection control				         */
	TFA9896_BF_TDMEN = 0x1030,	/*!< TDM interface enable				                  */
	TFA9896_BF_TDMCKINV= 0x1040,	/*!< TDM clock inversion, receive on				       */
	TFA9896_BF_TDMFSLN= 0x1053,	/*!< TDM FS length				                         */
	TFA9896_BF_TDMFSPOL= 0x1090,	/*!< TDM FS polarity (start frame)				         */
	TFA9896_BF_TDMSAMSZ= 0x10a4,	/*!< TDM sample size for all TDM sinks and sources		*/
	TFA9896_BF_TDMSLOTS= 0x1103,	/*!< TDM number of slots				                   */
	TFA9896_BF_TDMSLLN= 0x1144,	/*!< TDM slot length				                       */
	TFA9896_BF_TDMBRMG= 0x1194,	/*!< TDM bits remaining after the last slot				*/
	TFA9896_BF_TDMDDEL= 0x11e0,	/*!< TDM data delay				                        */
	TFA9896_BF_TDMDADJ= 0x11f0,	/*!< TDM data adjustment				                   */
	TFA9896_BF_TDMTXFRM= 0x1201,	/*!< TDM TXDATA format				                     */
	TFA9896_BF_TDMUUS0= 0x1221,	/*!< TDM TXDATA format unused slot SD0				     */
	TFA9896_BF_TDMUUS1= 0x1241,	/*!< TDM TXDATA format unused slot SD1				     */
	TFA9896_BF_TDMSI0EN= 0x1270,	/*!< TDM sink0 enable				                      */
	TFA9896_BF_TDMSI1EN= 0x1280,	/*!< TDM sink1 enable				                      */
	TFA9896_BF_TDMSI2EN= 0x1290,	/*!< TDM sink2 enable				                      */
	TFA9896_BF_TDMSO0EN= 0x12a0,	/*!< TDM source0 enable				                    */
	TFA9896_BF_TDMSO1EN= 0x12b0,	/*!< TDM source1 enable				                    */
	TFA9896_BF_TDMSO2EN= 0x12c0,	/*!< TDM source2 enable				                    */
	TFA9896_BF_TDMSI0IO= 0x12d0,	/*!< TDM sink0 IO selection				                */
	TFA9896_BF_TDMSI1IO= 0x12e0,	/*!< TDM sink1 IO selection				                */
	TFA9896_BF_TDMSI2IO= 0x12f0,	/*!< TDM sink2 IO selection				                */
	TFA9896_BF_TDMSO0IO= 0x1300,	/*!< TDM source0 IO selection				              */
	TFA9896_BF_TDMSO1IO= 0x1310,	/*!< TDM source1 IO selection				              */
	TFA9896_BF_TDMSO2IO= 0x1320,	/*!< TDM source2 IO selection				              */
	TFA9896_BF_TDMSI0SL= 0x1333,	/*!< TDM sink0 slot position [GAIN IN]				     */
	TFA9896_BF_TDMSI1SL= 0x1373,	/*!< TDM sink1 slot position [CH1 IN]				      */
	TFA9896_BF_TDMSI2SL= 0x13b3,	/*!< TDM sink2 slot position [CH2 IN]				      */
	TFA9896_BF_TDMSO0SL= 0x1403,	/*!< TDM source0 slot position [GAIN OUT]				  */
	TFA9896_BF_TDMSO1SL= 0x1443,	/*!< TDM source1 slot position [Voltage Sense]			*/
	TFA9896_BF_TDMSO2SL= 0x1483,	/*!< TDM source2 slot position [Current Sense]			*/
	TFA9896_BF_NBCK	= 0x14c3,	/*!< TDM NBCK bit clock ratio			                */
	TFA9896_BF_INTOVDDS= 0x2000,	/*!< flag_por_int_out				                      */
	TFA9896_BF_INTOPLLS= 0x2010,	/*!< flag_pll_lock_int_out				                 */
	TFA9896_BF_INTOOTDS= 0x2020,	/*!< flag_otpok_int_out				                    */
	TFA9896_BF_INTOOVDS= 0x2030,	/*!< flag_ovpok_int_out				                    */
	TFA9896_BF_INTOUVDS= 0x2040,	/*!< flag_uvpok_int_out				                    */
	TFA9896_BF_INTOOCDS= 0x2050,	/*!< flag_ocp_alarm_int_out				                */
	TFA9896_BF_INTOCLKS= 0x2060,	/*!< flag_clocks_stable_int_out				            */
	TFA9896_BF_INTOCLIPS= 0x2070,	/*!< flag_clip_int_out				                     */
	TFA9896_BF_INTOMTPB= 0x2080,	/*!< mtp_busy_int_out				                      */
	TFA9896_BF_INTONOCLK= 0x2090,	/*!< flag_lost_clk_int_out				                 */
	TFA9896_BF_INTOSPKS= 0x20a0,	/*!< flag_cf_speakererror_int_out				          */
	TFA9896_BF_INTOACS= 0x20b0,	/*!< flag_cold_started_int_out				             */
	TFA9896_BF_INTOSWS= 0x20c0,	/*!< flag_engage_int_out				                   */
	TFA9896_BF_INTOWDS= 0x20d0,	/*!< flag_watchdog_reset_int_out				           */
	TFA9896_BF_INTOAMPS= 0x20e0,	/*!< flag_enbl_amp_int_out				                 */
	TFA9896_BF_INTOAREFS= 0x20f0,	/*!< flag_enbl_ref_int_out				                 */
	TFA9896_BF_INTOERR= 0x2200,	/*!< flag_cfma_err_int_out				                 */
	TFA9896_BF_INTOACK= 0x2210,	/*!< flag_cfma_ack_int_out				                 */
	TFA9896_BF_INTIVDDS= 0x2300,	/*!< flag_por_int_in				                       */
	TFA9896_BF_INTIPLLS= 0x2310,	/*!< flag_pll_lock_int_in				                  */
	TFA9896_BF_INTIOTDS= 0x2320,	/*!< flag_otpok_int_in				                     */
	TFA9896_BF_INTIOVDS= 0x2330,	/*!< flag_ovpok_int_in				                     */
	TFA9896_BF_INTIUVDS= 0x2340,	/*!< flag_uvpok_int_in				                     */
	TFA9896_BF_INTIOCDS= 0x2350,	/*!< flag_ocp_alarm_int_in				                 */
	TFA9896_BF_INTICLKS= 0x2360,	/*!< flag_clocks_stable_int_in				             */
	TFA9896_BF_INTICLIPS= 0x2370,	/*!< flag_clip_int_in				                      */
	TFA9896_BF_INTIMTPB= 0x2380,	/*!< mtp_busy_int_in				                       */
	TFA9896_BF_INTINOCLK= 0x2390,	/*!< flag_lost_clk_int_in				                  */
	TFA9896_BF_INTISPKS= 0x23a0,	/*!< flag_cf_speakererror_int_in				           */
	TFA9896_BF_INTIACS= 0x23b0,	/*!< flag_cold_started_int_in				              */
	TFA9896_BF_INTISWS= 0x23c0,	/*!< flag_engage_int_in				                    */
	TFA9896_BF_INTIWDS= 0x23d0,	/*!< flag_watchdog_reset_int_in				            */
	TFA9896_BF_INTIAMPS= 0x23e0,	/*!< flag_enbl_amp_int_in				                  */
	TFA9896_BF_INTIAREFS= 0x23f0,	/*!< flag_enbl_ref_int_in				                  */
	TFA9896_BF_INTIERR= 0x2500,	/*!< flag_cfma_err_int_in				                  */
	TFA9896_BF_INTIACK= 0x2510,	/*!< flag_cfma_ack_int_in				                  */
	TFA9896_BF_INTENVDDS= 0x2600,	/*!< flag_por_int_enable				                   */
	TFA9896_BF_INTENPLLS= 0x2610,	/*!< flag_pll_lock_int_enable				              */
	TFA9896_BF_INTENOTDS= 0x2620,	/*!< flag_otpok_int_enable				                 */
	TFA9896_BF_INTENOVDS= 0x2630,	/*!< flag_ovpok_int_enable				                 */
	TFA9896_BF_INTENUVDS= 0x2640,	/*!< flag_uvpok_int_enable				                 */
	TFA9896_BF_INTENOCDS= 0x2650,	/*!< flag_ocp_alarm_int_enable				             */
	TFA9896_BF_INTENCLKS= 0x2660,	/*!< flag_clocks_stable_int_enable				         */
	TFA9896_BF_INTENCLIPS= 0x2670,	/*!< flag_clip_int_enable				                  */
	TFA9896_BF_INTENMTPB= 0x2680,	/*!< mtp_busy_int_enable				                   */
	TFA9896_BF_INTENNOCLK= 0x2690,	/*!< flag_lost_clk_int_enable				              */
	TFA9896_BF_INTENSPKS= 0x26a0,	/*!< flag_cf_speakererror_int_enable				       */
	TFA9896_BF_INTENACS= 0x26b0,	/*!< flag_cold_started_int_enable				          */
	TFA9896_BF_INTENSWS= 0x26c0,	/*!< flag_engage_int_enable				                */
	TFA9896_BF_INTENWDS= 0x26d0,	/*!< flag_watchdog_reset_int_enable				        */
	TFA9896_BF_INTENAMPS= 0x26e0,	/*!< flag_enbl_amp_int_enable				              */
	TFA9896_BF_INTENAREFS= 0x26f0,	/*!< flag_enbl_ref_int_enable				              */
	TFA9896_BF_INTENERR= 0x2800,	/*!< flag_cfma_err_int_enable				              */
	TFA9896_BF_INTENACK= 0x2810,	/*!< flag_cfma_ack_int_enable				              */
	TFA9896_BF_INTPOLVDDS= 0x2900,	/*!< flag_por_int_pol				                      */
	TFA9896_BF_INTPOLPLLS= 0x2910,	/*!< flag_pll_lock_int_pol				                 */
	TFA9896_BF_INTPOLOTDS= 0x2920,	/*!< flag_otpok_int_pol				                    */
	TFA9896_BF_INTPOLOVDS= 0x2930,	/*!< flag_ovpok_int_pol				                    */
	TFA9896_BF_INTPOLUVDS= 0x2940,	/*!< flag_uvpok_int_pol				                    */
	TFA9896_BF_INTPOLOCDS= 0x2950,	/*!< flag_ocp_alarm_int_pol				                */
	TFA9896_BF_INTPOLCLKS= 0x2960,	/*!< flag_clocks_stable_int_pol				            */
	TFA9896_BF_INTPOLCLIPS= 0x2970,	/*!< flag_clip_int_pol				                     */
	TFA9896_BF_INTPOLMTPB= 0x2980,	/*!< mtp_busy_int_pol				                      */
	TFA9896_BF_INTPOLNOCLK= 0x2990,	/*!< flag_lost_clk_int_pol				                 */
	TFA9896_BF_INTPOLSPKS= 0x29a0,	/*!< flag_cf_speakererror_int_pol				          */
	TFA9896_BF_INTPOLACS= 0x29b0,	/*!< flag_cold_started_int_pol				             */
	TFA9896_BF_INTPOLSWS= 0x29c0,	/*!< flag_engage_int_pol				                   */
	TFA9896_BF_INTPOLWDS= 0x29d0,	/*!< flag_watchdog_reset_int_pol				           */
	TFA9896_BF_INTPOLAMPS= 0x29e0,	/*!< flag_enbl_amp_int_pol				                 */
	TFA9896_BF_INTPOLAREFS= 0x29f0,	/*!< flag_enbl_ref_int_pol				                 */
	TFA9896_BF_INTPOLERR= 0x2b00,	/*!< flag_cfma_err_int_pol				                 */
	TFA9896_BF_INTPOLACK= 0x2b10,	/*!< flag_cfma_ack_int_pol				                 */
	TFA9896_BF_CLIP	= 0x4900,	/*!< Bypass clip control			                     */
	TFA9896_BF_CIMTP = 0x62b0,	/*!< Start copying data from I2C mtp registers to mtp	*/
	TFA9896_BF_RST	= 0x7000,	/*!< Reset CoolFlux DSP			                       */
	TFA9896_BF_DMEM	= 0x7011,	/*!< Target memory for access			                */
	TFA9896_BF_AIF	= 0x7030,	/*!< Auto increment flag for memory-address			   */
	TFA9896_BF_CFINT = 0x7040,	/*!< CF Interrupt - auto clear				             */
	TFA9896_BF_REQ	= 0x7087,	/*!< CF request for accessing the 8 channels			  */
	TFA9896_BF_MADD	= 0x710f,	/*!< Memory address			                          */
	TFA9896_BF_MEMA	= 0x720f,	/*!< Activate memory access			                  */
	TFA9896_BF_ERR	= 0x7307,	/*!< CF error flags			                           */
	TFA9896_BF_ACK	= 0x7387,	/*!< CF acknowledgment of the requests channels		*/
	TFA9896_BF_MTPOTC= 0x8000,	/*!< Calibration schedule selection				        */
	TFA9896_BF_MTPEX = 0x8010,	/*!< Calibration of RON status bit				         */
};

enum TFA9896_irq {
	TFA9896_irq_vdds = 0,
	TFA9896_irq_plls = 1,
	TFA9896_irq_ds = 2,
	TFA9896_irq_vds = 3,
	TFA9896_irq_uvds = 4,
	TFA9896_irq_cds = 5,
	TFA9896_irq_clks = 6,
	TFA9896_irq_clips = 7,
	TFA9896_irq_mtpb = 8,
	TFA9896_irq_clk = 9,
	TFA9896_irq_spks = 10,
	TFA9896_irq_acs = 11,
	TFA9896_irq_sws = 12,
	TFA9896_irq_wds = 13,
	TFA9896_irq_amps = 14,
	TFA9896_irq_arefs = 15,
	TFA9896_irq_err = 32,
	TFA9896_irq_ack = 33,
	TFA9896_irq_max = 34,
	TFA9896_irq_all = -1 /* all irqs */};

#endif
