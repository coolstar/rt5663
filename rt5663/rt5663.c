#define DESCRIPTOR_DEF
#include "rt5663.h"
#include "registers.h"
#include "rl6231.h"

#define RT5663_DEVICE_ID_2 0x6451
#define RT5663_DEVICE_ID_1 0x6406

#define RT5663_POWER_ON_DELAY_MS 300

enum {
	CODEC_VER_1,
	CODEC_VER_0,
};

#define bool int
#define MHz 1000000

static ULONG Rt5663DebugLevel = 100;
static ULONG Rt5663DebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

void rt5663_jackdetect(PRTEK_CONTEXT pDevice);

NTSTATUS
DriverEntry(
	__in PDRIVER_OBJECT  DriverObject,
	__in PUNICODE_STRING RegistryPath
)
{
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES  attributes;

	RtekPrint(DEBUG_LEVEL_INFO, DBG_INIT,
		"Driver Entry\n");

	WDF_DRIVER_CONFIG_INIT(&config, Rt5663EvtDeviceAdd);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	//
	// Create a framework driver object to represent our driver.
	//

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
	);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
			"WdfDriverCreate failed with status 0x%x\n", status);
	}

	return status;
}


static NTSTATUS rt5663_reg_write(PRTEK_CONTEXT pDevice, uint16_t reg, uint16_t data)
{
	uint16_t rawdata[2];
	rawdata[0] = RtlUshortByteSwap(reg);
	rawdata[1] = RtlUshortByteSwap(data);
	return SpbWriteDataSynchronously(&pDevice->I2CContext, rawdata, sizeof(rawdata));
}

static NTSTATUS rt5663_reg_read(PRTEK_CONTEXT pDevice, uint16_t reg, uint16_t* data)
{
	uint16_t reg_swap = RtlUshortByteSwap(reg);
	uint16_t data_swap = 0;
	NTSTATUS ret = SpbXferDataSynchronously(&pDevice->I2CContext, &reg_swap, sizeof(uint16_t), &data_swap, sizeof(uint16_t));
	*data = RtlUshortByteSwap(data_swap);
	return ret;
}

static NTSTATUS rt5663_reg_update(
	_In_ PRTEK_CONTEXT pDevice,
	uint16_t reg,
	uint16_t mask,
	uint16_t val
) {
	uint16_t tmp = 0, orig = 0;

	NTSTATUS status = rt5663_reg_read(pDevice, reg, &orig);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	tmp = orig & ~mask;
	tmp |= val & mask;

	if (tmp != orig) {
		status = rt5663_reg_write(pDevice, reg, tmp);
	}
	return status;
}

#define rt5663_widget_update rt5663_reg_update

static NTSTATUS rt5663_reg_burstWrite(PRTEK_CONTEXT pDevice, struct reg* regs, int regCount) {
	NTSTATUS status = STATUS_NO_MEMORY;
	for (int i = 0; i < regCount; i++) {
		struct reg* regToSet = &regs[i];
		status = rt5663_reg_write(pDevice, regToSet->reg, regToSet->val);
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}
	return status;
}

static void msleep(unsigned int msec) {
	LARGE_INTEGER delay;
	delay.QuadPart = -10 * 1000 * msec;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);
}

static void rt5663_set_bias_level(PRTEK_CONTEXT pDevice,
	enum snd_bias_level level){
	switch (level) {
	case SND_BIAS_ON:
		rt5663_reg_update(pDevice, RT5663_PWR_ANLG_1,
			RT5663_PWR_FV1_MASK | RT5663_PWR_FV2_MASK,
			RT5663_PWR_FV1 | RT5663_PWR_FV2);
		break;

	case SND_BIAS_PREPARE:
		break;

	case SND_BIAS_STANDBY:
		rt5663_reg_update(pDevice, RT5663_PWR_ANLG_1,
			RT5663_PWR_VREF1_MASK | RT5663_PWR_VREF2_MASK |
			RT5663_PWR_FV1_MASK | RT5663_PWR_FV2_MASK |
			RT5663_PWR_MB_MASK, RT5663_PWR_VREF1 |
			RT5663_PWR_VREF2 | RT5663_PWR_MB);
		msleep(10);
		break;

	case SND_BIAS_OFF:
		if (pDevice->CodecVer != SND_JACK_HEADSET)
			rt5663_reg_update(pDevice,
				RT5663_PWR_ANLG_1,
				RT5663_PWR_VREF1_MASK | RT5663_PWR_VREF2_MASK |
				RT5663_PWR_FV1 | RT5663_PWR_FV2 |
				RT5663_PWR_MB_MASK, 0);
		else
			rt5663_reg_update(pDevice,
				RT5663_PWR_ANLG_1,
				RT5663_PWR_FV1_MASK | RT5663_PWR_FV2_MASK,
				RT5663_PWR_FV1 | RT5663_PWR_FV2);
		break;

	default:
		break;
	}
}

static void rt5663_hp_event(PRTEK_CONTEXT pDevice, int event)
{
	switch (event) {
	case SND_DAPM_POST_PMU:
		if (pDevice->CodecVer == CODEC_VER_0) {
			rt5663_reg_update(pDevice,
				RT5663_DACREF_LDO, 0x3e0e, 0x3a0a);
			rt5663_reg_write(pDevice, RT5663_DEPOP_2, 0x3003);
			rt5663_reg_update(pDevice, RT5663_HP_CHARGE_PUMP_1,
				RT5663_OVCD_HP_MASK, RT5663_OVCD_HP_DIS);
			rt5663_reg_write(pDevice, RT5663_HP_CHARGE_PUMP_2, 0x1371);
			rt5663_reg_write(pDevice, RT5663_HP_BIAS, 0xabba);
			rt5663_reg_write(pDevice, RT5663_CHARGE_PUMP_1, 0x2224);
			rt5663_reg_write(pDevice, RT5663_ANA_BIAS_CUR_1, 0x7766);
			rt5663_reg_write(pDevice, RT5663_HP_BIAS, 0xafaa);
			rt5663_reg_write(pDevice, RT5663_CHARGE_PUMP_2, 0x7777);
			rt5663_reg_update(pDevice, RT5663_STO_DRE_1, 0x8000,
				0x8000);
			rt5663_reg_update(pDevice, RT5663_DEPOP_1, 0x3000,
				0x3000);
			rt5663_reg_update(pDevice,
				RT5663_DIG_VOL_ZCD, 0x00c0, 0x0080);
		}
		break;

	case SND_DAPM_PRE_PMD:
		if (pDevice->CodecVer == CODEC_VER_0) {
			rt5663_reg_update(pDevice, RT5663_DEPOP_1, 0x3000, 0x0);
			rt5663_reg_update(pDevice, RT5663_HP_CHARGE_PUMP_1,
				RT5663_OVCD_HP_MASK, RT5663_OVCD_HP_EN);
			rt5663_reg_update(pDevice,
				RT5663_DACREF_LDO, 0x3e0e, 0);
			rt5663_reg_update(pDevice,
				RT5663_DIG_VOL_ZCD, 0x00c0, 0);
		}
		break;

	default:
		break;
	}
}

static void rt5663_charge_pump_event(PRTEK_CONTEXT pDevice, int event) {
	switch (event){
	case SND_DAPM_PRE_PMU:
		if (pDevice->CodecVer == CODEC_VER_0) {
			rt5663_reg_update(pDevice, RT5663_DEPOP_1, 0x0030,
				0x0030);
			rt5663_reg_update(pDevice, RT5663_DEPOP_1, 0x0003,
				0x0003);
		}
		break;
	case SND_DAPM_POST_PMD:
		if (pDevice->CodecVer == CODEC_VER_0) {
			rt5663_reg_update(pDevice, RT5663_DEPOP_1, 0x0003, 0);
			rt5663_reg_update(pDevice, RT5663_DEPOP_1, 0x0030, 0);
		}
		break;
	default:
		break;
	}
}

static void rt5663_pre_div_power(PRTEK_CONTEXT pDevice, int event)
{
	switch (event) {
	case SND_DAPM_POST_PMU:
		rt5663_reg_write(pDevice, RT5663_PRE_DIV_GATING_1, 0xff00);
		rt5663_reg_write(pDevice, RT5663_PRE_DIV_GATING_2, 0xfffc);
		break;

	case SND_DAPM_PRE_PMD:
		rt5663_reg_write(pDevice, RT5663_PRE_DIV_GATING_1, 0x0000);
		rt5663_reg_write(pDevice, RT5663_PRE_DIV_GATING_2, 0x0000);
		break;
	default:
		break;
	}
}

void rt5663_enable(PRTEK_CONTEXT pDevice) {
	{ //hw params
		int pre_div = rl6231_get_clk_info(24576000, 48000);

		unsigned int val_len = RT5663_I2S_DL_16;
		rt5663_widget_update(pDevice, RT5663_I2S1_SDP, RT5663_I2S_DL_MASK, val_len);

		rt5663_widget_update(pDevice, RT5663_ADDA_CLK_1,
			RT5663_I2S_PD1_MASK, pre_div << RT5663_I2S_PD1_SHIFT);
	}

	rt5663_set_bias_level(pDevice, SND_BIAS_STANDBY);
	rt5663_set_bias_level(pDevice, SND_BIAS_PREPARE);

	rt5663_charge_pump_event(pDevice, SND_DAPM_PRE_PMU);

	rt5663_pre_div_power(pDevice, SND_DAPM_POST_PMU);

	{
		//set round 3

		//DAC Mixer
		rt5663_widget_update(pDevice, RT5663_PWR_DIG_1,
			RT5663_PWR_DAC_L1 | RT5663_PWR_DAC_R1 | RT5663_PWR_I2S1,
			RT5663_PWR_DAC_L1 | RT5663_PWR_DAC_R1 | RT5663_PWR_I2S1);
		rt5663_widget_update(pDevice, RT5663_PWR_DIG_2,
			RT5663_PWR_DAC_S1F,
			RT5663_PWR_DAC_S1F);

		//ASRC
		rt5663_widget_update(pDevice, RT5663_ASRC_1,
			RT5663_I2S1_ASRC_MASK | RT5663_DAC_STO1_ASRC_MASK,
			RT5663_I2S1_ASRC_MASK | RT5663_DAC_STO1_ASRC_MASK);
	}

	rt5663_hp_event(pDevice, SND_DAPM_POST_PMU);

	rt5663_set_bias_level(pDevice, SND_BIAS_ON);

	{ //defaults (v1)
		//set round 4

		//DAC Mixer
		rt5663_widget_update(pDevice, RT5663_PWR_DIG_1,
			RT5663_PWR_ADC_L1,
			RT5663_PWR_ADC_L1);
		rt5663_widget_update(pDevice, RT5663_PWR_DIG_2,
			RT5663_PWR_ADC_S1F,
			RT5663_PWR_ADC_S1F);

		rt5663_widget_update(pDevice, RT5663_PWR_ANLG_2,
			RT5663_PWR_RECMIX1,
			RT5663_PWR_RECMIX1);

		//ASRC
		rt5663_widget_update(pDevice, RT5663_ASRC_1,
			RT5663_ADC_STO1_ASRC_MASK,
			RT5663_ADC_STO1_ASRC_MASK);

		//ADC Mixer
		rt5663_widget_update(pDevice,
			RT5663_CHOP_ADC,
			RT5663_CKGEN_ADCC_MASK,
			RT5663_CKGEN_ADCC_MASK);

		//Headphone Volume
		rt5663_widget_update(pDevice,
			RT5663_STO_DRE_9,
			RT5663_DRE_GAIN_HP_MASK,
			0x07);
		rt5663_widget_update(pDevice,
			RT5663_STO_DRE_10,
			RT5663_DRE_GAIN_HP_MASK,
			0x07);

		rt5663_widget_update(pDevice, RT5663_PWR_ANLG_1,
			RT5663_PWR_VREF1 | RT5663_PWR_VREF2 | RT5663_PWR_MB,
			RT5663_PWR_VREF1 | RT5663_PWR_VREF2 | RT5663_PWR_MB);
	}
}

void rt5663_disable(PRTEK_CONTEXT pDevice) {
	{ //Unset defaults
		//DAC Mixer
		rt5663_widget_update(pDevice, RT5663_PWR_DIG_1,
			RT5663_PWR_ADC_L1,
			0);
		rt5663_widget_update(pDevice, RT5663_PWR_DIG_2,
			RT5663_PWR_ADC_S1F,
			0);

		rt5663_widget_update(pDevice, RT5663_PWR_ANLG_2,
			RT5663_PWR_RECMIX1,
			0);

		//ASRC
		rt5663_widget_update(pDevice, RT5663_ASRC_1,
			RT5663_ADC_STO1_ASRC_MASK,
			0);

		//ADC Mixer
		rt5663_widget_update(pDevice,
			RT5663_CHOP_ADC,
			RT5663_CKGEN_ADCC_MASK,
			0);
	}

	rt5663_set_bias_level(pDevice, SND_BIAS_PREPARE);

	rt5663_hp_event(pDevice, SND_DAPM_PRE_PMD);

	rt5663_pre_div_power(pDevice, SND_DAPM_PRE_PMD);

	rt5663_charge_pump_event(pDevice, SND_DAPM_POST_PMD);

	{ //unset last defaults
		rt5663_widget_update(pDevice, RT5663_PWR_DIG_1,
			RT5663_PWR_I2S1 | RT5663_PWR_DAC_L1 | RT5663_PWR_DAC_R1,
			0);
		rt5663_widget_update(pDevice, RT5663_PWR_DIG_2,
			RT5663_PWR_DAC_S1F,
			0);

		//ASRC
		rt5663_widget_update(pDevice, RT5663_ASRC_1,
			RT5663_I2S1_ASRC_MASK | RT5663_DAC_STO1_ASRC_MASK,
			0);
	}

	rt5663_set_bias_level(pDevice, SND_BIAS_STANDBY);

	rt5663_set_bias_level(pDevice, SND_BIAS_OFF);
}

NTSTATUS BOOTCODEC(
	_In_  PRTEK_CONTEXT  devContext
)
{
	NTSTATUS status = STATUS_SUCCESS;

	UINT16 val;
	status = rt5663_reg_read(devContext, RT5663_VENDOR_ID_2, &val);
	if (!NT_SUCCESS(status) || (val != RT5663_DEVICE_ID_2 && val != RT5663_DEVICE_ID_1)) {
		msleep(100);
		rt5663_reg_read(devContext, RT5663_VENDOR_ID_2, &val);
	}

	switch (val) {
	case RT5663_DEVICE_ID_2:
		DbgPrint("Codec version 1 is not supported\n");
		return STATUS_INVALID_DEVICE_STATE;
	case RT5663_DEVICE_ID_1:
		devContext->CodecVer = CODEC_VER_0;
		break;
	default:
		DbgPrint("Device with ID register %#x is not rt5663\n",
			val);
		return STATUS_INVALID_DEVICE_STATE;
	}

	/* reset */

	rt5663_reg_write(devContext, RT5663_RESET, 0);

	static const struct reg rt5663_patch_list[] = {
		{ 0x002a, 0x8020 },
		{ 0x0086, 0x0028 },
		{ 0x0100, 0xa020 },
		{ 0x0117, 0x0f28 },
		{ 0x02fb, 0x8089 },
	};
	if (devContext->CodecVer == CODEC_VER_0) {
		status = rt5663_reg_burstWrite(devContext, rt5663_patch_list, ARRAYSIZE(rt5663_patch_list));
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}

	/* GPIO1 as IRQ */
	rt5663_reg_update(devContext, RT5663_GPIO_1, RT5663_GP1_PIN_MASK,
		RT5663_GP1_PIN_IRQ);
	/* 4btn inline command debounce */
	rt5663_reg_update(devContext, RT5663_IL_CMD_5,
		RT5663_4BTN_CLK_DEB_MASK, RT5663_4BTN_CLK_DEB_65MS);

	switch (devContext->CodecVer) {
	case CODEC_VER_0:
		rt5663_reg_update(devContext, RT5663_DIG_MISC,
			RT5663_DIG_GATE_CTRL_MASK, RT5663_DIG_GATE_CTRL_EN);
		rt5663_reg_update(devContext, RT5663_AUTO_1MRC_CLK,
			RT5663_IRQ_MANUAL_MASK, RT5663_IRQ_MANUAL_EN);
		rt5663_reg_update(devContext, RT5663_IRQ_1,
			RT5663_EN_IRQ_JD1_MASK, RT5663_EN_IRQ_JD1_EN);
		rt5663_reg_update(devContext, RT5663_GPIO_1,
			RT5663_GPIO1_TYPE_MASK, RT5663_GPIO1_TYPE_EN);
		rt5663_reg_write(devContext, RT5663_VREF_RECMIX, 0x0032);
		rt5663_reg_update(devContext, RT5663_GPIO_2,
			RT5663_GP1_PIN_CONF_MASK | RT5663_SEL_GPIO1_MASK,
			RT5663_GP1_PIN_CONF_OUTPUT | RT5663_SEL_GPIO1_EN);
		rt5663_reg_update(devContext, RT5663_RECMIX,
			RT5663_RECMIX1_BST1_MASK, RT5663_RECMIX1_BST1_ON);
		rt5663_reg_update(devContext, RT5663_TDM_2,
			RT5663_DATA_SWAP_ADCDAT1_MASK,
			RT5663_DATA_SWAP_ADCDAT1_LL);
		break;
	}

	{ //i2s use asrc
		unsigned int asrc2_mask = 0;
		unsigned int asrc2_value = 0;
		unsigned int clk_src = RT5663_CLK_SEL_I2S1_ASRC;
		unsigned int filter_mask = RT5663_DA_STEREO_FILTER | RT5663_AD_STEREO_FILTER;

		if (filter_mask & RT5663_DA_STEREO_FILTER) {
			asrc2_mask |= RT5663_DA_STO1_TRACK_MASK;
			asrc2_value |= clk_src << RT5663_DA_STO1_TRACK_SHIFT;
		}

		if (filter_mask & RT5663_AD_STEREO_FILTER) {
			switch (devContext->CodecVer) {
			case CODEC_VER_0:
				asrc2_mask |= RT5663_AD_STO1_TRACK_MASK;
				asrc2_value |= clk_src << RT5663_AD_STO1_TRACK_SHIFT;
				break;
			}
		}

		if (asrc2_mask)
			rt5663_reg_update(devContext, RT5663_ASRC_2, asrc2_mask,
				asrc2_value);
	}

	{ //set sysclk
		int clk_id = RT5663_SCLK_S_MCLK;
		unsigned int freq = 24576000;

		unsigned int reg_val = 0;
		switch (clk_id) {
		case RT5663_SCLK_S_MCLK:
			reg_val |= RT5663_SCLK_SRC_MCLK;
			break;
		case RT5663_SCLK_S_PLL1:
			reg_val |= RT5663_SCLK_SRC_PLL1;
			break;
		case RT5663_SCLK_S_RCCLK:
			reg_val |= RT5663_SCLK_SRC_RCCLK;
			break;
		default:
			return STATUS_INVALID_DEVICE_REQUEST;
		}
		rt5663_reg_update(devContext, RT5663_GLB_CLK, RT5663_SCLK_SRC_MASK,
			reg_val);
	}

	{ //dai fmt
		unsigned int reg_val = RT5663_I2S_MS_S;

		UINT16 val;
		rt5663_reg_read(devContext, RT5663_I2S1_SDP, &val);

		rt5663_reg_update(devContext, RT5663_I2S1_SDP, RT5663_I2S_MS_MASK |
			RT5663_I2S_BP_MASK | RT5663_I2S_DF_MASK, reg_val);

		rt5663_reg_read(devContext, RT5663_I2S1_SDP, &val);
	}

	{
		//set round 1
		//Headphone Volume
		rt5663_widget_update(devContext,
			RT5663_STO_DRE_9,
			RT5663_DRE_GAIN_HP_MASK,
			0x00);
		rt5663_widget_update(devContext,
			RT5663_STO_DRE_10,
			RT5663_DRE_GAIN_HP_MASK,
			0x00);

		//Mic Volume
		rt5663_widget_update(devContext,
			RT5663_STO1_ADC_DIG_VOL,
			RT5663_ADC_L_MUTE_MASK | RT5663_ADC_L_VOL_MASK | RT5663_ADC_R_MUTE_MASK | RT5663_ADC_R_VOL_MASK,
			(0x53 << RT5663_ADC_L_VOL_SHIFT) | (0x53 << RT5663_ADC_R_VOL_SHIFT));

		//ADC Mixer
		rt5663_widget_update(devContext,
			RT5663_STO1_ADC_MIXER,
			RT5663_M_STO1_ADC_L1,
			0);

		//Headphone Volume
		rt5663_widget_update(devContext,
			RT5663_STO_DRE_9,
			RT5663_DRE_GAIN_HP_MASK,
			0x07);
		rt5663_widget_update(devContext,
			RT5663_STO_DRE_10,
			RT5663_DRE_GAIN_HP_MASK,
			0x07);
	}

	rt5663_enable(devContext); //must power cycle once before enabling it or output doesn't work

	rt5663_disable(devContext);

	rt5663_enable(devContext);

	rt5663_jackdetect(devContext); //attempt to check if microphone present

	return STATUS_SUCCESS;
}

NTSTATUS
OnPrepareHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesRaw,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PRTEK_CONTEXT pDevice = GetDeviceContext(FxDevice);
	BOOLEAN fSpbResourceFound = FALSE;
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

	UNREFERENCED_PARAMETER(FxResourcesRaw);

	//
	// Parse the peripheral's resources.
	//

	ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

	for (ULONG i = 0; i < resourceCount; i++)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;
		UCHAR Class;
		UCHAR Type;

		pDescriptor = WdfCmResourceListGetDescriptor(
			FxResourcesTranslated, i);

		switch (pDescriptor->Type)
		{
		case CmResourceTypeConnection:
			//
			// Look for I2C or SPI resource and save connection ID.
			//
			Class = pDescriptor->u.Connection.Class;
			Type = pDescriptor->u.Connection.Type;
			if (Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
				Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
			{
				if (fSpbResourceFound == FALSE)
				{
					status = STATUS_SUCCESS;
					pDevice->I2CContext.I2cResHubId.LowPart = pDescriptor->u.Connection.IdLowPart;
					pDevice->I2CContext.I2cResHubId.HighPart = pDescriptor->u.Connection.IdHighPart;
					fSpbResourceFound = TRUE;
				}
				else
				{
				}
			}
			break;
		default:
			//
			// Ignoring all other resource types.
			//
			break;
		}
	}

	//
	// An SPB resource is required.
	//

	if (fSpbResourceFound == FALSE)
	{
		status = STATUS_NOT_FOUND;
	}

	status = SpbTargetInitialize(FxDevice, &pDevice->I2CContext);

	if (!NT_SUCCESS(status))
	{
		return status;
	}

	return status;
}

NTSTATUS
OnReleaseHardware(
	_In_  WDFDEVICE     FxDevice,
	_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PRTEK_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	SpbTargetDeinitialize(FxDevice, &pDevice->I2CContext);

	return status;
}

NTSTATUS
OnD0Entry(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PRTEK_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	pDevice->JackType = 0;

	status = BOOTCODEC(pDevice);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	pDevice->ConnectInterrupt = true;

	return status;
}

NTSTATUS
OnD0Exit(
	_In_  WDFDEVICE               FxDevice,
	_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PRTEK_CONTEXT pDevice = GetDeviceContext(FxDevice);

	rt5663_disable(pDevice);
	rt5663_reg_write(pDevice, RT5663_RESET, 0);

	pDevice->ConnectInterrupt = false;

	return STATUS_SUCCESS;
}

static void rt5663_enable_push_button_irq(PRTEK_CONTEXT pDevice,
	bool enable)
{

	if (enable) {
		rt5663_reg_update(pDevice, RT5663_IL_CMD_6,
			RT5663_EN_4BTN_INL_MASK, RT5663_EN_4BTN_INL_EN);
		/* reset in-line command */
		rt5663_reg_update(pDevice, RT5663_IL_CMD_6,
			RT5663_RESET_4BTN_INL_MASK,
			RT5663_RESET_4BTN_INL_RESET);
		rt5663_reg_update(pDevice, RT5663_IL_CMD_6,
			RT5663_RESET_4BTN_INL_MASK,
			RT5663_RESET_4BTN_INL_NOR);
		switch (pDevice->CodecVer) {
		case CODEC_VER_0:
			rt5663_reg_update(pDevice, RT5663_IRQ_2,
				RT5663_EN_IRQ_INLINE_MASK,
				RT5663_EN_IRQ_INLINE_NOR);
			break;
		}
	}
	else {
		switch (pDevice->CodecVer) {
		case CODEC_VER_0:
			rt5663_reg_update(pDevice, RT5663_IRQ_2,
				RT5663_EN_IRQ_INLINE_MASK,
				RT5663_EN_IRQ_INLINE_BYP);
			break;
		}
		rt5663_reg_update(pDevice, RT5663_IL_CMD_6,
			RT5663_EN_4BTN_INL_MASK, RT5663_EN_4BTN_INL_DIS);
		/* reset in-line command */
		rt5663_reg_update(pDevice, RT5663_IL_CMD_6,
			RT5663_RESET_4BTN_INL_MASK,
			RT5663_RESET_4BTN_INL_RESET);
		rt5663_reg_update(pDevice, RT5663_IL_CMD_6,
			RT5663_RESET_4BTN_INL_MASK,
			RT5663_RESET_4BTN_INL_NOR);
	}
}

int rt5663_jack_detect(PRTEK_CONTEXT pDevice, int jack_insert) {
	UINT16 val, i = 0;
	if (jack_insert) {
		rt5663_reg_update(pDevice, RT5663_DIG_MISC,
			RT5663_DIG_GATE_CTRL_MASK, RT5663_DIG_GATE_CTRL_EN);
		rt5663_reg_update(pDevice, RT5663_HP_CHARGE_PUMP_1,
			RT5663_SI_HP_MASK | RT5663_OSW_HP_L_MASK |
			RT5663_OSW_HP_R_MASK, RT5663_SI_HP_EN |
			RT5663_OSW_HP_L_DIS | RT5663_OSW_HP_R_DIS);
		rt5663_reg_update(pDevice, RT5663_DUMMY_1,
			RT5663_EMB_CLK_MASK | RT5663_HPA_CPL_BIAS_MASK |
			RT5663_HPA_CPR_BIAS_MASK, RT5663_EMB_CLK_EN |
			RT5663_HPA_CPL_BIAS_1 | RT5663_HPA_CPR_BIAS_1);
		rt5663_reg_update(pDevice, RT5663_CBJ_1,
			RT5663_INBUF_CBJ_BST1_MASK | RT5663_CBJ_SENSE_BST1_MASK,
			RT5663_INBUF_CBJ_BST1_ON | RT5663_CBJ_SENSE_BST1_L);
		rt5663_reg_update(pDevice, RT5663_IL_CMD_2,
			RT5663_PWR_MIC_DET_MASK, RT5663_PWR_MIC_DET_ON);
		/* BST1 power on for JD */
		rt5663_reg_update(pDevice, RT5663_PWR_ANLG_2,
			RT5663_PWR_BST1_MASK, RT5663_PWR_BST1_ON);
		rt5663_reg_update(pDevice, RT5663_EM_JACK_TYPE_1,
			RT5663_CBJ_DET_MASK | RT5663_EXT_JD_MASK |
			RT5663_POL_EXT_JD_MASK, RT5663_CBJ_DET_EN |
			RT5663_EXT_JD_EN | RT5663_POL_EXT_JD_EN);
		rt5663_reg_update(pDevice, RT5663_PWR_ANLG_1,
			RT5663_PWR_MB_MASK | RT5663_LDO1_DVO_MASK |
			RT5663_AMP_HP_MASK, RT5663_PWR_MB |
			RT5663_LDO1_DVO_0_9V | RT5663_AMP_HP_3X);
		rt5663_reg_update(pDevice, RT5663_PWR_ANLG_1,
			RT5663_PWR_VREF1_MASK | RT5663_PWR_VREF2_MASK |
			RT5663_PWR_FV1_MASK | RT5663_PWR_FV2_MASK,
			RT5663_PWR_VREF1 | RT5663_PWR_VREF2);
		msleep(20);
		rt5663_reg_update(pDevice, RT5663_PWR_ANLG_1,
			RT5663_PWR_FV1_MASK | RT5663_PWR_FV2_MASK,
			RT5663_PWR_FV1 | RT5663_PWR_FV2);
		rt5663_reg_update(pDevice, RT5663_AUTO_1MRC_CLK,
			RT5663_IRQ_POW_SAV_MASK, RT5663_IRQ_POW_SAV_EN);
		rt5663_reg_update(pDevice, RT5663_IRQ_1,
			RT5663_EN_IRQ_JD1_MASK, RT5663_EN_IRQ_JD1_EN);
		rt5663_reg_update(pDevice, RT5663_EM_JACK_TYPE_1,
			RT5663_EM_JD_MASK, RT5663_EM_JD_RST);
		rt5663_reg_update(pDevice, RT5663_EM_JACK_TYPE_1,
			RT5663_EM_JD_MASK, RT5663_EM_JD_NOR);

		while (true) {
			rt5663_reg_read(pDevice, RT5663_INT_ST_2, &val);
			if (!(val & 0x80))
				msleep(10);
			else
				break;

			if (i > 200)
				break;
			i++;
		}

		rt5663_reg_read(pDevice, RT5663_EM_JACK_TYPE_2, &val);
		val &= 0x0003;

		rt5663_reg_update(pDevice, RT5663_HP_CHARGE_PUMP_1,
			RT5663_OSW_HP_L_MASK | RT5663_OSW_HP_R_MASK,
			RT5663_OSW_HP_L_EN | RT5663_OSW_HP_R_EN);

		val = 2;

		switch (val) {
		case 0x1:
		case 0x2:
			pDevice->JackType = SND_JACK_HEADSET;
			rt5663_enable_push_button_irq(pDevice, true);

			break;
		default:
			pDevice->JackType = SND_JACK_HEADPHONE;

			rt5663_reg_update(pDevice,
				RT5663_PWR_ANLG_1,
				RT5663_PWR_MB_MASK | RT5663_PWR_VREF1_MASK |
				RT5663_PWR_VREF2_MASK, 0);
		}
	}
	else {
		if (pDevice->JackType == SND_JACK_HEADSET)
			rt5663_enable_push_button_irq(pDevice, false);

		pDevice->JackType = 0;

		rt5663_reg_update(pDevice, RT5663_PWR_ANLG_1,
			RT5663_PWR_MB_MASK | RT5663_PWR_VREF1_MASK |
			RT5663_PWR_VREF2_MASK, 0);
	}

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Jack Type: %d\n", pDevice->JackType);
	return pDevice->JackType;
}

static UINT16 rt5663_button_detect(PRTEK_CONTEXT pDevice)
{
	UINT16 btn_type, val;

	rt5663_reg_read(pDevice, RT5663_IL_CMD_5, &val);
	btn_type = val & 0xfff0;
	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"btn_type=%x\n", btn_type);
	rt5663_reg_write(pDevice,
		RT5663_IL_CMD_5, val);

	return btn_type;
}

static bool rt5663_check_jd_status(PRTEK_CONTEXT pDevice)
{
	UINT16 val;
	rt5663_reg_read(pDevice, RT5663_INT_ST_1, &val);

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"%s val=%x\n", __func__, val);

	/* JD1 */
	switch (pDevice->CodecVer) {
	case CODEC_VER_0:
		return !(val & 0x1000);
	}

	return false;
}

void rt5663_jackdetect(PRTEK_CONTEXT pDevice) {
	NTSTATUS status = STATUS_SUCCESS;

	msleep(250);

	if (rt5663_check_jd_status(pDevice)) {
		/* jack in */
		if (pDevice->JackType == 0) {
			/* jack was out, report jack type */
			switch (pDevice->CodecVer) {
			case CODEC_VER_0:
				pDevice->JackType = rt5663_jack_detect(pDevice, 1);
				break;
			}

			/* Delay the jack insert report to avoid pop noise */
			msleep(30);
		}
		else {
			/* jack is already in, report button event */
			UINT16 btn_type = rt5663_button_detect(pDevice);
			/**
			 * rt5663 can report three kinds of button behavior,
			 * one click, double click and hold. However,
			 * currently we will report button pressed/released
			 * event. So all the three button behaviors are
			 * treated as button pressed.
			 */
			int rawButton = 0;

			switch (btn_type) {
			case 0x8000:
			case 0x4000:
			case 0x2000:
				rawButton = 1;
				break;
			case 0x1000:
			case 0x0800:
			case 0x0400:
				rawButton = 2;
				break;
			case 0x0200:
			case 0x0100:
			case 0x0080:
				rawButton = 3;
				break;
			case 0x0040:
			case 0x0020:
			case 0x0010:
				rawButton = 4;
				break;
			case 0x0000: /* unpressed */
				break;
			default:
				RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"Unexpected button code 0x%04x\n",
					btn_type);
				break;
			}

			///XXX: Spurious Button Reports. Ignore

			/*Rt5663MediaReport report;
			report.ReportID = REPORTID_MEDIA;
			report.ControlCode = rawButton;

			size_t bytesWritten;
			Rt5663ProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);*/
		}
	}
	else {
		/* jack out */
		switch (pDevice->CodecVer) {
		case CODEC_VER_0:
			pDevice->JackType = rt5663_jack_detect(pDevice, 0);
			break;
		}
	}

	CsAudioSpecialKeyReport report;
	report.ReportID = REPORTID_SPECKEYS;
	report.ControlCode = CONTROL_CODE_JACK_TYPE;
	report.ControlValue = pDevice->JackType;

	size_t bytesWritten;
	Rt5663ProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);
}

VOID
RtekJdetWorkItem(
	IN WDFWORKITEM  WorkItem
)
{
	WDFDEVICE Device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
	PRTEK_CONTEXT pDevice = GetDeviceContext(Device);

	rt5663_jackdetect(pDevice);
}

BOOLEAN OnInterruptIsr(
	WDFINTERRUPT Interrupt,
	ULONG MessageID) {
	UNREFERENCED_PARAMETER(MessageID);

	WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
	PRTEK_CONTEXT pDevice = GetDeviceContext(Device);

	if (!pDevice->ConnectInterrupt)
		return true;

	NTSTATUS status = STATUS_SUCCESS;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDF_WORKITEM_CONFIG workitemConfig;
	WDFWORKITEM hWorkItem;

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
	WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, RTEK_CONTEXT);
	attributes.ParentObject = Device;
	WDF_WORKITEM_CONFIG_INIT(&workitemConfig, RtekJdetWorkItem);

	WdfWorkItemCreate(&workitemConfig,
		&attributes,
		&hWorkItem);

	WdfWorkItemEnqueue(hWorkItem);

	return true;
}

NTSTATUS
Rt5663EvtDeviceAdd(
	IN WDFDRIVER       Driver,
	IN PWDFDEVICE_INIT DeviceInit
)
{
	NTSTATUS                      status = STATUS_SUCCESS;
	WDF_IO_QUEUE_CONFIG           queueConfig;
	WDF_OBJECT_ATTRIBUTES         attributes;
	WDFDEVICE                     device;
	WDF_INTERRUPT_CONFIG interruptConfig;
	WDFQUEUE                      queue;
	UCHAR                         minorFunction;
	PRTEK_CONTEXT               devContext;

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	RtekPrint(DEBUG_LEVEL_INFO, DBG_PNP,
		"Rt5663EvtDeviceAdd called\n");

	//
	// Tell framework this is a filter driver. Filter drivers by default are  
	// not power policy owners. This works well for this driver because
	// HIDclass driver is the power policy owner for HID minidrivers.
	//

	WdfFdoInitSetFilter(DeviceInit);

	{
		WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
		WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

		pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
		pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
		pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
		pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

		WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
	}

	//
	// Setup the device context
	//

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, RTEK_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfDeviceCreate failed with status code 0x%x\n", status);

		return status;
	}

	{
		WDF_DEVICE_STATE deviceState;
		WDF_DEVICE_STATE_INIT(&deviceState);

		deviceState.NotDisableable = WdfFalse;
		WdfDeviceSetDeviceState(device, &deviceState);
	}

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

	queueConfig.EvtIoInternalDeviceControl = Rt5663EvtInternalDeviceControl;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&queue
	);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create manual I/O queue to take care of hid report read requests
	//

	devContext = GetDeviceContext(device);

	devContext->FxDevice = device;

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->ReportQueue
	);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create an interrupt object for hardware notifications
	//
	WDF_INTERRUPT_CONFIG_INIT(
		&interruptConfig,
		OnInterruptIsr,
		NULL);
	interruptConfig.PassiveHandling = TRUE;

	status = WdfInterruptCreate(
		device,
		&interruptConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->Interrupt);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"Error creating WDF interrupt object - %!STATUS!",
			status);

		return status;
	}

	return status;
}

VOID
Rt5663EvtInternalDeviceControl(
	IN WDFQUEUE     Queue,
	IN WDFREQUEST   Request,
	IN size_t       OutputBufferLength,
	IN size_t       InputBufferLength,
	IN ULONG        IoControlCode
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	WDFDEVICE           device;
	PRTEK_CONTEXT     devContext;
	BOOLEAN             completeRequest = TRUE;

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	device = WdfIoQueueGetDevice(Queue);
	devContext = GetDeviceContext(device);

	RtekPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
		"%s, Queue:0x%p, Request:0x%p\n",
		DbgHidInternalIoctlString(IoControlCode),
		Queue,
		Request
	);

	//
	// Please note that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl. So depending on the ioctl code, we will either
	// use retreive function or escape to WDM to get the UserBuffer.
	//

	switch (IoControlCode)
	{

	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		//
		// Retrieves the device's HID descriptor.
		//
		status = Rt5663GetHidDescriptor(device, Request);
		break;

	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		//
		//Retrieves a device's attributes in a HID_DEVICE_ATTRIBUTES structure.
		//
		status = Rt5663GetDeviceAttributes(Request);
		break;

	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		//
		//Obtains the report descriptor for the HID device.
		//
		status = Rt5663GetReportDescriptor(device, Request);
		break;

	case IOCTL_HID_GET_STRING:
		//
		// Requests that the HID minidriver retrieve a human-readable string
		// for either the manufacturer ID, the product ID, or the serial number
		// from the string descriptor of the device. The minidriver must send
		// a Get String Descriptor request to the device, in order to retrieve
		// the string descriptor, then it must extract the string at the
		// appropriate index from the string descriptor and return it in the
		// output buffer indicated by the IRP. Before sending the Get String
		// Descriptor request, the minidriver must retrieve the appropriate
		// index for the manufacturer ID, the product ID or the serial number
		// from the device extension of a top level collection associated with
		// the device.
		//
		status = Rt5663GetString(Request);
		break;

	case IOCTL_HID_WRITE_REPORT:
	case IOCTL_HID_SET_OUTPUT_REPORT:
		//
		//Transmits a class driver-supplied report to the device.
		//
		status = Rt5663WriteReport(devContext, Request);
		break;

	case IOCTL_HID_READ_REPORT:
	case IOCTL_HID_GET_INPUT_REPORT:
		//
		// Returns a report from the device into a class driver-supplied buffer.
		// 
		status = Rt5663ReadReport(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_SET_FEATURE:
		//
		// This sends a HID class feature report to a top-level collection of
		// a HID class device.
		//
		status = Rt5663SetFeature(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_GET_FEATURE:
		//
		// returns a feature report associated with a top-level collection
		status = Rt5663GetFeature(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_ACTIVATE_DEVICE:
		//
		// Makes the device ready for I/O operations.
		//
	case IOCTL_HID_DEACTIVATE_DEVICE:
		//
		// Causes the device to cease operations and terminate all outstanding
		// I/O requests.
		//
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	if (completeRequest)
	{
		WdfRequestComplete(Request, status);

		RtekPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"%s completed, Queue:0x%p, Request:0x%p\n",
			DbgHidInternalIoctlString(IoControlCode),
			Queue,
			Request
		);
	}
	else
	{
		RtekPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"%s deferred, Queue:0x%p, Request:0x%p\n",
			DbgHidInternalIoctlString(IoControlCode),
			Queue,
			Request
		);
	}

	return;
}

NTSTATUS
Rt5663GetHidDescriptor(
	IN WDFDEVICE Device,
	IN WDFREQUEST Request
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	size_t              bytesToCopy = 0;
	WDFMEMORY           memory;

	UNREFERENCED_PARAMETER(Device);

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663GetHidDescriptor Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputMemory(Request, &memory);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputMemory failed 0x%x\n", status);

		return status;
	}

	//
	// Use hardcoded "HID Descriptor" 
	//
	bytesToCopy = DefaultHidDescriptor.bLength;

	if (bytesToCopy == 0)
	{
		status = STATUS_INVALID_DEVICE_STATE;

		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"DefaultHidDescriptor is zero, 0x%x\n", status);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0, // Offset
		(PVOID)&DefaultHidDescriptor,
		bytesToCopy);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfMemoryCopyFromBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, bytesToCopy);

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663GetHidDescriptor Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Rt5663GetReportDescriptor(
	IN WDFDEVICE Device,
	IN WDFREQUEST Request
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	ULONG_PTR           bytesToCopy;
	WDFMEMORY           memory;

	PRTEK_CONTEXT devContext = GetDeviceContext(Device);

	UNREFERENCED_PARAMETER(Device);

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663GetReportDescriptor Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputMemory(Request, &memory);
	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputMemory failed 0x%x\n", status);

		return status;
	}

	//
	// Use hardcoded Report descriptor
	//
	bytesToCopy = DefaultHidDescriptor.DescriptorList[0].wReportLength;

	if (bytesToCopy == 0)
	{
		status = STATUS_INVALID_DEVICE_STATE;

		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"DefaultHidDescriptor's reportLength is zero, 0x%x\n", status);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0,
		(PVOID)DefaultReportDescriptor,
		bytesToCopy);
	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfMemoryCopyFromBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, bytesToCopy);

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663GetReportDescriptor Exit = 0x%x\n", status);

	return status;
}


NTSTATUS
Rt5663GetDeviceAttributes(
	IN WDFREQUEST Request
)
{
	NTSTATUS                 status = STATUS_SUCCESS;
	PHID_DEVICE_ATTRIBUTES   deviceAttributes = NULL;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663GetDeviceAttributes Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputBuffer(Request,
		sizeof(HID_DEVICE_ATTRIBUTES),
		(PVOID *)&deviceAttributes,
		NULL);
	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Set USB device descriptor
	//

	deviceAttributes->Size = sizeof(HID_DEVICE_ATTRIBUTES);
	deviceAttributes->VendorID = RT5663_VID;
	deviceAttributes->ProductID = RT5663_PID;
	deviceAttributes->VersionNumber = RT5663_VERSION;

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, sizeof(HID_DEVICE_ATTRIBUTES));

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663GetDeviceAttributes Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Rt5663GetString(
	IN WDFREQUEST Request
)
{

	NTSTATUS status = STATUS_SUCCESS;
	PWSTR pwstrID;
	size_t lenID;
	WDF_REQUEST_PARAMETERS params;
	void *pStringBuffer = NULL;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663GetString Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	switch ((ULONG_PTR)params.Parameters.DeviceIoControl.Type3InputBuffer & 0xFFFF)
	{
	case HID_STRING_ID_IMANUFACTURER:
		pwstrID = L"Rt5663.\0";
		break;

	case HID_STRING_ID_IPRODUCT:
		pwstrID = L"MaxTouch Touch Screen\0";
		break;

	case HID_STRING_ID_ISERIALNUMBER:
		pwstrID = L"123123123\0";
		break;

	default:
		pwstrID = NULL;
		break;
	}

	lenID = pwstrID ? wcslen(pwstrID) * sizeof(WCHAR) + sizeof(UNICODE_NULL) : 0;

	if (pwstrID == NULL)
	{

		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Rt5663GetString Invalid request type\n");

		status = STATUS_INVALID_PARAMETER;

		return status;
	}

	status = WdfRequestRetrieveOutputBuffer(Request,
		lenID,
		&pStringBuffer,
		&lenID);

	if (!NT_SUCCESS(status))
	{

		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Rt5663GetString WdfRequestRetrieveOutputBuffer failed Status 0x%x\n", status);

		return status;
	}

	RtlCopyMemory(pStringBuffer, pwstrID, lenID);

	WdfRequestSetInformation(Request, lenID);

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663GetString Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Rt5663WriteReport(
	IN PRTEK_CONTEXT DevContext,
	IN WDFREQUEST Request
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;
	size_t bytesWritten = 0;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663WriteReport Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Rt5663WriteReport Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"Rt5663WriteReport No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			case REPORTID_SPECKEYS:
				status = STATUS_SUCCESS;

				CsAudioSpecialKeyReport report;
				report.ReportID = REPORTID_SPECKEYS;
				report.ControlCode = CONTROL_CODE_JACK_TYPE;
				report.ControlValue = DevContext->JackType;

				size_t bytesWritten;
				RtekProcessVendorReport(DevContext, &report, sizeof(report), &bytesWritten);
				break;
			default:

				RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"Rt5663WriteReport Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663WriteReport Exit = 0x%x\n", status);

	return status;

}

NTSTATUS
Rt5663ProcessVendorReport(
	IN PRTEK_CONTEXT DevContext,
	IN PVOID ReportBuffer,
	IN ULONG ReportBufferLen,
	OUT size_t* BytesWritten
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFREQUEST reqRead;
	PVOID pReadReport = NULL;
	size_t bytesReturned = 0;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663ProcessVendorReport Entry\n");

	status = WdfIoQueueRetrieveNextRequest(DevContext->ReportQueue,
		&reqRead);

	if (NT_SUCCESS(status))
	{
		status = WdfRequestRetrieveOutputBuffer(reqRead,
			ReportBufferLen,
			&pReadReport,
			&bytesReturned);

		if (NT_SUCCESS(status))
		{
			//
			// Copy ReportBuffer into read request
			//

			if (bytesReturned > ReportBufferLen)
			{
				bytesReturned = ReportBufferLen;
			}

			RtlCopyMemory(pReadReport,
				ReportBuffer,
				bytesReturned);

			//
			// Complete read with the number of bytes returned as info
			//

			WdfRequestCompleteWithInformation(reqRead,
				status,
				bytesReturned);

			RtekPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"Rt5663ProcessVendorReport %d bytes returned\n", bytesReturned);

			//
			// Return the number of bytes written for the write request completion
			//

			*BytesWritten = bytesReturned;

			RtekPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"%s completed, Queue:0x%p, Request:0x%p\n",
				DbgHidInternalIoctlString(IOCTL_HID_READ_REPORT),
				DevContext->ReportQueue,
				reqRead);
		}
		else
		{
			RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"WdfRequestRetrieveOutputBuffer failed Status 0x%x\n", status);
		}
	}
	else
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfIoQueueRetrieveNextRequest failed Status 0x%x\n", status);
	}

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663ProcessVendorReport Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Rt5663ReadReport(
	IN PRTEK_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS status = STATUS_SUCCESS;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663ReadReport Entry\n");

	//
	// Forward this read request to our manual queue
	// (in other words, we are going to defer this request
	// until we have a corresponding write request to
	// match it with)
	//

	status = WdfRequestForwardToIoQueue(Request, DevContext->ReportQueue);

	if (!NT_SUCCESS(status))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestForwardToIoQueue failed Status 0x%x\n", status);
	}
	else
	{
		*CompleteRequest = FALSE;
	}

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663ReadReport Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Rt5663SetFeature(
	IN PRTEK_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663SetFeature Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Rt5663SetFeature Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"Rt5663WriteReport No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			default:

				RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"Rt5663SetFeature Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663SetFeature Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
Rt5663GetFeature(
	IN PRTEK_CONTEXT DevContext,
	IN WDFREQUEST Request,
	OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663GetFeature Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.OutputBufferLength < sizeof(HID_XFER_PACKET))
	{
		RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"Rt5663GetFeature Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"Rt5663GetFeature No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			default:

				RtekPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"Rt5663GetFeature Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	RtekPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"Rt5663GetFeature Exit = 0x%x\n", status);

	return status;
}

PCHAR
DbgHidInternalIoctlString(
	IN ULONG IoControlCode
)
{
	switch (IoControlCode)
	{
	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		return "IOCTL_HID_GET_DEVICE_DESCRIPTOR";
	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		return "IOCTL_HID_GET_REPORT_DESCRIPTOR";
	case IOCTL_HID_READ_REPORT:
		return "IOCTL_HID_READ_REPORT";
	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		return "IOCTL_HID_GET_DEVICE_ATTRIBUTES";
	case IOCTL_HID_WRITE_REPORT:
		return "IOCTL_HID_WRITE_REPORT";
	case IOCTL_HID_SET_FEATURE:
		return "IOCTL_HID_SET_FEATURE";
	case IOCTL_HID_GET_FEATURE:
		return "IOCTL_HID_GET_FEATURE";
	case IOCTL_HID_GET_STRING:
		return "IOCTL_HID_GET_STRING";
	case IOCTL_HID_ACTIVATE_DEVICE:
		return "IOCTL_HID_ACTIVATE_DEVICE";
	case IOCTL_HID_DEACTIVATE_DEVICE:
		return "IOCTL_HID_DEACTIVATE_DEVICE";
	case IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST:
		return "IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST";
	case IOCTL_HID_SET_OUTPUT_REPORT:
		return "IOCTL_HID_SET_OUTPUT_REPORT";
	case IOCTL_HID_GET_INPUT_REPORT:
		return "IOCTL_HID_GET_INPUT_REPORT";
	default:
		return "Unknown IOCTL";
	}
}