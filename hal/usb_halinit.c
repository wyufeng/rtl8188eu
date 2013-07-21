/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _HCI_HAL_INIT_C_

#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_efuse.h>

#include <rtl8188e_hal.h>
#include <rtl8188e_led.h>
#include <rtw_iol.h>
#include <usb_ops.h>
#include <usb_hal.h>
#include <usb_osintf.h>

#ifdef CONFIG_EFUSE_CONFIG_FILE
#include <linux/fs.h>
#include <asm/uaccess.h>
#endif /* CONFIG_EFUSE_CONFIG_FILE */

#if DISABLE_BB_RF
	#define		HAL_MAC_ENABLE	0
	#define		HAL_BB_ENABLE		0
	#define		HAL_RF_ENABLE		0
#else
	#define		HAL_MAC_ENABLE	1
	#define		HAL_BB_ENABLE		1
	#define		HAL_RF_ENABLE		1
#endif


static void
_ConfigNormalChipOutEP_8188E(
		PADAPTER	pAdapter,
		u8		NumOutPipe
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);

	switch (NumOutPipe){
		case	3:
				pHalData->OutEpQueueSel=TX_SELE_HQ| TX_SELE_LQ|TX_SELE_NQ;
				pHalData->OutEpNumber=3;
				break;
		case	2:
				pHalData->OutEpQueueSel=TX_SELE_HQ| TX_SELE_NQ;
				pHalData->OutEpNumber=2;
				break;
		case	1:
				pHalData->OutEpQueueSel=TX_SELE_HQ;
				pHalData->OutEpNumber=1;
				break;
		default:
				break;

	}
	DBG_88E("%s OutEpQueueSel(0x%02x), OutEpNumber(%d)\n",__func__,pHalData->OutEpQueueSel,pHalData->OutEpNumber );

}

static bool HalUsbSetQueuePipeMapping8188EUsb(
		PADAPTER	pAdapter,
		u8		NumInPipe,
		u8		NumOutPipe
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(pAdapter);
	bool			result		= false;

	_ConfigNormalChipOutEP_8188E(pAdapter, NumOutPipe);

	/*  Normal chip with one IN and one OUT doesn't have interrupt IN EP. */
	if (1 == pHalData->OutEpNumber){
		if (1 != NumInPipe){
			return result;
		}
	}

	/*  All config other than above support one Bulk IN and one Interrupt IN. */

	result = Hal_MappingOutPipe(pAdapter, NumOutPipe);

	return result;

}

static void rtl8188eu_interface_configure(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	struct dvobj_priv	*pdvobjpriv = adapter_to_dvobj(padapter);

	if (pdvobjpriv->ishighspeed == true)
	{
		pHalData->UsbBulkOutSize = USB_HIGH_SPEED_BULK_SIZE;/* 512 bytes */
	}
	else
	{
		pHalData->UsbBulkOutSize = USB_FULL_SPEED_BULK_SIZE;/* 64 bytes */
	}

	pHalData->interfaceIndex = pdvobjpriv->InterfaceNumber;

	pHalData->UsbTxAggMode		= 1;
	pHalData->UsbTxAggDescNum	= 0x6;	/*  only 4 bits */

	pHalData->UsbRxAggMode		= USB_RX_AGG_DMA;/*  USB_RX_AGG_DMA; */
	pHalData->UsbRxAggBlockCount	= 8; /* unit : 512b */
	pHalData->UsbRxAggBlockTimeout	= 0x6;
	pHalData->UsbRxAggPageCount	= 48; /* uint :128 b 0x0A;	10 = MAX_RX_DMA_BUFFER_SIZE/2/pHalData->UsbBulkOutSize */
	pHalData->UsbRxAggPageTimeout	= 0x4; /* 6, absolute time = 34ms/(2^6) */

	HalUsbSetQueuePipeMapping8188EUsb(padapter,
				pdvobjpriv->RtNumInPipes, pdvobjpriv->RtNumOutPipes);

}

static u32 rtl8188eu_InitPowerOn(_adapter *padapter)
{
	u16 value16;
	/*  HW Power on sequence */
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	if (true == pHalData->bMacPwrCtrlOn)
		return _SUCCESS;

	if (!HalPwrSeqCmdParsing(padapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, Rtl8188E_NIC_PWR_ON_FLOW))
	{
		DBG_88E(KERN_ERR "%s: run power on flow fail\n", __func__);
		return _FAIL;
	}

	/*  Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	/*  Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31. */
	rtw_write16(padapter, REG_CR, 0x00);  /* suggseted by zhouzhou, by page, 20111230 */


		/*  Enable MAC DMA/WMAC/SCHEDULE/SEC block */
	value16 = rtw_read16(padapter, REG_CR);
	value16 |= (HCI_TXDMA_EN | HCI_RXDMA_EN | TXDMA_EN | RXDMA_EN
				| PROTOCOL_EN | SCHEDULE_EN | ENSEC | CALTMR_EN);
	/*  for SDIO - Set CR bit10 to enable 32k calibration. Suggested by SD1 Gimmy. Added by tynli. 2011.08.31. */

	rtw_write16(padapter, REG_CR, value16);
	pHalData->bMacPwrCtrlOn = true;

	return _SUCCESS;

}


static void _dbg_dump_macreg(_adapter *padapter)
{
	u32 offset = 0;
	u32 val32 = 0;
	u32 index =0 ;
	for (index=0;index<64;index++)
	{
		offset = index*4;
		val32 = rtw_read32(padapter,offset);
		DBG_88E("offset : 0x%02x ,val:0x%08x\n",offset,val32);
	}
}


static void _InitPABias(_adapter *padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u8	pa_setting;
	bool	is92C = IS_92C_SERIAL(pHalData->VersionID);

	/* FIXED PA current issue */
	pa_setting = EFUSE_Read1Byte(padapter, 0x1FA);

	if (!(pa_setting & BIT0))
	{
		PHY_SetRFReg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0x0F406);
		PHY_SetRFReg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0x4F406);
		PHY_SetRFReg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0x8F406);
		PHY_SetRFReg(padapter, RF_PATH_A, 0x15, 0x0FFFFF, 0xCF406);
	}

	if (!(pa_setting & BIT1) && is92C)
	{
		PHY_SetRFReg(padapter,RF_PATH_B, 0x15, 0x0FFFFF, 0x0F406);
		PHY_SetRFReg(padapter,RF_PATH_B, 0x15, 0x0FFFFF, 0x4F406);
		PHY_SetRFReg(padapter,RF_PATH_B, 0x15, 0x0FFFFF, 0x8F406);
		PHY_SetRFReg(padapter,RF_PATH_B, 0x15, 0x0FFFFF, 0xCF406);
	}

	if (!(pa_setting & BIT4))
	{
		pa_setting = rtw_read8(padapter, 0x16);
		pa_setting &= 0x0F;
		rtw_write8(padapter, 0x16, pa_setting | 0x80);
		rtw_write8(padapter, 0x16, pa_setting | 0x90);
	}
}
#ifdef CONFIG_BT_COEXIST
static void _InitBTCoexist(_adapter *padapter)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(padapter);
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
	u8 u1Tmp;

	if (pbtpriv->BT_Coexist && pbtpriv->BT_CoexistType == BT_CSR_BC4)
	{

/* if MP_DRIVER != 1 */
	if (padapter->registrypriv.mp_mode == 0)
	{
		if (pbtpriv->BT_Ant_isolation)
		{
			rtw_write8( padapter,REG_GPIO_MUXCFG, 0xa0);
			DBG_88E("BT write 0x%x = 0x%x\n", REG_GPIO_MUXCFG, 0xa0);
		}
	}
/* endif */

		u1Tmp = rtw_read8(padapter, 0x4fd) & BIT0;
		u1Tmp = u1Tmp |
				((pbtpriv->BT_Ant_isolation==1)?0:BIT1) |
				((pbtpriv->BT_Service==BT_SCO)?0:BIT2);
		rtw_write8( padapter, 0x4fd, u1Tmp);
		DBG_88E("BT write 0x%x = 0x%x for non-isolation\n", 0x4fd, u1Tmp);


		rtw_write32(padapter, REG_BT_COEX_TABLE+4, 0xaaaa9aaa);
		DBG_88E("BT write 0x%x = 0x%x\n", REG_BT_COEX_TABLE+4, 0xaaaa9aaa);

		rtw_write32(padapter, REG_BT_COEX_TABLE+8, 0xffbd0040);
		DBG_88E("BT write 0x%x = 0x%x\n", REG_BT_COEX_TABLE+8, 0xffbd0040);

		rtw_write32(padapter,  REG_BT_COEX_TABLE+0xc, 0x40000010);
		DBG_88E("BT write 0x%x = 0x%x\n", REG_BT_COEX_TABLE+0xc, 0x40000010);

		/* Config to 1T1R */
		u1Tmp =  rtw_read8(padapter,rOFDM0_TRxPathEnable);
		u1Tmp &= ~(BIT1);
		rtw_write8( padapter, rOFDM0_TRxPathEnable, u1Tmp);
		DBG_88E("BT write 0xC04 = 0x%x\n", u1Tmp);

		u1Tmp = rtw_read8(padapter, rOFDM1_TRxPathEnable);
		u1Tmp &= ~(BIT1);
		rtw_write8( padapter, rOFDM1_TRxPathEnable, u1Tmp);
		DBG_88E("BT write 0xD04 = 0x%x\n", u1Tmp);

	}
}
#endif



/*  */
/*  */
/* 	MAC init functions */
/*  */
/*  */
static void
_SetMacID(
	PADAPTER Adapter, u8* MacID
	)
{
	u32 i;

	for (i=0 ; i< MAC_ADDR_LEN ; i++)
		rtw_write32(Adapter, REG_MACID+i, MacID[i]);
}

static void
_SetBSSID(
	PADAPTER Adapter, u8* BSSID
	)
{
	u32 i;

	for (i=0 ; i< MAC_ADDR_LEN ; i++)
		rtw_write32(Adapter, REG_BSSID+i, BSSID[i]);
}


/*  Shall USB interface init this? */
static void
_InitInterrupt(
	PADAPTER Adapter
	)
{
	u32	imr,imr_ex;
	u8  usb_opt;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	/* HISR write one to clear */
	rtw_write32(Adapter, REG_HISR_88E, 0xFFFFFFFF);
	/*  HIMR - */
	imr = IMR_PSTIMEOUT_88E | IMR_TBDER_88E | IMR_CPWM_88E | IMR_CPWM2_88E ;
	rtw_write32(Adapter, REG_HIMR_88E, imr);
	pHalData->IntrMask[0]=imr;

	imr_ex = IMR_TXERR_88E | IMR_RXERR_88E | IMR_TXFOVW_88E |IMR_RXFOVW_88E;
	rtw_write32(Adapter, REG_HIMRE_88E, imr_ex);
	pHalData->IntrMask[1]=imr_ex;

#ifdef CONFIG_SUPPORT_USB_INT
	/*  REG_USB_SPECIAL_OPTION - BIT(4) */
	/*  0; Use interrupt endpoint to upload interrupt pkt */
	/*  1; Use bulk endpoint to upload interrupt pkt, */
	usb_opt = rtw_read8(Adapter, REG_USB_SPECIAL_OPTION);


	if (!adapter_to_dvobj(Adapter)->ishighspeed)
		usb_opt = usb_opt & (~INT_BULK_SEL);
	else
		usb_opt = usb_opt | (INT_BULK_SEL);

	rtw_write8(Adapter, REG_USB_SPECIAL_OPTION, usb_opt );

#endif/* CONFIG_SUPPORT_USB_INT */

}


static void
_InitQueueReservedPage(
	PADAPTER Adapter
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv;
	u32			outEPNum	= (u32)pHalData->OutEpNumber;
	u32			numHQ		= 0;
	u32			numLQ		= 0;
	u32			numNQ		= 0;
	u32			numPubQ;
	u32			value32;
	u8			value8;
	bool			bWiFiConfig	= pregistrypriv->wifi_spec;

	if (bWiFiConfig)
	{
		if (pHalData->OutEpQueueSel & TX_SELE_HQ)
		{
			numHQ =  0x29;
		}

		if (pHalData->OutEpQueueSel & TX_SELE_LQ)
		{
			numLQ = 0x1C;
		}

		/*  NOTE: This step shall be proceed before writting REG_RQPN. */
		if (pHalData->OutEpQueueSel & TX_SELE_NQ) {
			numNQ = 0x1C;
		}
		value8 = (u8)_NPQ(numNQ);
		rtw_write8(Adapter, REG_RQPN_NPQ, value8);

		numPubQ = 0xA8 - numHQ - numLQ - numNQ;

		/*  TX DMA */
		value32 = _HPQ(numHQ) | _LPQ(numLQ) | _PUBQ(numPubQ) | LD_RQPN;
		rtw_write32(Adapter, REG_RQPN, value32);
	}
	else
	{
		rtw_write16(Adapter,REG_RQPN_NPQ, 0x0000);/* Just follow MP Team,??? Georgia 03/28 */
		rtw_write16(Adapter,REG_RQPN_NPQ, 0x0d);
		rtw_write32(Adapter,REG_RQPN, 0x808E000d);/* reserve 7 page for LPS */
	}
}

static void
_InitTxBufferBoundary(
	PADAPTER Adapter,
	u8 txpktbuf_bndy
	)
{
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;


	rtw_write8(Adapter, REG_TXPKTBUF_BCNQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TXPKTBUF_MGQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TXPKTBUF_WMAC_LBK_BF_HD, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TRXFF_BNDY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TDECTRL+1, txpktbuf_bndy);

}

static void
_InitPageBoundary(
	PADAPTER Adapter
	)
{
	/*  RX Page Boundary */
	/*  */
	u16 rxff_bndy = MAX_RX_DMA_BUFFER_SIZE_88E-1;

	rtw_write16(Adapter, (REG_TRXFF_BNDY + 2), rxff_bndy);
}


static void
_InitNormalChipRegPriority(
		PADAPTER	Adapter,
		u16		beQ,
		u16		bkQ,
		u16		viQ,
		u16		voQ,
		u16		mgtQ,
		u16		hiQ
	)
{
	u16 value16	= (rtw_read16(Adapter, REG_TRXDMA_CTRL) & 0x7);

	value16 |=	_TXDMA_BEQ_MAP(beQ)	| _TXDMA_BKQ_MAP(bkQ) |
				_TXDMA_VIQ_MAP(viQ)	| _TXDMA_VOQ_MAP(voQ) |
				_TXDMA_MGQ_MAP(mgtQ)| _TXDMA_HIQ_MAP(hiQ);

	rtw_write16(Adapter, REG_TRXDMA_CTRL, value16);
}

static void
_InitNormalChipOneOutEpPriority(
		PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	u16	value = 0;
	switch (pHalData->OutEpQueueSel)
	{
		case TX_SELE_HQ:
			value = QUEUE_HIGH;
			break;
		case TX_SELE_LQ:
			value = QUEUE_LOW;
			break;
		case TX_SELE_NQ:
			value = QUEUE_NORMAL;
			break;
		default:
			break;
	}

	_InitNormalChipRegPriority(Adapter,
								value,
								value,
								value,
								value,
								value,
								value
								);

}

static void
_InitNormalChipTwoOutEpPriority(
		PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u16			beQ,bkQ,viQ,voQ,mgtQ,hiQ;


	u16	valueHi = 0;
	u16	valueLow = 0;

	switch (pHalData->OutEpQueueSel)
	{
		case (TX_SELE_HQ | TX_SELE_LQ):
			valueHi = QUEUE_HIGH;
			valueLow = QUEUE_LOW;
			break;
		case (TX_SELE_NQ | TX_SELE_LQ):
			valueHi = QUEUE_NORMAL;
			valueLow = QUEUE_LOW;
			break;
		case (TX_SELE_HQ | TX_SELE_NQ):
			valueHi = QUEUE_HIGH;
			valueLow = QUEUE_NORMAL;
			break;
		default:
			break;
	}

	if (!pregistrypriv->wifi_spec ){
		beQ		= valueLow;
		bkQ		= valueLow;
		viQ		= valueHi;
		voQ		= valueHi;
		mgtQ	= valueHi;
		hiQ		= valueHi;
	}
	else{/* for WMM ,CONFIG_OUT_EP_WIFI_MODE */
		beQ		= valueLow;
		bkQ		= valueHi;
		viQ		= valueHi;
		voQ		= valueLow;
		mgtQ	= valueHi;
		hiQ		= valueHi;
	}

	_InitNormalChipRegPriority(Adapter,beQ,bkQ,viQ,voQ,mgtQ,hiQ);

}

static void
_InitNormalChipThreeOutEpPriority(
		PADAPTER Adapter
	)
{
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;
	u16			beQ,bkQ,viQ,voQ,mgtQ,hiQ;

	if (!pregistrypriv->wifi_spec ){/*  typical setting */
		beQ		= QUEUE_LOW;
		bkQ		= QUEUE_LOW;
		viQ		= QUEUE_NORMAL;
		voQ		= QUEUE_HIGH;
		mgtQ	= QUEUE_HIGH;
		hiQ		= QUEUE_HIGH;
	}
	else{/*  for WMM */
		beQ		= QUEUE_LOW;
		bkQ		= QUEUE_NORMAL;
		viQ		= QUEUE_NORMAL;
		voQ		= QUEUE_HIGH;
		mgtQ	= QUEUE_HIGH;
		hiQ		= QUEUE_HIGH;
	}
	_InitNormalChipRegPriority(Adapter,beQ,bkQ,viQ,voQ,mgtQ,hiQ);
}

static void
_InitQueuePriority(
		PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	switch (pHalData->OutEpNumber)
	{
		case 1:
			_InitNormalChipOneOutEpPriority(Adapter);
			break;
		case 2:
			_InitNormalChipTwoOutEpPriority(Adapter);
			break;
		case 3:
			_InitNormalChipThreeOutEpPriority(Adapter);
			break;
		default:
			break;
	}


}



static void
_InitHardwareDropIncorrectBulkOut(
	PADAPTER Adapter
	)
{
	u32	value32 = rtw_read32(Adapter, REG_TXDMA_OFFSET_CHK);
	value32 |= DROP_DATA_EN;
	rtw_write32(Adapter, REG_TXDMA_OFFSET_CHK, value32);
}

static void
_InitNetworkType(
	PADAPTER Adapter
	)
{
	u32	value32;

	value32 = rtw_read32(Adapter, REG_CR);
	/*  TODO: use the other function to set network type */
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);

	rtw_write32(Adapter, REG_CR, value32);
}

static void
_InitTransferPageSize(
	PADAPTER Adapter
	)
{
	/*  Tx page size is always 128. */

	u8	value8;
	value8 = _PSRX(PBP_128) | _PSTX(PBP_128);
	rtw_write8(Adapter, REG_PBP, value8);
}

static void
_InitDriverInfoSize(
	PADAPTER	Adapter,
		u8		drvInfoSize
	)
{
	rtw_write8(Adapter,REG_RX_DRVINFO_SZ, drvInfoSize);
}

static void
_InitWMACSetting(
	PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	pHalData->ReceiveConfig =
	RCR_AAP | RCR_APM | RCR_AM | RCR_AB |RCR_CBSSID_DATA| RCR_CBSSID_BCN| RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYSTS;

#if (1 == RTL8188E_RX_PACKET_INCLUDE_CRC)
	pHalData->ReceiveConfig |= ACRC32;
#endif

	/*  some REG_RCR will be modified later by phy_ConfigMACWithHeaderFile() */
	rtw_write32(Adapter, REG_RCR, pHalData->ReceiveConfig);

	/*  Accept all multicast address */
	rtw_write32(Adapter, REG_MAR, 0xFFFFFFFF);
	rtw_write32(Adapter, REG_MAR + 4, 0xFFFFFFFF);
}

static void
_InitAdaptiveCtrl(
	PADAPTER Adapter
	)
{
	u16	value16;
	u32	value32;

	/*  Response Rate Set */
	value32 = rtw_read32(Adapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	value32 |= RATE_RRSR_CCK_ONLY_1M;
	rtw_write32(Adapter, REG_RRSR, value32);

	/*  CF-END Threshold */

	/*  SIFS (used in NAV) */
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtw_write16(Adapter, REG_SPEC_SIFS, value16);

	/*  Retry Limit */
	value16 = _LRL(0x30) | _SRL(0x30);
	rtw_write16(Adapter, REG_RL, value16);

}

static void
_InitRateFallback(
	PADAPTER Adapter
	)
{
	/*  Set Data Auto Rate Fallback Retry Count register. */
	rtw_write32(Adapter, REG_DARFRC, 0x00000000);
	rtw_write32(Adapter, REG_DARFRC+4, 0x10080404);
	rtw_write32(Adapter, REG_RARFRC, 0x04030201);
	rtw_write32(Adapter, REG_RARFRC+4, 0x08070605);

}


static void
_InitEDCA(
	PADAPTER Adapter
	)
{
	/*  Set Spec SIFS (used in NAV) */
	rtw_write16(Adapter,REG_SPEC_SIFS, 0x100a);
	rtw_write16(Adapter,REG_MAC_SPEC_SIFS, 0x100a);

	/*  Set SIFS for CCK */
	rtw_write16(Adapter,REG_SIFS_CTX, 0x100a);

	/*  Set SIFS for OFDM */
	rtw_write16(Adapter,REG_SIFS_TRX, 0x100a);

	/*  TXOP */
	rtw_write32(Adapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(Adapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(Adapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(Adapter, REG_EDCA_VO_PARAM, 0x002FA226);
}

static void _InitBeaconMaxError(PADAPTER	Adapter, bool		InfraMode)
{
}

static void _InitHWLed(PADAPTER Adapter)
{
	struct led_priv *pledpriv = &(Adapter->ledpriv);

	if ( pledpriv->LedStrategy != HW_LED)
		return;

/*  HW led control */
/*  to do .... */
/* must consider cases of antenna diversity/ commbo card/solo card/mini card */

}

static void _InitRDGSetting(PADAPTER Adapter)
{
	rtw_write8(Adapter,REG_RD_CTRL,0xFF);
	rtw_write16(Adapter, REG_RD_NAV_NXT, 0x200);
	rtw_write8(Adapter,REG_RD_RESP_PKT_TH,0x05);
}

static void
_InitRxSetting(
		PADAPTER Adapter
	)
{
	rtw_write32(Adapter, REG_MACID, 0x87654321);
	rtw_write32(Adapter, 0x0700, 0x87654321);
}

static void
_InitRetryFunction(
	PADAPTER Adapter
	)
{
	u8	value8;

	value8 = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL);
	value8 |= EN_AMPDU_RTY_NEW;
	rtw_write8(Adapter, REG_FWHW_TXQ_CTRL, value8);

	/*  Set ACK timeout */
	rtw_write8(Adapter, REG_ACKTO, 0x40);
}

/*-----------------------------------------------------------------------------
 * Function:	usb_AggSettingTxUpdate()
 *
 * Overview:	Seperate TX/RX parameters update independent for TP detection and
 *			dynamic TX/RX aggreagtion parameters update.
 *
 * Input:			PADAPTER
 *
 * Output/Return:	NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Seperate to smaller function.
 *
 *---------------------------------------------------------------------------*/
static void
usb_AggSettingTxUpdate(
		PADAPTER			Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32			value32;

	if (Adapter->registrypriv.wifi_spec)
		pHalData->UsbTxAggMode = false;

	if (pHalData->UsbTxAggMode){
		value32 = rtw_read32(Adapter, REG_TDECTRL);
		value32 = value32 & ~(BLK_DESC_NUM_MASK << BLK_DESC_NUM_SHIFT);
		value32 |= ((pHalData->UsbTxAggDescNum & BLK_DESC_NUM_MASK) << BLK_DESC_NUM_SHIFT);

		rtw_write32(Adapter, REG_TDECTRL, value32);
	}
}	/*  usb_AggSettingTxUpdate */


/*-----------------------------------------------------------------------------
 * Function:	usb_AggSettingRxUpdate()
 *
 * Overview:	Seperate TX/RX parameters update independent for TP detection and
 *			dynamic TX/RX aggreagtion parameters update.
 *
 * Input:			PADAPTER
 *
 * Output/Return:	NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Seperate to smaller function.
 *
 *---------------------------------------------------------------------------*/
static void
usb_AggSettingRxUpdate(
		PADAPTER			Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			valueDMA;
	u8			valueUSB;

	valueDMA = rtw_read8(Adapter, REG_TRXDMA_CTRL);
	valueUSB = rtw_read8(Adapter, REG_USB_SPECIAL_OPTION);

	switch (pHalData->UsbRxAggMode)
	{
		case USB_RX_AGG_DMA:
			valueDMA |= RXDMA_AGG_EN;
			valueUSB &= ~USB_AGG_EN;
			break;
		case USB_RX_AGG_USB:
			valueDMA &= ~RXDMA_AGG_EN;
			valueUSB |= USB_AGG_EN;
			break;
		case USB_RX_AGG_MIX:
			valueDMA |= RXDMA_AGG_EN;
			valueUSB |= USB_AGG_EN;
			break;
		case USB_RX_AGG_DISABLE:
		default:
			valueDMA &= ~RXDMA_AGG_EN;
			valueUSB &= ~USB_AGG_EN;
			break;
	}

	rtw_write8(Adapter, REG_TRXDMA_CTRL, valueDMA);
	rtw_write8(Adapter, REG_USB_SPECIAL_OPTION, valueUSB);

	switch (pHalData->UsbRxAggMode)
	{
		case USB_RX_AGG_DMA:
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, pHalData->UsbRxAggPageCount);
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH+1, pHalData->UsbRxAggPageTimeout);
			break;
		case USB_RX_AGG_USB:
			rtw_write8(Adapter, REG_USB_AGG_TH, pHalData->UsbRxAggBlockCount);
			rtw_write8(Adapter, REG_USB_AGG_TO, pHalData->UsbRxAggBlockTimeout);
			break;
		case USB_RX_AGG_MIX:
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, pHalData->UsbRxAggPageCount);
			rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH+1, (pHalData->UsbRxAggPageTimeout& 0x1F));/* 0x280[12:8] */

			rtw_write8(Adapter, REG_USB_AGG_TH, pHalData->UsbRxAggBlockCount);
			rtw_write8(Adapter, REG_USB_AGG_TO, pHalData->UsbRxAggBlockTimeout);

			break;
		case USB_RX_AGG_DISABLE:
		default:
			/*  TODO: */
			break;
	}

	switch (PBP_128)
	{
		case PBP_128:
			pHalData->HwRxPageSize = 128;
			break;
		case PBP_64:
			pHalData->HwRxPageSize = 64;
			break;
		case PBP_256:
			pHalData->HwRxPageSize = 256;
			break;
		case PBP_512:
			pHalData->HwRxPageSize = 512;
			break;
		case PBP_1024:
			pHalData->HwRxPageSize = 1024;
			break;
		default:
			break;
	}
}	/*  usb_AggSettingRxUpdate */

static void
InitUsbAggregationSetting(
	PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	/*  Tx aggregation setting */
	usb_AggSettingTxUpdate(Adapter);

	/*  Rx aggregation setting */
	usb_AggSettingRxUpdate(Adapter);

	/*  201/12/10 MH Add for USB agg mode dynamic switch. */
	pHalData->UsbRxHighSpeedMode = false;
}
static void
HalRxAggr8188EUsb(
	PADAPTER Adapter,
	bool	Value
	)
{
}

/*-----------------------------------------------------------------------------
 * Function:	USB_AggModeSwitch()
 *
 * Overview:	When RX traffic is more than 40M, we need to adjust some parameters to increase
 *			RX speed by increasing batch indication size. This will decrease TCP ACK speed, we
 *			need to monitor the influence of FTP/network share.
 *			For TX mode, we are still ubder investigation.
 *
 * Input:		PADAPTER
 *
 * Output:		NONE
 *
 * Return:		NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	12/10/2010	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static void
USB_AggModeSwitch(
		PADAPTER			Adapter
	)
{
}	/*  USB_AggModeSwitch */

static void
_InitOperationMode(
		PADAPTER			Adapter
	)
{
}


 static void
_InitBeaconParameters(
	PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	rtw_write16(Adapter, REG_BCN_CTRL, 0x1010);

	/*  TODO: Remove these magic number */
	rtw_write16(Adapter, REG_TBTT_PROHIBIT,0x6404);/*  ms */
	rtw_write8(Adapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME);/*  5ms */
	rtw_write8(Adapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME); /*  2ms */

	/*  Suggested by designer timchen. Change beacon AIFS to the largest number */
	/*  beacause test chip does not contension before sending beacon. by tynli. 2009.11.03 */
	rtw_write16(Adapter, REG_BCNTCFG, 0x660F);

	pHalData->RegBcnCtrlVal = rtw_read8(Adapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(Adapter, REG_TXPAUSE);
	pHalData->RegFwHwTxQCtrl = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL+2);
	pHalData->RegReg542 = rtw_read8(Adapter, REG_TBTT_PROHIBIT+2);
	pHalData->RegCR_1 = rtw_read8(Adapter, REG_CR+1);
}

static void
_InitRFType(
		PADAPTER Adapter
	)
{
	struct registry_priv	 *pregpriv = &Adapter->registrypriv;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);
	bool			is92CU		= IS_92C_SERIAL(pHalData->VersionID);

#if	DISABLE_BB_RF
	pHalData->rf_chip	= RF_PSEUDO_11N;
	return;
#endif

	pHalData->rf_chip	= RF_6052;

	if (false == is92CU){
		pHalData->rf_type = RF_1T1R;
		DBG_88E("Set RF Chip ID to RF_6052 and RF type to 1T1R.\n");
		return;
	}

	/*  TODO: Consider that EEPROM set 92CU to 1T1R later. */
	/*  Force to overwrite setting according to chip version. Ignore EEPROM setting. */
	MSG_88E("Set RF Chip ID to RF_6052 and RF type to %d.\n", pHalData->rf_type);

}


static void
_BeaconFunctionEnable(
		PADAPTER		Adapter,
		bool			Enable,
		bool			Linked
	)
{
	rtw_write8(Adapter, REG_BCN_CTRL, (BIT4 | BIT3 | BIT1));

	rtw_write8(Adapter, REG_RD_CTRL+1, 0x6F);
}


/*  Set CCK and OFDM Block "ON" */
static void _BBTurnOnBlock(
		PADAPTER		Adapter
	)
{
#if (DISABLE_BB_RF)
	return;
#endif

	PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bCCKEn, 0x1);
	PHY_SetBBReg(Adapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
}

static void _RfPowerSave(
		PADAPTER		Adapter
	)
{
}

enum {
	Antenna_Lfet = 1,
	Antenna_Right = 2,
};

static void
_InitAntenna_Selection(	PADAPTER Adapter)
{

	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	if (pHalData->AntDivCfg==0)
		return;
	DBG_88E("==>  %s ....\n",__func__);

	rtw_write32(Adapter, REG_LEDCFG0, rtw_read32(Adapter, REG_LEDCFG0)|BIT23);
	PHY_SetBBReg(Adapter, rFPGA0_XAB_RFParameter, BIT13, 0x01);

	if (PHY_QueryBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300) == Antenna_A)
		pHalData->CurAntenna = Antenna_A;
	else
		pHalData->CurAntenna = Antenna_B;
	DBG_88E("%s,Cur_ant:(%x)%s\n",__func__,pHalData->CurAntenna,(pHalData->CurAntenna == Antenna_A)?"Antenna_A":"Antenna_B");


}

/*  */
/*  2010/08/26 MH Add for selective suspend mode check. */
/*  If Efuse 0x0e bit1 is not enabled, we can not support selective suspend for Minicard and */
/*  slim card. */
/*  */
static void
HalDetectSelectiveSuspendMode(
	PADAPTER				Adapter
	)
{
}	/*  HalDetectSelectiveSuspendMode */
/*-----------------------------------------------------------------------------
 * Function:	HwSuspendModeEnable92Cu()
 *
 * Overview:	HW suspend mode switch.
 *
 * Input:		NONE
 *
 * Output:	NONE
 *
 * Return:	NONE
 *
 * Revised History:
 *	When		Who		Remark
 *	08/23/2010	MHC		HW suspend mode switch test..
 *---------------------------------------------------------------------------*/
static void
HwSuspendModeEnable_88eu(
		PADAPTER	pAdapter,
		u8			Type
	)
{
}	/*  HwSuspendModeEnable92Cu */

rt_rf_power_state RfOnOffDetect(	PADAPTER pAdapter )
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(pAdapter);
	u8	val8;
	rt_rf_power_state rfpowerstate = rf_off;

	if (pAdapter->pwrctrlpriv.bHWPowerdown)
	{
		val8 = rtw_read8(pAdapter, REG_HSISR);
		DBG_88E("pwrdown, 0x5c(BIT7)=%02x\n", val8);
		rfpowerstate = (val8 & BIT7) ? rf_off: rf_on;
	}
	else /*  rf on/off */
	{
		rtw_write8(	pAdapter, REG_MAC_PINMUX_CFG,rtw_read8(pAdapter, REG_MAC_PINMUX_CFG)&~(BIT3));
		val8 = rtw_read8(pAdapter, REG_GPIO_IO_SEL);
		DBG_88E("GPIO_IN=%02x\n", val8);
		rfpowerstate = (val8 & BIT3) ? rf_on : rf_off;
	}
	return rfpowerstate;
}	/*  HalDetectPwrDownMode */

void _ps_open_RF(_adapter *padapter);

static u32 rtl8188eu_hal_init(PADAPTER Adapter)
{
	u8	value8 = 0;
	u16  value16;
	u8	txpktbuf_bndy;
	u32	status = _SUCCESS;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv		*pwrctrlpriv = &Adapter->pwrctrlpriv;
	struct registry_priv	*pregistrypriv = &Adapter->registrypriv;

	rt_rf_power_state		eRfPowerStateToSet;
#ifdef CONFIG_BT_COEXIST
	struct btcoexist_priv	*pbtpriv = &(pHalData->bt_coexist);
#endif

	u32 init_start_time = rtw_get_current_time();


#ifdef DBG_HAL_INIT_PROFILING

	enum HAL_INIT_STAGES {
		HAL_INIT_STAGES_BEGIN = 0,
		HAL_INIT_STAGES_INIT_PW_ON,
		HAL_INIT_STAGES_MISC01,
		HAL_INIT_STAGES_DOWNLOAD_FW,
		HAL_INIT_STAGES_MAC,
		HAL_INIT_STAGES_BB,
		HAL_INIT_STAGES_RF,
		HAL_INIT_STAGES_EFUSE_PATCH,
		HAL_INIT_STAGES_INIT_LLTT,

		HAL_INIT_STAGES_MISC02,
		HAL_INIT_STAGES_TURN_ON_BLOCK,
		HAL_INIT_STAGES_INIT_SECURITY,
		HAL_INIT_STAGES_MISC11,
		HAL_INIT_STAGES_INIT_HAL_DM,
		/* HAL_INIT_STAGES_RF_PS, */
		HAL_INIT_STAGES_IQK,
		HAL_INIT_STAGES_PW_TRACK,
		HAL_INIT_STAGES_LCK,
		/* HAL_INIT_STAGES_MISC21, */
		/* HAL_INIT_STAGES_INIT_PABIAS, */
		#ifdef CONFIG_BT_COEXIST
		HAL_INIT_STAGES_BT_COEXIST,
		#endif
		/* HAL_INIT_STAGES_ANTENNA_SEL, */
		/* HAL_INIT_STAGES_MISC31, */
		HAL_INIT_STAGES_END,
		HAL_INIT_STAGES_NUM
	};

	char * hal_init_stages_str[] = {
		"HAL_INIT_STAGES_BEGIN",
		"HAL_INIT_STAGES_INIT_PW_ON",
		"HAL_INIT_STAGES_MISC01",
		"HAL_INIT_STAGES_DOWNLOAD_FW",
		"HAL_INIT_STAGES_MAC",
		"HAL_INIT_STAGES_BB",
		"HAL_INIT_STAGES_RF",
		"HAL_INIT_STAGES_EFUSE_PATCH",
		"HAL_INIT_STAGES_INIT_LLTT",
		"HAL_INIT_STAGES_MISC02",
		"HAL_INIT_STAGES_TURN_ON_BLOCK",
		"HAL_INIT_STAGES_INIT_SECURITY",
		"HAL_INIT_STAGES_MISC11",
		"HAL_INIT_STAGES_INIT_HAL_DM",
		/* HAL_INIT_STAGES_RF_PS", */
		"HAL_INIT_STAGES_IQK",
		"HAL_INIT_STAGES_PW_TRACK",
		"HAL_INIT_STAGES_LCK",
		/* HAL_INIT_STAGES_MISC21", */
		#ifdef CONFIG_BT_COEXIST
		"HAL_INIT_STAGES_BT_COEXIST",
		#endif
		/* HAL_INIT_STAGES_ANTENNA_SEL", */
		/* HAL_INIT_STAGES_MISC31", */
		"HAL_INIT_STAGES_END",
	};

	int hal_init_profiling_i;
	u32 hal_init_stages_timestamp[HAL_INIT_STAGES_NUM]; /* used to record the time of each stage's starting point */

	for (hal_init_profiling_i=0;hal_init_profiling_i<HAL_INIT_STAGES_NUM;hal_init_profiling_i++)
		hal_init_stages_timestamp[hal_init_profiling_i]=0;

	#define HAL_INIT_PROFILE_TAG(stage) hal_init_stages_timestamp[(stage)]=rtw_get_current_time();
#else
	#define HAL_INIT_PROFILE_TAG(stage) do {} while (0)
#endif /* DBG_HAL_INIT_PROFILING */



_func_enter_;

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BEGIN);

#ifdef CONFIG_WOWLAN

	Adapter->pwrctrlpriv.wowlan_wake_reason = rtw_read8(Adapter, REG_WOWLAN_WAKE_REASON);
	DBG_88E("%s wowlan_wake_reason: 0x%02x\n",
				__func__, Adapter->pwrctrlpriv.wowlan_wake_reason);

	if (rtw_read8(Adapter, REG_MCUFWDL)&BIT7){ /*&&
		(Adapter->pwrctrlpriv.wowlan_wake_reason & FWDecisionDisconnect)) {*/
		u8 reg_val=0;
		DBG_88E("+Reset Entry+\n");
		rtw_write8(Adapter, REG_MCUFWDL, 0x00);
		_8051Reset88E(Adapter);
		/* reset BB */
		reg_val = rtw_read8(Adapter, REG_SYS_FUNC_EN);
		reg_val &= ~(BIT(0) | BIT(1));
		rtw_write8(Adapter, REG_SYS_FUNC_EN, reg_val);
		/* reset RF */
		rtw_write8(Adapter, REG_RF_CTRL, 0);
		/* reset TRX path */
		rtw_write16(Adapter, REG_CR, 0);
		/* reset MAC, Digital Core */
		reg_val = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
		reg_val &= ~(BIT(4) | BIT(7));
		rtw_write8(Adapter, REG_SYS_FUNC_EN+1, reg_val);
		reg_val = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
		reg_val |= BIT(4) | BIT(7);
		rtw_write8(Adapter, REG_SYS_FUNC_EN+1, reg_val);
		DBG_88E("-Reset Entry-\n");
	}
#endif /* CONFIG_WOWLAN */

	if (Adapter->pwrctrlpriv.bkeepfwalive)
	{
		_ps_open_RF(Adapter);

		if (pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized){
			PHY_IQCalibrate_8188E(Adapter,true);
		}
		else
		{
			PHY_IQCalibrate_8188E(Adapter,false);
			pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized = true;
		}

		ODM_TXPowerTrackingCheck(&pHalData->odmpriv );
		PHY_LCCalibrate_8188E(Adapter);

		goto exit;
	}

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PW_ON);
	status = rtl8188eu_InitPowerOn(Adapter);
	if (status == _FAIL){
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init power on!\n"));
		goto exit;
	}

	/*  Save target channel */
	pHalData->CurrentChannel = 6;/* default set to 6 */


	if (pwrctrlpriv->reg_rfoff == true){
		pwrctrlpriv->rf_pwrstate = rf_off;
	}

	/*  2010/08/09 MH We need to check if we need to turnon or off RF after detecting */
	/*  HW GPIO pin. Before PHY_RFConfig8192C. */
	/*  2010/08/26 MH If Efuse does not support sective suspend then disable the function. */

	if (!pregistrypriv->wifi_spec) {
		txpktbuf_bndy = TX_PAGE_BOUNDARY_88E;
	} else {
		/*  for WMM */
		txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY_88E;
	}

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC01);
	_InitQueueReservedPage(Adapter);
	_InitQueuePriority(Adapter);
	_InitPageBoundary(Adapter);
	_InitTransferPageSize(Adapter);

	_InitTxBufferBoundary(Adapter, 0);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_DOWNLOAD_FW);
#if (MP_DRIVER == 1)
	if (Adapter->registrypriv.mp_mode == 1)
	{
		_InitRxSetting(Adapter);
		Adapter->bFWReady = false;
		pHalData->fw_ractrl = false;
	}
	else
#endif  /* MP_DRIVER == 1 */
	{
#ifdef CONFIG_WOWLAN
	status = rtl8188e_FirmwareDownload(Adapter, false);
#else
		status = rtl8188e_FirmwareDownload(Adapter);
#endif /* CONFIG_WOWLAN */

		if (status != _SUCCESS) {
			DBG_88E("%s: Download Firmware failed!!\n", __func__);
			Adapter->bFWReady = false;
			pHalData->fw_ractrl = false;
			return status;
		} else {
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Initializepadapter8192CSdio(): Download Firmware Success!!\n"));
			Adapter->bFWReady = true;
			pHalData->fw_ractrl = false;
		}
	}
	rtl8188e_InitializeFirmwareVars(Adapter);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MAC);
#if (HAL_MAC_ENABLE == 1)
	status = PHY_MACConfig8188E(Adapter);
	if (status == _FAIL)
	{
		DBG_88E(" ### Failed to init MAC ......\n ");
		goto exit;
	}
#endif

	/*  */
	/* d. Initialize BB related configurations. */
	/*  */
	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_BB);
#if (HAL_BB_ENABLE == 1)
	status = PHY_BBConfig8188E(Adapter);
	if (status == _FAIL)
	{
		DBG_88E(" ### Failed to init BB ......\n ");
		goto exit;
	}
#endif


	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_RF);
#if (HAL_RF_ENABLE == 1)
	status = PHY_RFConfig8188E(Adapter);
	if (status == _FAIL)
	{
		DBG_88E(" ### Failed to init RF ......\n ");
		goto exit;
	}
#endif

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_EFUSE_PATCH);
	status = rtl8188e_iol_efuse_patch(Adapter);
	if (status == _FAIL){
		DBG_88E("%s  rtl8188e_iol_efuse_patch failed\n",__func__);
		goto exit;
	}

	_InitTxBufferBoundary(Adapter, txpktbuf_bndy);

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_LLTT);
	status =  InitLLTTable(Adapter, txpktbuf_bndy);
	if (status == _FAIL){
		RT_TRACE(_module_hci_hal_init_c_, _drv_err_, ("Failed to init LLT table\n"));
		goto exit;
	}

	HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC02);
	/*  Get Rx PHY status in order to report RSSI and others. */
	_InitDriverInfoSize(Adapter, DRVINFO_SZ);

	_InitInterrupt(Adapter);
	hal_init_macaddr(Adapter);/* set mac_address */
	_InitNetworkType(Adapter);/* set msr */
	_InitWMACSetting(Adapter);
	_InitAdaptiveCtrl(Adapter);
	_InitEDCA(Adapter);
	_InitRetryFunction(Adapter);
	InitUsbAggregationSetting(Adapter);
	_InitOperationMode(Adapter);/* todo */
	_InitBeaconParameters(Adapter);
	_InitBeaconMaxError(Adapter, true);

	/*  */
	/*  Init CR MACTXEN, MACRXEN after setting RxFF boundary REG_TRXFF_BNDY to patch */
	/*  Hw bug which Hw initials RxFF boundry size to a value which is larger than the real Rx buffer size in 88E. */
	/*  */
	/*  Enable MACTXEN/MACRXEN block */
	value16 = rtw_read16(Adapter, REG_CR);
	value16 |= (MACTXEN | MACRXEN);
	rtw_write8(Adapter, REG_CR, value16);

#if ENABLE_USB_DROP_INCORRECT_OUT
	_InitHardwareDropIncorrectBulkOut(Adapter);
#endif

	if (pHalData->bRDGEnable){
		_InitRDGSetting(Adapter);
	}

#if (RATE_ADAPTIVE_SUPPORT==1)
	{/* Enable TX Report */
		/* Enable Tx Report Timer */
		value8 = rtw_read8(Adapter, REG_TX_RPT_CTRL);
		rtw_write8(Adapter,  REG_TX_RPT_CTRL, (value8|BIT1|BIT0));
		/* Set MAX RPT MACID */
		rtw_write8(Adapter,  REG_TX_RPT_CTRL+1, 2);/* FOR sta mode ,0: bc/mc ,1:AP */
		/* Tx RPT Timer. Unit: 32us */
		rtw_write16(Adapter, REG_TX_RPT_TIME, 0xCdf0);
	}
#endif

	rtw_write8(Adapter, REG_EARLY_MODE_CONTROL, 0);

#ifdef CONFIG_CHECK_AC_LIFETIME
	/*  Enable lifetime check for the four ACs */
	rtw_write8(Adapter, REG_LIFETIME_EN, 0x0F);
#endif	/*  CONFIG_CHECK_AC_LIFETIME */

	rtw_write16(Adapter, REG_PKT_VO_VI_LIFE_TIME, 0x0400);	/*  unit: 256us. 256ms */
	rtw_write16(Adapter, REG_PKT_BE_BK_LIFE_TIME, 0x0400);	/*  unit: 256us. 256ms */

	_InitHWLed(Adapter);

	/*  */
	/*  Joseph Note: Keep RfRegChnlVal for later use. */
	/*  */
	pHalData->RfRegChnlVal[0] = PHY_QueryRFReg(Adapter, (RF_RADIO_PATH_E)0, RF_CHNLBW, bRFRegOffsetMask);
	pHalData->RfRegChnlVal[1] = PHY_QueryRFReg(Adapter, (RF_RADIO_PATH_E)1, RF_CHNLBW, bRFRegOffsetMask);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_TURN_ON_BLOCK);
	_BBTurnOnBlock(Adapter);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_SECURITY);
	invalidate_cam_all(Adapter);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_MISC11);
	/*  2010/12/17 MH We need to set TX power according to EFUSE content at first. */
	PHY_SetTxPowerLevel8188E(Adapter, pHalData->CurrentChannel);

/*  Move by Neo for USB SS to below setp */
/* _RfPowerSave(Adapter); */

	_InitAntenna_Selection(Adapter);

	/*  */
	/*  Disable BAR, suggested by Scott */
	/*  2010.04.09 add by hpfan */
	/*  */
	rtw_write32(Adapter, REG_BAR_MODE_CTRL, 0x0201ffff);

	/*  HW SEQ CTRL */
	/* set 0x0 to 0xFF by tynli. Default enable HW SEQ NUM. */
	rtw_write8(Adapter,REG_HWSEQ_CTRL, 0xFF);

	if (pregistrypriv->wifi_spec)
		rtw_write16(Adapter,REG_FAST_EDCA_CTRL ,0);

	/* Nav limit , suggest by scott */
	rtw_write8(Adapter, 0x652, 0x0);

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_HAL_DM);
	rtl8188e_InitHalDm(Adapter);

#if (MP_DRIVER == 1)
	if (Adapter->registrypriv.mp_mode == 1)
	{
		Adapter->mppriv.channel = pHalData->CurrentChannel;
		MPT_InitializeAdapter(Adapter, Adapter->mppriv.channel);
	}
	else
#endif  /* if (MP_DRIVER == 1) */
	{
	/*  */
	/*  2010/08/11 MH Merge from 8192SE for Minicard init. We need to confirm current radio status */
	/*  and then decide to enable RF or not.!!!??? For Selective suspend mode. We may not */
	/*  call init_adapter. May cause some problem?? */
	/*  */
	/*  Fix the bug that Hw/Sw radio off before S3/S4, the RF off action will not be executed */
	/*  in MgntActSet_RF_State() after wake up, because the value of pHalData->eRFPowerState */
	/*  is the same as eRfOff, we should change it to eRfOn after we config RF parameters. */
	/*  Added by tynli. 2010.03.30. */
	pwrctrlpriv->rf_pwrstate = rf_on;

	/*  enable Tx report. */
	rtw_write8(Adapter,  REG_FWHW_TXQ_CTRL+1, 0x0F);

	/*  Suggested by SD1 pisa. Added by tynli. 2011.10.21. */
	rtw_write8(Adapter, REG_EARLY_MODE_CONTROL+3, 0x01);/* Pretx_en, for WEP/TKIP SEC */

	/* tynli_test_tx_report. */
	rtw_write16(Adapter, REG_TX_RPT_TIME, 0x3DF0);

	/* enable tx DMA to drop the redundate data of packet */
	rtw_write16(Adapter,REG_TXDMA_OFFSET_CHK, (rtw_read16(Adapter,REG_TXDMA_OFFSET_CHK) | DROP_DATA_EN));

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_IQK);
	/*  2010/08/26 MH Merge from 8192CE. */
	if (pwrctrlpriv->rf_pwrstate == rf_on)
	{
		if (pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized){
			PHY_IQCalibrate_8188E(Adapter,true);
		}
		else
		{
			PHY_IQCalibrate_8188E(Adapter,false);
			pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized = true;
		}

HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_PW_TRACK);

		ODM_TXPowerTrackingCheck(&pHalData->odmpriv );


HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_LCK);
		PHY_LCCalibrate_8188E(Adapter);
	}
}

/* HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_INIT_PABIAS); */
/* 	_InitPABias(Adapter); */
	rtw_write8(Adapter, REG_USB_HRPWM, 0);

	/* ack for xmit mgmt frames. */
	rtw_write32(Adapter, REG_FWHW_TXQ_CTRL, rtw_read32(Adapter, REG_FWHW_TXQ_CTRL)|BIT(12));

exit:
HAL_INIT_PROFILE_TAG(HAL_INIT_STAGES_END);

	DBG_88E("%s in %dms\n", __func__, rtw_get_passing_time_ms(init_start_time));

	#ifdef DBG_HAL_INIT_PROFILING
	hal_init_stages_timestamp[HAL_INIT_STAGES_END]=rtw_get_current_time();

	for (hal_init_profiling_i=0;hal_init_profiling_i<HAL_INIT_STAGES_NUM-1;hal_init_profiling_i++) {
		DBG_88E("DBG_HAL_INIT_PROFILING: %35s, %u, %5u, %5u\n"
			, hal_init_stages_str[hal_init_profiling_i]
			, hal_init_stages_timestamp[hal_init_profiling_i]
			, (hal_init_stages_timestamp[hal_init_profiling_i+1]-hal_init_stages_timestamp[hal_init_profiling_i])
			, rtw_get_time_interval_ms(hal_init_stages_timestamp[hal_init_profiling_i], hal_init_stages_timestamp[hal_init_profiling_i+1])
		);
	}
	#endif


_func_exit_;

	return status;
}

void _ps_open_RF(_adapter *padapter) {
	/* here call with bRegSSPwrLvl 1, bRegSSPwrLvl 2 needs to be verified */
	/* phy_SsPwrSwitch92CU(padapter, rf_on, 1); */
}

static void _ps_close_RF(_adapter *padapter){
	/* here call with bRegSSPwrLvl 1, bRegSSPwrLvl 2 needs to be verified */
	/* phy_SsPwrSwitch92CU(padapter, rf_off, 1); */
}


static void CardDisableRTL8188EU(PADAPTER Adapter)
{
	u8	val8;
	u16	val16;
	u32	val32;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(Adapter);

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("CardDisableRTL8188EU\n"));

	/* Stop Tx Report Timer. 0x4EC[Bit1]=b'0 */
	val8 = rtw_read8(Adapter, REG_TX_RPT_CTRL);
	rtw_write8(Adapter, REG_TX_RPT_CTRL, val8&(~BIT1));

	/*  stop rx */
	rtw_write8(Adapter, REG_CR, 0x0);

	/*  Run LPS WL RFOFF flow */
	HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, Rtl8188E_NIC_LPS_ENTER_FLOW);


	/*  2. 0x1F[7:0] = 0		turn off RF */

	val8 = rtw_read8(Adapter, REG_MCUFWDL);
	if ((val8 & RAM_DL_SEL) && Adapter->bFWReady) /* 8051 RAM code */
	{
		/*  Reset MCU 0x2[10]=0. */
		val8 = rtw_read8(Adapter, REG_SYS_FUNC_EN+1);
		val8 &= ~BIT(2);	/*  0x2[10], FEN_CPUEN */
		rtw_write8(Adapter, REG_SYS_FUNC_EN+1, val8);
	}

	/*  reset MCU ready status */
	rtw_write8(Adapter, REG_MCUFWDL, 0);

	/* YJ,add,111212 */
	/* Disable 32k */
	val8 = rtw_read8(Adapter, REG_32K_CTRL);
	rtw_write8(Adapter, REG_32K_CTRL, val8&(~BIT0));

	/*  Card disable power action flow */
	HalPwrSeqCmdParsing(Adapter, PWR_CUT_ALL_MSK, PWR_FAB_ALL_MSK, PWR_INTF_USB_MSK, Rtl8188E_NIC_DISABLE_FLOW);

	/*  Reset MCU IO Wrapper */
	val8 = rtw_read8(Adapter, REG_RSV_CTRL+1);
	rtw_write8(Adapter, REG_RSV_CTRL+1, (val8&(~BIT3)));
	val8 = rtw_read8(Adapter, REG_RSV_CTRL+1);
	rtw_write8(Adapter, REG_RSV_CTRL+1, val8|BIT3);

	/* YJ,test add, 111207. For Power Consumption. */
	val8 = rtw_read8(Adapter, GPIO_IN);
	rtw_write8(Adapter, GPIO_OUT, val8);
	rtw_write8(Adapter, GPIO_IO_SEL, 0xFF);/* Reg0x46 */

	val8 = rtw_read8(Adapter, REG_GPIO_IO_SEL);
	rtw_write8(Adapter, REG_GPIO_IO_SEL, (val8<<4));
	val8 = rtw_read8(Adapter, REG_GPIO_IO_SEL+1);
	rtw_write8(Adapter, REG_GPIO_IO_SEL+1, val8|0x0F);/* Reg0x43 */
	rtw_write32(Adapter, REG_BB_PAD_CTRL, 0x00080808);/* set LNA ,TRSW,EX_PA Pin to output mode */
	pHalData->bMacPwrCtrlOn = false;
	Adapter->bFWReady = false;
}
static void rtl8192cu_hw_power_down(_adapter *padapter)
{
	/*  2010/-8/09 MH For power down module, we need to enable register block contrl reg at 0x1c. */
	/*  Then enable power down control bit of register 0x04 BIT4 and BIT15 as 1. */

	/*  Enable register area 0x0-0xc. */
	rtw_write8(padapter,REG_RSV_CTRL, 0x0);
	rtw_write16(padapter, REG_APS_FSMCO, 0x8812);
}

static u32 rtl8188eu_hal_deinit(PADAPTER Adapter)
{

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	DBG_88E("==> %s\n",__func__);

#ifdef CONFIG_SUPPORT_USB_INT
	rtw_write32(Adapter, REG_HIMR_88E, IMR_DISABLED_88E);
	rtw_write32(Adapter, REG_HIMRE_88E, IMR_DISABLED_88E);
#endif

	DBG_88E("bkeepfwalive(%x)\n",Adapter->pwrctrlpriv.bkeepfwalive);
	if (Adapter->pwrctrlpriv.bkeepfwalive)
	{
		_ps_close_RF(Adapter);
		if ((Adapter->pwrctrlpriv.bHWPwrPindetect) && (Adapter->pwrctrlpriv.bHWPowerdown))
			rtl8192cu_hw_power_down(Adapter);
	} else {
		if (Adapter->hw_init_completed == true){
			CardDisableRTL8188EU(Adapter);

			if ((Adapter->pwrctrlpriv.bHWPwrPindetect ) && (Adapter->pwrctrlpriv.bHWPowerdown))
				rtl8192cu_hw_power_down(Adapter);

		}
	}
	return _SUCCESS;
 }


static unsigned int rtl8188eu_inirp_init(PADAPTER Adapter)
{
	u8 i;
	struct recv_buf *precvbuf;
	uint	status;
	struct dvobj_priv *pdev= adapter_to_dvobj(Adapter);
	struct intf_hdl * pintfhdl=&Adapter->iopriv.intf;
	struct recv_priv *precvpriv = &(Adapter->recvpriv);
	u32 (*_read_port)(struct intf_hdl *pintfhdl, u32 addr, u32 cnt, u8 *pmem);

_func_enter_;

	_read_port = pintfhdl->io_ops._read_port;

	status = _SUCCESS;

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_,
		 ("===> usb_inirp_init\n"));

	precvpriv->ff_hwaddr = RECV_BULK_IN_ADDR;

	/* issue Rx irp to receive data */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++) {
		if (_read_port(pintfhdl, precvpriv->ff_hwaddr, 0, (unsigned char *)precvbuf) == false ) {
			RT_TRACE(_module_hci_hal_init_c_,_drv_err_,("usb_rx_init: usb_read_port error\n"));
			status = _FAIL;
			goto exit;
		}

		precvbuf++;
		precvpriv->free_recv_buf_queue_cnt--;
	}

exit:

	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("<=== usb_inirp_init\n"));

_func_exit_;

	return status;
}

static unsigned int rtl8188eu_inirp_deinit(PADAPTER Adapter)
{
	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("\n ===> usb_rx_deinit\n"));

	rtw_read_port_cancel(Adapter);

	RT_TRACE(_module_hci_hal_init_c_,_drv_info_,("\n <=== usb_rx_deinit\n"));

	return _SUCCESS;
}

/*  */
/*  */
/* 	EEPROM/EFUSE Content Parsing */
/*  */
/*  */
static void _ReadIDs(PADAPTER	Adapter, u8 *PROMContent, bool AutoloadFail)
{
}

static void
_ReadBoardType(
		PADAPTER	Adapter,
		u8*		PROMContent,
		bool		AutoloadFail
	)
{

}


static void
_ReadLEDSetting(
		PADAPTER	Adapter,
		u8*		PROMContent,
		bool		AutoloadFail
	)
{
	struct led_priv *pledpriv = &(Adapter->ledpriv);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	pledpriv->bRegUseLed = true;

	switch (pHalData->CustomerID)
	{
		default:
			pledpriv->LedStrategy = SW_LED_MODE1;
			break;
	}
	pHalData->bLedOpenDrain = true;/*  Support Open-drain arrangement for controlling the LED. Added by Roger, 2009.10.16. */
}

static void
_ReadThermalMeter(
		PADAPTER	Adapter,
		u8*	PROMContent,
		bool		AutoloadFail
	)
{
}

static void
_ReadRFSetting(
		PADAPTER	Adapter,
		u8*	PROMContent,
		bool		AutoloadFail
	)
{
}

static void
_ReadPROMVersion(
		PADAPTER	Adapter,
		u8*	PROMContent,
		bool		AutoloadFail
	)
{
}

static void
readAntennaDiversity(
		PADAPTER	pAdapter,
		u8			*hwinfo,
		bool		AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct registry_priv	*registry_par = &pAdapter->registrypriv;

	pHalData->AntDivCfg = registry_par->antdiv_cfg ;  /*  0:OFF , 1:ON, */
}

static void
hal_InitPGData(
		PADAPTER	pAdapter,
			u8		*PROMContent
	)
{
}
static void
Hal_EfuseParsePIDVID_8188EU(
		PADAPTER		pAdapter,
		u8*				hwinfo,
		bool			AutoLoadFail
	)
{

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if ( !AutoLoadFail )
	{
		/*  VID, PID */
		pHalData->EEPROMVID = EF2BYTE( *(__le16 *)&hwinfo[EEPROM_VID_88EU] );
		pHalData->EEPROMPID = EF2BYTE( *(__le16 *)&hwinfo[EEPROM_PID_88EU] );

		/*  Customer ID, 0x00 and 0xff are reserved for Realtek. */
		pHalData->EEPROMCustomerID = *(u8 *)&hwinfo[EEPROM_CUSTOMERID_88E];
		pHalData->EEPROMSubCustomerID = EEPROM_Default_SubCustomerID;

	}
	else
	{
		pHalData->EEPROMVID			= EEPROM_Default_VID;
		pHalData->EEPROMPID			= EEPROM_Default_PID;

		/*  Customer ID, 0x00 and 0xff are reserved for Realtek. */
		pHalData->EEPROMCustomerID		= EEPROM_Default_CustomerID;
		pHalData->EEPROMSubCustomerID	= EEPROM_Default_SubCustomerID;

	}

	DBG_88E("VID = 0x%04X, PID = 0x%04X\n", pHalData->EEPROMVID, pHalData->EEPROMPID);
	DBG_88E("Customer ID: 0x%02X, SubCustomer ID: 0x%02X\n", pHalData->EEPROMCustomerID, pHalData->EEPROMSubCustomerID);
}

static void
Hal_EfuseParseMACAddr_8188EU(
		PADAPTER		padapter,
		u8*			hwinfo,
		bool			AutoLoadFail
	)
{
	u16			i, usValue;
	u8			sMacAddr[6] = {0x00, 0xE0, 0x4C, 0x81, 0x88, 0x02};
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	if (AutoLoadFail)
	{
/* 		sMacAddr[5] = (u1Byte)GetRandomNumber(1, 254); */
		for (i=0; i<6; i++)
			pEEPROM->mac_addr[i] = sMacAddr[i];
	}
	else
	{
		/* Read Permanent MAC address */
		_rtw_memcpy(pEEPROM->mac_addr, &hwinfo[EEPROM_MAC_ADDR_88EU], ETH_ALEN);

	}

	RT_TRACE(_module_hci_hal_init_c_, _drv_notice_,
			("Hal_EfuseParseMACAddr_8188EU: Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
		  pEEPROM->mac_addr[0], pEEPROM->mac_addr[1],
		  pEEPROM->mac_addr[2], pEEPROM->mac_addr[3],
		  pEEPROM->mac_addr[4], pEEPROM->mac_addr[5]));
}


static void
Hal_CustomizeByCustomerID_8188EU(
		PADAPTER		padapter
	)
{
}

/*  Read HW power down mode selection */
static void _ReadPSSetting(PADAPTER Adapter, u8 *PROMContent, u8 AutoloadFail)
{
}

#ifdef CONFIG_EFUSE_CONFIG_FILE
static u32 Hal_readPGDataFromConfigFile(
	PADAPTER	padapter)
{
	u32 i;
	struct file *fp;
	mm_segment_t fs;
	u8 temp[3];
	loff_t pos = 0;
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u8	*PROMContent = pEEPROM->efuse_eeprom_data;


	temp[2] = 0; /*  add end of string '\0' */

	fp = filp_open("/system/etc/wifi/wifi_efuse.map", O_RDWR,  0644);
	if (IS_ERR(fp)) {
		pEEPROM->bloadfile_fail_flag = true;
		DBG_88E("Error, Efuse configure file doesn't exist.\n");
		return _FAIL;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);

	DBG_88E("Efuse configure file:\n");
	for (i=0; i<HWSET_MAX_SIZE_88E; i++) {
		vfs_read(fp, temp, 2, &pos);
		PROMContent[i] = simple_strtoul(temp, NULL, 16 );
		pos += 1; /*  Filter the space character */
		DBG_88E("%02X\n", PROMContent[i]);
	}
	DBG_88E("\n");
	set_fs(fs);

	filp_close(fp, NULL);

	pEEPROM->bloadfile_fail_flag = false;

	return _SUCCESS;
}

static void
Hal_ReadMACAddrFromFile_8188EU(
	PADAPTER		padapter
	)
{
	u32 i;
	struct file *fp;
	mm_segment_t fs;
	u8 source_addr[18];
	loff_t pos = 0;
	u32 curtime = rtw_get_current_time();
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u8 *head, *end;

	u8 null_mac_addr[ETH_ALEN] = {0, 0, 0,0, 0, 0};
	u8 multi_mac_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	_rtw_memset(source_addr, 0, 18);
	_rtw_memset(pEEPROM->mac_addr, 0, ETH_ALEN);

	fp = filp_open("/data/wifimac.txt", O_RDWR,  0644);
	if (IS_ERR(fp)) {
		pEEPROM->bloadmac_fail_flag = true;
		DBG_88E("Error, wifi mac address file doesn't exist.\n");
	} else {
		fs = get_fs();
		set_fs(KERNEL_DS);

		DBG_88E("wifi mac address:\n");
		vfs_read(fp, source_addr, 18, &pos);
		source_addr[17] = ':';

		head = end = source_addr;
		for (i=0; i<ETH_ALEN; i++) {
			while (end && (*end != ':') )
				end++;

			if (end && (*end == ':') )
				*end = '\0';

			pEEPROM->mac_addr[i] = simple_strtoul(head, NULL, 16 );

			if (end) {
				end++;
				head = end;
			}
			DBG_88E("%02x\n", pEEPROM->mac_addr[i]);
		}
		DBG_88E("\n");
		set_fs(fs);
		pEEPROM->bloadmac_fail_flag = false;
		filp_close(fp, NULL);
	}

	if ( (_rtw_memcmp(pEEPROM->mac_addr, null_mac_addr, ETH_ALEN)) ||
		(_rtw_memcmp(pEEPROM->mac_addr, multi_mac_addr, ETH_ALEN)) ) {
		pEEPROM->mac_addr[0] = 0x00;
		pEEPROM->mac_addr[1] = 0xe0;
		pEEPROM->mac_addr[2] = 0x4c;
		pEEPROM->mac_addr[3] = (u8)(curtime & 0xff) ;
		pEEPROM->mac_addr[4] = (u8)((curtime>>8) & 0xff) ;
		pEEPROM->mac_addr[5] = (u8)((curtime>>16) & 0xff) ;
	}

	DBG_88E("Hal_ReadMACAddrFromFile_8188ES: Permanent Address = %02x-%02x-%02x-%02x-%02x-%02x\n",
		  pEEPROM->mac_addr[0], pEEPROM->mac_addr[1],
		  pEEPROM->mac_addr[2], pEEPROM->mac_addr[3],
		  pEEPROM->mac_addr[4], pEEPROM->mac_addr[5]);
}
#endif /* CONFIG_EFUSE_CONFIG_FILE */

static void
readAdapterInfo_8188EU(
		PADAPTER	padapter
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	/* parse the eeprom/efuse content */
	Hal_EfuseParseIDCode88E(padapter, pEEPROM->efuse_eeprom_data);
	Hal_EfuseParsePIDVID_8188EU(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
#ifdef CONFIG_EFUSE_CONFIG_FILE
	Hal_ReadMACAddrFromFile_8188EU(padapter);
#else /* CONFIG_EFUSE_CONFIG_FILE */
	Hal_EfuseParseMACAddr_8188EU(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
#endif /* CONFIG_EFUSE_CONFIG_FILE */

	Hal_ReadPowerSavingMode88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_ReadTxPowerInfo88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseEEPROMVer88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	rtl8188e_EfuseParseChnlPlan(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseXtal_8188E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseCustomerID88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_ReadAntennaDiversity88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_EfuseParseBoardType88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
	Hal_ReadThermalMeter_88E(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);

	/*  */
	/*  The following part initialize some vars by PG info. */
	/*  */
	Hal_InitChannelPlan(padapter);
	Hal_CustomizeByCustomerID_8188EU(padapter);

	_ReadLEDSetting(padapter, pEEPROM->efuse_eeprom_data, pEEPROM->bautoload_fail_flag);
}

static void _ReadPROMContent(
	PADAPTER		Adapter
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
	u8			eeValue;

	/* check system boot selection */
	eeValue = rtw_read8(Adapter, REG_9346CR);
	pEEPROM->EepromOrEfuse		= (eeValue & BOOT_FROM_EEPROM) ? true : false;
	pEEPROM->bautoload_fail_flag	= (eeValue & EEPROM_EN) ? false : true;


	DBG_88E("Boot from %s, Autoload %s !\n", (pEEPROM->EepromOrEfuse ? "EEPROM" : "EFUSE"),
				(pEEPROM->bautoload_fail_flag ? "Fail" : "OK") );

#ifdef CONFIG_EFUSE_CONFIG_FILE
	Hal_readPGDataFromConfigFile(Adapter);
#else /* CONFIG_EFUSE_CONFIG_FILE */
	Hal_InitPGData88E(Adapter);
#endif	/* CONFIG_EFUSE_CONFIG_FILE */
	readAdapterInfo_8188EU(Adapter);
}



static void
_ReadRFType(
		PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

#if DISABLE_BB_RF
	pHalData->rf_chip = RF_PSEUDO_11N;
#else
	pHalData->rf_chip = RF_6052;
#endif
}

static int _ReadAdapterInfo8188EU(PADAPTER	Adapter)
{
	u32 start=rtw_get_current_time();

	MSG_88E("====> %s\n", __func__);

	_ReadRFType(Adapter);/* rf_chip -> _InitRFType() */
	_ReadPROMContent(Adapter);

	MSG_88E("<==== %s in %d ms\n", __func__, rtw_get_passing_time_ms(start));

	return _SUCCESS;
}


static void ReadAdapterInfo8188EU(PADAPTER Adapter)
{
	/*  Read EEPROM size before call any EEPROM function */
	Adapter->EepromAddressSize = GetEEPROMSize8188E(Adapter);

	_ReadAdapterInfo8188EU(Adapter);
}


#define GPIO_DEBUG_PORT_NUM 0
static void rtl8192cu_trigger_gpio_0(_adapter *padapter)
{
}

static void ResumeTxBeacon(_adapter *padapter)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(padapter);

	/*  2010.03.01. Marked by tynli. No need to call workitem beacause we record the value */
	/*  which should be read from register to a global variable. */

	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) | BIT6);
	pHalData->RegFwHwTxQCtrl |= BIT6;
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0xff);
	pHalData->RegReg542 |= BIT0;
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);
}

static void UpdateInterruptMask8188EU(PADAPTER padapter,u8 bHIMR0 ,u32 AddMSR, u32 RemoveMSR)
{
	HAL_DATA_TYPE *pHalData;

	u32 *himr;
	pHalData = GET_HAL_DATA(padapter);

	if (bHIMR0)
		himr = &(pHalData->IntrMask[0]);
	else
		himr = &(pHalData->IntrMask[1]);

	if (AddMSR)
		*himr |= AddMSR;

	if (RemoveMSR)
		*himr &= (~RemoveMSR);

	if (bHIMR0)
		rtw_write32(padapter, REG_HIMR_88E, *himr);
	else
		rtw_write32(padapter, REG_HIMRE_88E, *himr);

}

static void StopTxBeacon(_adapter *padapter)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(padapter);

	/*  2010.03.01. Marked by tynli. No need to call workitem beacause we record the value */
	/*  which should be read from register to a global variable. */

	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) & (~BIT6));
	pHalData->RegFwHwTxQCtrl &= (~BIT6);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0x64);
	pHalData->RegReg542 &= ~(BIT0);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);

	 /* todo: CheckFwRsvdPageContent(Adapter);  2010.06.23. Added by tynli. */

}


static void hw_var_set_opmode(PADAPTER Adapter, u8 variable, u8* val)
{
	u8	val8;
	u8	mode = *((u8 *)val);

	{
		/*  disable Port0 TSF update */
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));

		/*  set net_type */
		val8 = rtw_read8(Adapter, MSR)&0x0c;
		val8 |= mode;
		rtw_write8(Adapter, MSR, val8);

		DBG_88E("%s()-%d mode = %d\n", __func__, __LINE__, mode);

		if ((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
			StopTxBeacon(Adapter);

			rtw_write8(Adapter,REG_BCN_CTRL, 0x19);/* disable atim wnd */
		}
		else if ((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
		{
			ResumeTxBeacon(Adapter);
			rtw_write8(Adapter,REG_BCN_CTRL, 0x1a);
		}
		else if (mode == _HW_STATE_AP_)
		{

			ResumeTxBeacon(Adapter);

			rtw_write8(Adapter, REG_BCN_CTRL, 0x12);

			/* Set RCR */
			rtw_write32(Adapter, REG_RCR, 0x7000208e);/* CBSSID_DATA must set to 0,reject ICV_ERR packet */
			/* enable to rx data frame */
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
			/* enable to rx ps-poll */
			rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

			/* Beacon Control related register for first time */
			rtw_write8(Adapter, REG_BCNDMATIM, 0x02); /*  2ms */

			rtw_write8(Adapter, REG_ATIMWND, 0x0a); /*  10ms */
			rtw_write16(Adapter, REG_BCNTCFG, 0x00);
			rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);/*  +32767 (~32ms) */

			/* reset TSF */
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

			/* BIT3 - If set 0, hw will clr bcnq when tx becon ok/fail or port 0 */
			rtw_write8(Adapter, REG_MBID_NUM, rtw_read8(Adapter, REG_MBID_NUM)|BIT(3)|BIT(4));

		        /* enable BCN0 Function for if1 */
			/* don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received) */
			rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT0_NORMAL_CHIP|EN_BCN_FUNCTION |BIT(1)));

			/* dis BCN1 ATIM  WND if if2 is station */
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(0));
		}
	}
}

static void hw_var_set_macaddr(PADAPTER Adapter, u8 variable, u8* val)
{
	u8 idx = 0;
	u32 reg_macid;

	reg_macid = REG_MACID;

	for (idx = 0 ; idx < 6; idx++)
		rtw_write8(Adapter, (reg_macid+idx), val[idx]);
}

static void hw_var_set_bssid(PADAPTER Adapter, u8 variable, u8* val)
{
	u8	idx = 0;
	u32 reg_bssid;

	reg_bssid = REG_BSSID;

	for (idx = 0 ; idx < 6; idx++)
		rtw_write8(Adapter, (reg_bssid+idx), val[idx]);
}

static void hw_var_set_bcn_func(PADAPTER Adapter, u8 variable, u8* val)
{
	u32 bcn_ctrl_reg;

	bcn_ctrl_reg = REG_BCN_CTRL;

	if (*((u8 *)val))
		rtw_write8(Adapter, bcn_ctrl_reg, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
	else
		rtw_write8(Adapter, bcn_ctrl_reg, rtw_read8(Adapter, bcn_ctrl_reg)&(~(EN_BCN_FUNCTION | EN_TXBCN_RPT)));
}

static void hw_var_set_correct_tsf(PADAPTER Adapter, u8 variable, u8* val)
{
}

static void hw_var_set_mlme_disconnect(PADAPTER Adapter, u8 variable, u8* val)
{
}

static void hw_var_set_mlme_sitesurvey(PADAPTER Adapter, u8 variable, u8* val)
{
}

static void hw_var_set_mlme_join(PADAPTER Adapter, u8 variable, u8* val)
{
}

static void SetHwReg8188EU(PADAPTER Adapter, u8 variable, u8* val)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DM_ODM_T		*podmpriv = &pHalData->odmpriv;
_func_enter_;

	switch (variable)
	{
		case HW_VAR_MEDIA_STATUS:
			{
				u8 val8;

				val8 = rtw_read8(Adapter, MSR)&0x0c;
				val8 |= *((u8 *)val);
				rtw_write8(Adapter, MSR, val8);
			}
			break;
		case HW_VAR_MEDIA_STATUS1:
			{
				u8 val8;

				val8 = rtw_read8(Adapter, MSR)&0x03;
				val8 |= *((u8 *)val) <<2;
				rtw_write8(Adapter, MSR, val8);
			}
			break;
		case HW_VAR_SET_OPMODE:
			hw_var_set_opmode(Adapter, variable, val);
			break;
		case HW_VAR_MAC_ADDR:
			hw_var_set_macaddr(Adapter, variable, val);
			break;
		case HW_VAR_BSSID:
			hw_var_set_bssid(Adapter, variable, val);
			break;
		case HW_VAR_BASIC_RATE:
			{
				u16			BrateCfg = 0;
				u8			RateIndex = 0;

				/*  2007.01.16, by Emily */
				/*  Select RRSR (in Legacy-OFDM and CCK) */
				/*  For 8190, we select only 24M, 12M, 6M, 11M, 5.5M, 2M, and 1M from the Basic rate. */
				/*  We do not use other rates. */
				HalSetBrateCfg( Adapter, val, &BrateCfg );
				DBG_88E("HW_VAR_BASIC_RATE: BrateCfg(%#x)\n", BrateCfg);

				/* 2011.03.30 add by Luke Lee */
				/* CCK 2M ACK should be disabled for some BCM and Atheros AP IOT */
				/* because CCK 2M has poor TXEVM */
				/* CCK 5.5M & 11M ACK should be enabled for better performance */

				pHalData->BasicRateSet = BrateCfg = (BrateCfg |0xd) & 0x15d;

				BrateCfg |= 0x01; /*  default enable 1M ACK rate */
				/*  Set RRSR rate table. */
				rtw_write8(Adapter, REG_RRSR, BrateCfg&0xff);
				rtw_write8(Adapter, REG_RRSR+1, (BrateCfg>>8)&0xff);
				rtw_write8(Adapter, REG_RRSR+2, rtw_read8(Adapter, REG_RRSR+2)&0xf0);

				/*  Set RTS initial rate */
				while (BrateCfg > 0x1)
				{
					BrateCfg = (BrateCfg>> 1);
					RateIndex++;
				}
				/*  Ziv - Check */
				rtw_write8(Adapter, REG_INIRTS_RATE_SEL, RateIndex);
			}
			break;
		case HW_VAR_TXPAUSE:
			rtw_write8(Adapter, REG_TXPAUSE, *((u8 *)val));
			break;
		case HW_VAR_BCN_FUNC:
			hw_var_set_bcn_func(Adapter, variable, val);
			break;
		case HW_VAR_CORRECT_TSF:
			{
				u64	tsf;
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

				tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval*1024)) -1024; /* us */

				if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
				{
					StopTxBeacon(Adapter);
				}

				/* disable related TSF function */
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(3)));

				rtw_write32(Adapter, REG_TSFTR, tsf);
				rtw_write32(Adapter, REG_TSFTR+4, tsf>>32);

				/* enable related TSF function */
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(3));


				if (((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
				{
					ResumeTxBeacon(Adapter);
				}
			}
			break;
		case HW_VAR_CHECK_BSSID:
			if (*((u8 *)val))
			{
				rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
			}
			else
			{
				u32	val32;

				val32 = rtw_read32(Adapter, REG_RCR);

				val32 &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);

				rtw_write32(Adapter, REG_RCR, val32);
			}
			break;
		case HW_VAR_MLME_DISCONNECT:
			{
				/* Set RCR to not to receive data frame when NO LINK state */
				/* reject all data frames */
				rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

				/* reset TSF */
				rtw_write8(Adapter, REG_DUAL_TSF_RST, (BIT(0)|BIT(1)));

				/* disable update TSF */
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));
			}
			break;
		case HW_VAR_MLME_SITESURVEY:
			if (*((u8 *)val))/* under sitesurvey */
			{
				/* config RCR to receive different BSSID & not to receive data frame */
				u32 v = rtw_read32(Adapter, REG_RCR);
				v &= ~(RCR_CBSSID_BCN);
				rtw_write32(Adapter, REG_RCR, v);
				/* reject all data frame */
				rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

				/* disable update TSF */
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));
			}
			else/* sitesurvey done */
			{
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

				if ((is_client_associated_to_ap(Adapter) == true) ||
					((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) )
				{
					/* enable to rx data frame */
					rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

					/* enable update TSF */
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));
				}
				else if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
				{
					rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

					/* enable update TSF */
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));
				}

				if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
					rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
				else
				{
					if (Adapter->in_cta_test)
					{
						u32 v = rtw_read32(Adapter, REG_RCR);
						v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN );/*  RCR_ADF */
						rtw_write32(Adapter, REG_RCR, v);
					}
					else
					{
						rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
					}
				}
			}
			break;
		case HW_VAR_MLME_JOIN:
			{
				u8	RetryLimit = 0x30;
				u8	type = *((u8 *)val);
				struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;

				if (type == 0) /*  prepare to join */
				{
					/* enable to rx data frame.Accept all data frame */
					rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

					if (Adapter->in_cta_test)
					{
						u32 v = rtw_read32(Adapter, REG_RCR);
						v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN );/*  RCR_ADF */
						rtw_write32(Adapter, REG_RCR, v);
					}
					else
					{
						rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
					}

					if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == true)
					{
						RetryLimit = (pHalData->CustomerID == RT_CID_CCX) ? 7 : 48;
					}
					else /*  Ad-hoc Mode */
					{
						RetryLimit = 0x7;
					}
				}
				else if (type == 1) /* joinbss_event call back when join res < 0 */
				{
					rtw_write16(Adapter, REG_RXFLTMAP2,0x00);
				}
				else if (type == 2) /* sta add event call back */
				{
					/* enable update TSF */
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));

					if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
					{
						RetryLimit = 0x7;
					}
				}

				rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
			}
			break;
		case HW_VAR_BEACON_INTERVAL:
			rtw_write16(Adapter, REG_BCN_INTERVAL, *((u16 *)val));
			break;
		case HW_VAR_SLOT_TIME:
			{
				u8	u1bAIFS, aSifsTime;
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

				rtw_write8(Adapter, REG_SLOT, val[0]);

				if (pmlmeinfo->WMM_enable == 0)
				{
					if ( pmlmeext->cur_wireless_mode == WIRELESS_11B)
						aSifsTime = 10;
					else
						aSifsTime = 16;

					u1bAIFS = aSifsTime + (2 * pmlmeinfo->slotTime);

					/*  <Roger_EXP> Temporary removed, 2008.06.20. */
					rtw_write8(Adapter, REG_EDCA_VO_PARAM, u1bAIFS);
					rtw_write8(Adapter, REG_EDCA_VI_PARAM, u1bAIFS);
					rtw_write8(Adapter, REG_EDCA_BE_PARAM, u1bAIFS);
					rtw_write8(Adapter, REG_EDCA_BK_PARAM, u1bAIFS);
				}
			}
			break;
		case HW_VAR_RESP_SIFS:
			/* RESP_SIFS for CCK */
			rtw_write8(Adapter, REG_R2T_SIFS, val[0]); /*  SIFS_T2T_CCK (0x08) */
			rtw_write8(Adapter, REG_R2T_SIFS+1, val[1]); /* SIFS_R2T_CCK(0x08) */
			/* RESP_SIFS for OFDM */
			rtw_write8(Adapter, REG_T2T_SIFS, val[2]); /* SIFS_T2T_OFDM (0x0a) */
			rtw_write8(Adapter, REG_T2T_SIFS+1, val[3]); /* SIFS_R2T_OFDM(0x0a) */
			break;
		case HW_VAR_ACK_PREAMBLE:
			{
				u8	regTmp;
				u8	bShortPreamble = *( (bool *)val );
				/*  Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily) */
				regTmp = (pHalData->nCur40MhzPrimeSC)<<5;
				if (bShortPreamble)
					regTmp |= 0x80;

				rtw_write8(Adapter, REG_RRSR+2, regTmp);
			}
			break;
		case HW_VAR_SEC_CFG:
			rtw_write8(Adapter, REG_SECCFG, *((u8 *)val));
			break;
		case HW_VAR_DM_FLAG:
			podmpriv->SupportAbility = *((u8 *)val);
			break;
		case HW_VAR_DM_FUNC_OP:
			if (val[0])
			{/*  save dm flag */
				podmpriv->BK_SupportAbility = podmpriv->SupportAbility;
			}
			else
			{/*  restore dm flag */
				podmpriv->SupportAbility = podmpriv->BK_SupportAbility;
			}
			break;
		case HW_VAR_DM_FUNC_SET:
			if (*((u32 *)val) == DYNAMIC_ALL_FUNC_ENABLE){
				pdmpriv->DMFlag = pdmpriv->InitDMFlag;
				podmpriv->SupportAbility =	pdmpriv->InitODMFlag;
			}
			else{
				podmpriv->SupportAbility |= *((u32 *)val);
			}
			break;
		case HW_VAR_DM_FUNC_CLR:
			podmpriv->SupportAbility &= *((u32 *)val);
			break;

		case HW_VAR_CAM_EMPTY_ENTRY:
			{
				u8	ucIndex = *((u8 *)val);
				u8	i;
				u32	ulCommand=0;
				u32	ulContent=0;
				u32	ulEncAlgo=CAM_AES;

				for (i=0;i<CAM_CONTENT_COUNT;i++)
				{
					/*  filled id in CAM config 2 byte */
					if ( i == 0)
					{
						ulContent |=(ucIndex & 0x03) | ((u16)(ulEncAlgo)<<2);
						/* ulContent |= CAM_VALID; */
					}
					else
					{
						ulContent = 0;
					}
					/*  polling bit, and No Write enable, and address */
					ulCommand= CAM_CONTENT_COUNT*ucIndex+i;
					ulCommand= ulCommand | CAM_POLLINIG|CAM_WRITE;
					/*  write content 0 is equall to mark invalid */
					rtw_write32(Adapter, WCAMI, ulContent);  /* delay_ms(40); */
					rtw_write32(Adapter, RWCAM, ulCommand);  /* delay_ms(40); */
				}
			}
			break;
		case HW_VAR_CAM_INVALID_ALL:
			rtw_write32(Adapter, RWCAM, BIT(31)|BIT(30));
			break;
		case HW_VAR_CAM_WRITE:
			{
				u32	cmd;
				u32	*cam_val = (u32 *)val;
				rtw_write32(Adapter, WCAMI, cam_val[0]);

				cmd = CAM_POLLINIG | CAM_WRITE | cam_val[1];
				rtw_write32(Adapter, RWCAM, cmd);
			}
			break;
		case HW_VAR_AC_PARAM_VO:
			rtw_write32(Adapter, REG_EDCA_VO_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_AC_PARAM_VI:
			rtw_write32(Adapter, REG_EDCA_VI_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_AC_PARAM_BE:
			pHalData->AcParam_BE = ((u32 *)(val))[0];
			rtw_write32(Adapter, REG_EDCA_BE_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_AC_PARAM_BK:
			rtw_write32(Adapter, REG_EDCA_BK_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_ACM_CTRL:
			{
				u8	acm_ctrl = *((u8 *)val);
				u8	AcmCtrl = rtw_read8( Adapter, REG_ACMHWCTRL);

				if (acm_ctrl > 1)
					AcmCtrl = AcmCtrl | 0x1;

				if (acm_ctrl & BIT(3))
					AcmCtrl |= AcmHw_VoqEn;
				else
					AcmCtrl &= (~AcmHw_VoqEn);

				if (acm_ctrl & BIT(2))
					AcmCtrl |= AcmHw_ViqEn;
				else
					AcmCtrl &= (~AcmHw_ViqEn);

				if (acm_ctrl & BIT(1))
					AcmCtrl |= AcmHw_BeqEn;
				else
					AcmCtrl &= (~AcmHw_BeqEn);

				DBG_88E("[HW_VAR_ACM_CTRL] Write 0x%X\n", AcmCtrl );
				rtw_write8(Adapter, REG_ACMHWCTRL, AcmCtrl );
			}
			break;
		case HW_VAR_AMPDU_MIN_SPACE:
			{
				u8	MinSpacingToSet;
				u8	SecMinSpace;

				MinSpacingToSet = *((u8 *)val);
				if (MinSpacingToSet <= 7)
				{
					switch (Adapter->securitypriv.dot11PrivacyAlgrthm)
					{
						case _NO_PRIVACY_:
						case _AES_:
							SecMinSpace = 0;
							break;

						case _WEP40_:
						case _WEP104_:
						case _TKIP_:
						case _TKIP_WTMIC_:
							SecMinSpace = 6;
							break;
						default:
							SecMinSpace = 7;
							break;
					}

					if (MinSpacingToSet < SecMinSpace){
						MinSpacingToSet = SecMinSpace;
					}

					rtw_write8(Adapter, REG_AMPDU_MIN_SPACE, (rtw_read8(Adapter, REG_AMPDU_MIN_SPACE) & 0xf8) | MinSpacingToSet);
				}
			}
			break;
		case HW_VAR_AMPDU_FACTOR:
			{
				u8	RegToSet_Normal[4]={0x41,0xa8,0x72, 0xb9};
				u8	RegToSet_BT[4]={0x31,0x74,0x42, 0x97};
				u8	FactorToSet;
				u8	*pRegToSet;
				u8	index = 0;

#ifdef CONFIG_BT_COEXIST
				if (	(pHalData->bt_coexist.BT_Coexist) &&
					(pHalData->bt_coexist.BT_CoexistType == BT_CSR_BC4) )
					pRegToSet = RegToSet_BT; /*  0x97427431; */
				else
#endif
					pRegToSet = RegToSet_Normal; /*  0xb972a841; */

				FactorToSet = *((u8 *)val);
				if (FactorToSet <= 3)
				{
					FactorToSet = (1<<(FactorToSet + 2));
					if (FactorToSet>0xf)
						FactorToSet = 0xf;

					for (index=0; index<4; index++)
					{
						if ((pRegToSet[index] & 0xf0) > (FactorToSet<<4))
							pRegToSet[index] = (pRegToSet[index] & 0x0f) | (FactorToSet<<4);

						if ((pRegToSet[index] & 0x0f) > FactorToSet)
							pRegToSet[index] = (pRegToSet[index] & 0xf0) | (FactorToSet);

						rtw_write8(Adapter, (REG_AGGLEN_LMT+index), pRegToSet[index]);
					}

				}
			}
			break;
		case HW_VAR_RXDMA_AGG_PG_TH:
			{
				u8	threshold = *((u8 *)val);
				if ( threshold == 0)
				{
					threshold = pHalData->UsbRxAggPageCount;
				}
				rtw_write8(Adapter, REG_RXDMA_AGG_PG_TH, threshold);
			}
			break;
		case HW_VAR_SET_RPWM:
			break;
		case HW_VAR_H2C_FW_PWRMODE:
			{
				u8	psmode = (*(u8 *)val);

				/*  Forece leave RF low power mode for 1T1R to prevent conficting setting in Fw power */
				/*  saving sequence. 2010.06.07. Added by tynli. Suggested by SD3 yschang. */
				if ( (psmode != PS_MODE_ACTIVE) && (!IS_92C_SERIAL(pHalData->VersionID)))
					ODM_RF_Saving(podmpriv, true);
				rtl8188e_set_FwPwrMode_cmd(Adapter, psmode);
			}
			break;
		case HW_VAR_H2C_FW_JOINBSSRPT:
		    {
				u8	mstatus = (*(u8 *)val);
				rtl8188e_set_FwJoinBssReport_cmd(Adapter, mstatus);
			}
			break;
		case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
			{
				u8	p2p_ps_state = (*(u8 *)val);
				rtl8188e_set_p2p_ps_offload_cmd(Adapter, p2p_ps_state);
			}
			break;
		case HW_VAR_INITIAL_GAIN:
			{
				DIG_T	*pDigTable = &podmpriv->DM_DigTable;
				u32		rx_gain = ((u32 *)(val))[0];

				if (rx_gain == 0xff){/* restore rx gain */
					ODM_Write_DIG(podmpriv,pDigTable->BackupIGValue);
				}
				else{
					pDigTable->BackupIGValue = pDigTable->CurIGValue;
					ODM_Write_DIG(podmpriv,rx_gain);
				}
			}
			break;
		case HW_VAR_TRIGGER_GPIO_0:
			rtl8192cu_trigger_gpio_0(Adapter);
			break;
#ifdef CONFIG_BT_COEXIST
		case HW_VAR_BT_SET_COEXIST:
			{
				u8	bStart = (*(u8 *)val);
				rtl8192c_set_dm_bt_coexist(Adapter, bStart);
			}
			break;
		case HW_VAR_BT_ISSUE_DELBA:
			{
				u8	dir = (*(u8 *)val);
				rtl8192c_issue_delete_ba(Adapter, dir);
			}
			break;
#endif
#if (RATE_ADAPTIVE_SUPPORT==1)
		case HW_VAR_RPT_TIMER_SETTING:
			{
				u16	min_rpt_time = (*(u16 *)val);
				ODM_RA_Set_TxRPT_Time(podmpriv,min_rpt_time);
			}
			break;
#endif
		case HW_VAR_ANTENNA_DIVERSITY_SELECT:
			{
				u8	Optimum_antenna = (*(u8 *)val);
				u8	Ant ;
				/* switch antenna to Optimum_antenna */
				if (pHalData->CurAntenna !=  Optimum_antenna) {
					Ant = (Optimum_antenna==2)?MAIN_ANT:AUX_ANT;
					ODM_UpdateRxIdleAnt_88E(&pHalData->odmpriv, Ant);

					pHalData->CurAntenna = Optimum_antenna ;
				}
			}
			break;
		case HW_VAR_EFUSE_BYTES: /*  To set EFUE total used bytes, added by Roger, 2008.12.22. */
			pHalData->EfuseUsedBytes = *((u16 *)val);
			break;
		case HW_VAR_FIFO_CLEARN_UP:
			{
				struct pwrctrl_priv *pwrpriv = &Adapter->pwrctrlpriv;
				u8 trycnt = 100;

				/* pause tx */
				rtw_write8(Adapter,REG_TXPAUSE,0xff);

				/* keep sn */
				Adapter->xmitpriv.nqos_ssn = rtw_read16(Adapter,REG_NQOS_SEQ);

				if (pwrpriv->bkeepfwalive != true)
				{
					/* RX DMA stop */
					rtw_write32(Adapter,REG_RXPKT_NUM,(rtw_read32(Adapter,REG_RXPKT_NUM)|RW_RELEASE_EN));
					do{
						if (!(rtw_read32(Adapter,REG_RXPKT_NUM)&RXDMA_IDLE))
							break;
					}while (trycnt--);
					if (trycnt ==0)
						DBG_88E("Stop RX DMA failed......\n");

					/* RQPN Load 0 */
					rtw_write16(Adapter,REG_RQPN_NPQ,0x0);
					rtw_write32(Adapter,REG_RQPN,0x80000000);
					rtw_mdelay_os(10);
				}
			}
			break;
		case HW_VAR_CHECK_TXBUF:
			break;
		case HW_VAR_APFM_ON_MAC:
			pHalData->bMacPwrCtrlOn = *val;
			DBG_88E("%s: bMacPwrCtrlOn=%d\n", __func__, pHalData->bMacPwrCtrlOn);
			break;
#ifdef CONFIG_WOWLAN
		case HW_VAR_WOWLAN:
		{
			struct wowlan_ioctl_param *poidparam;
			struct recv_buf *precvbuf;
			int res, i;
			u32 tmp;
			u16 len = 0;
			u8 mstatus = (*(u8 *)val);
			u8 trycnt = 100;
			u8 data[4];

			poidparam = (struct wowlan_ioctl_param *)val;
			switch (poidparam->subcode){
				case WOWLAN_ENABLE:
					DBG_88E_LEVEL(_drv_always_, "WOWLAN_ENABLE\n");

					SetFwRelatedForWoWLAN8188ES(Adapter, true);

					/* RX DMA stop */
					DBG_88E_LEVEL(_drv_always_, "Pause DMA\n");
					rtw_write32(Adapter,REG_RXPKT_NUM,(rtw_read32(Adapter,REG_RXPKT_NUM)|RW_RELEASE_EN));
					do{
						if ((rtw_read32(Adapter, REG_RXPKT_NUM)&RXDMA_IDLE)) {
							DBG_88E_LEVEL(_drv_always_, "RX_DMA_IDLE is true\n");
							break;
						} else {
							/*  If RX_DMA is not idle, receive one pkt from DMA */
							DBG_88E_LEVEL(_drv_always_, "RX_DMA_IDLE is not true\n");
						}
					}while (trycnt--);
					if (trycnt ==0)
						DBG_88E_LEVEL(_drv_always_, "Stop RX DMA failed......\n");

					/* Set WOWLAN H2C command. */
					DBG_88E_LEVEL(_drv_always_, "Set WOWLan cmd\n");
					rtl8188es_set_wowlan_cmd(Adapter, 1);

					mstatus = rtw_read8(Adapter, REG_WOW_CTRL);
					trycnt = 10;

					while (!(mstatus&BIT1) && trycnt>1) {
						mstatus = rtw_read8(Adapter, REG_WOW_CTRL);
						DBG_88E_LEVEL(_drv_info_, "Loop index: %d :0x%02x\n", trycnt, mstatus);
						trycnt --;
						rtw_msleep_os(2);
					}

					Adapter->pwrctrlpriv.wowlan_wake_reason = rtw_read8(Adapter, REG_WOWLAN_WAKE_REASON);
					DBG_88E_LEVEL(_drv_always_, "wowlan_wake_reason: 0x%02x\n",
										Adapter->pwrctrlpriv.wowlan_wake_reason);

					/* Invoid SE0 reset signal during suspending*/
					rtw_write8(Adapter, REG_RSV_CTRL, 0x20);
					rtw_write8(Adapter, REG_RSV_CTRL, 0x60);

					/* rtw_msleep_os(10); */
					break;
				case WOWLAN_DISABLE:
					DBG_88E_LEVEL(_drv_always_, "WOWLAN_DISABLE\n");
					trycnt = 10;
					rtl8188es_set_wowlan_cmd(Adapter, 0);
					mstatus = rtw_read8(Adapter, REG_WOW_CTRL);
					DBG_88E_LEVEL(_drv_info_, "%s mstatus:0x%02x\n", __func__, mstatus);

					while (mstatus&BIT1 && trycnt>1) {
						mstatus = rtw_read8(Adapter, REG_WOW_CTRL);
						DBG_88E_LEVEL(_drv_always_, "Loop index: %d :0x%02x\n", trycnt, mstatus);
						trycnt --;
						rtw_msleep_os(2);
					}

					if (mstatus & BIT1)
						printk("System did not release RX_DMA\n");
					else
						SetFwRelatedForWoWLAN8188ES(Adapter, false);

					rtw_msleep_os(2);
					if (!(Adapter->pwrctrlpriv.wowlan_wake_reason & FWDecisionDisconnect))
						rtl8188e_set_FwJoinBssReport_cmd(Adapter, 1);
					break;
				default:
					break;
			}
		}
		break;
#endif /* CONFIG_WOWLAN */


	#if (RATE_ADAPTIVE_SUPPORT == 1)
		case HW_VAR_TX_RPT_MAX_MACID:
			{
				u8 maxMacid = *val;
				DBG_88E("### MacID(%d),Set Max Tx RPT MID(%d)\n",maxMacid,maxMacid+1);
				rtw_write8(Adapter, REG_TX_RPT_CTRL+1, maxMacid+1);
			}
			break;
	#endif
		case HW_VAR_H2C_MEDIA_STATUS_RPT:
			{
				rtl8188e_set_FwMediaStatus_cmd(Adapter , (*(__le16 *)val));
			}
			break;
		case HW_VAR_BCN_VALID:
			/* BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2, write 1 to clear, Clear by sw */
			rtw_write8(Adapter, REG_TDECTRL+2, rtw_read8(Adapter, REG_TDECTRL+2) | BIT0);
			break;

		default:

			break;
	}

_func_exit_;
}

static void GetHwReg8188EU(PADAPTER Adapter, u8 variable, u8* val)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	DM_ODM_T		*podmpriv = &pHalData->odmpriv;
_func_enter_;

	switch (variable)
	{
		case HW_VAR_BASIC_RATE:
			*((u16 *)(val)) = pHalData->BasicRateSet;
		case HW_VAR_TXPAUSE:
			val[0] = rtw_read8(Adapter, REG_TXPAUSE);
			break;
		case HW_VAR_BCN_VALID:
			/* BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2 */
			val[0] = (BIT0 & rtw_read8(Adapter, REG_TDECTRL+2))?true:false;
			break;
		case HW_VAR_DM_FLAG:
			val[0] = podmpriv->SupportAbility;
			break;
		case HW_VAR_RF_TYPE:
			val[0] = pHalData->rf_type;
			break;
		case HW_VAR_FWLPS_RF_ON:
			{
				/* When we halt NIC, we should check if FW LPS is leave. */
				if (Adapter->pwrctrlpriv.rf_pwrstate == rf_off)
				{
					/*  If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave, */
					/*  because Fw is unload. */
					val[0] = true;
				}
				else
				{
					u32 valRCR;
					valRCR = rtw_read32(Adapter, REG_RCR);
					valRCR &= 0x00070000;
					if (valRCR)
						val[0] = false;
					else
						val[0] = true;
				}
			}
			break;
		case HW_VAR_CURRENT_ANTENNA:
			val[0] = pHalData->CurAntenna;
			break;
		case HW_VAR_EFUSE_BYTES: /*  To get EFUE total used bytes, added by Roger, 2008.12.22. */
			*((u16 *)(val)) = pHalData->EfuseUsedBytes;
			break;
		case HW_VAR_APFM_ON_MAC:
			*val = pHalData->bMacPwrCtrlOn;
			break;
		case HW_VAR_CHK_HI_QUEUE_EMPTY:
			*val = ((rtw_read32(Adapter, REG_HGQ_INFORMATION)&0x0000ff00)==0) ? true:false;
			break;
		default:
			break;
	}

_func_exit_;
}

/*  */
/* 	Description: */
/* 		Query setting of specified variable. */
/*  */
static u8
GetHalDefVar8188EUsb(
		PADAPTER				Adapter,
		HAL_DEF_VARIABLE		eVariable,
		void *					pValue
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch (eVariable)
	{
		case HAL_DEF_UNDERCORATEDSMOOTHEDPWDB:
#if 1 /* trunk */
			{
				struct mlme_priv *pmlmepriv = &Adapter->mlmepriv;
				struct sta_priv * pstapriv = &Adapter->stapriv;
				struct sta_info * psta;
				psta = rtw_get_stainfo(pstapriv, pmlmepriv->cur_network.network.MacAddress);
				if (psta)
				{
					*((int *)pValue) = psta->rssi_stat.UndecoratedSmoothedPWDB;
				}
			}
#else /* V4 branch */
				if (check_fwstate(&Adapter->mlmepriv, WIFI_STATION_STATE) == true){
						*((int *)pValue) = pHalData->dmpriv.UndecoratedSmoothedPWDB;
				}
				else{

				}
#endif
			break;
		case HAL_DEF_IS_SUPPORT_ANT_DIV:
			*((u8 *)pValue) = (pHalData->AntDivCfg==0)?false:true;
			break;
		case HAL_DEF_CURRENT_ANTENNA:
			*(( u8*)pValue) = pHalData->CurAntenna;
			break;
		case HAL_DEF_DRVINFO_SZ:
			*(( u32*)pValue) = DRVINFO_SZ;
			break;
		case HAL_DEF_MAX_RECVBUF_SZ:
			*(( u32*)pValue) = MAX_RECVBUF_SZ;
			break;
		case HAL_DEF_RX_PACKET_OFFSET:
			*(( u32*)pValue) = RXDESC_SIZE + DRVINFO_SZ;
			break;

		case HAL_DEF_DBG_DM_FUNC:
			*(( u32*)pValue) =pHalData->odmpriv.SupportAbility;
			break;
#if (RATE_ADAPTIVE_SUPPORT == 1)
		case HAL_DEF_RA_DECISION_RATE:
			{
				u8 MacID = *((u8*)pValue);
				*((u8*)pValue) = ODM_RA_GetDecisionRate_8188E(&(pHalData->odmpriv), MacID);
			}
			break;

		case HAL_DEF_RA_SGI:
			{
				u8 MacID = *((u8*)pValue);
				*((u8*)pValue) = ODM_RA_GetShortGI_8188E(&(pHalData->odmpriv), MacID);
			}
			break;
#endif


		case HAL_DEF_PT_PWR_STATUS:
#if (POWER_TRAINING_ACTIVE==1)
			{
				u8 MacID = *((u8*)pValue);
				*((u8*)pValue) = ODM_RA_GetHwPwrStatus_8188E(&(pHalData->odmpriv), MacID);
			}
#endif/* POWER_TRAINING_ACTIVE==1) */
			break;

		case HW_VAR_MAX_RX_AMPDU_FACTOR:
			*(( u32*)pValue) = MAX_AMPDU_FACTOR_64K;
			break;

                case HW_DEF_RA_INFO_DUMP:
#if (RATE_ADAPTIVE_SUPPORT == 1)
			{
				u8 entry_id = *((u8*)pValue);
				if (check_fwstate(&Adapter->mlmepriv, _FW_LINKED)== true)
				{
					DBG_88E("============ RA status check ===================\n");
					DBG_88E("Mac_id:%d ,RateID = %d,RAUseRate = 0x%08x,RateSGI = %d, DecisionRate = 0x%02x ,PTStage = %d\n",
						entry_id,
						pHalData->odmpriv.RAInfo[entry_id].RateID,
						pHalData->odmpriv.RAInfo[entry_id].RAUseRate,
						pHalData->odmpriv.RAInfo[entry_id].RateSGI,
						pHalData->odmpriv.RAInfo[entry_id].DecisionRate,
						pHalData->odmpriv.RAInfo[entry_id].PTStage);
				}
			}
#endif	/* RATE_ADAPTIVE_SUPPORT == 1) */
			break;
		case HW_DEF_ODM_DBG_FLAG:
			{
				u8Byte	DebugComponents = *((u32*)pValue);
				PDM_ODM_T	pDM_Odm = &(pHalData->odmpriv);
				printk("pDM_Odm->DebugComponents = 0x%llx\n",pDM_Odm->DebugComponents );
			}
			break;

		case HAL_DEF_DBG_DUMP_RXPKT:
			*(( u8*)pValue) = pHalData->bDumpRxPkt;
			break;
		case HAL_DEF_DBG_DUMP_TXPKT:
			*(( u8*)pValue) = pHalData->bDumpTxPkt;
			break;

		default:
			bResult = _FAIL;
			break;
	}

	return bResult;
}




/*  */
/* 	Description: */
/* 		Change default setting of specified variable. */
/*  */
static u8 SetHalDefVar8188EUsb(
		PADAPTER				Adapter,
		HAL_DEF_VARIABLE		eVariable,
		void *					pValue
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch (eVariable)
	{
		case HAL_DEF_DBG_DM_FUNC:
			{
				u8 dm_func = *(( u8*)pValue);
				struct dm_priv	*pdmpriv = &pHalData->dmpriv;
				DM_ODM_T		*podmpriv = &pHalData->odmpriv;

				if (dm_func == 0){ /* disable all dynamic func */
					podmpriv->SupportAbility = DYNAMIC_FUNC_DISABLE;
					DBG_88E("==> Disable all dynamic function...\n");
				}
				else if (dm_func == 1){/* disable DIG */
					podmpriv->SupportAbility  &= (~DYNAMIC_BB_DIG);
					DBG_88E("==> Disable DIG...\n");
				}
				else if (dm_func == 2){/* disable High power */
					podmpriv->SupportAbility  &= (~DYNAMIC_BB_DYNAMIC_TXPWR);
				}
				else if (dm_func == 3){/* disable tx power tracking */
					podmpriv->SupportAbility  &= (~DYNAMIC_RF_CALIBRATION);
					DBG_88E("==> Disable tx power tracking...\n");
				}
				else if (dm_func == 5){/* disable antenna diversity */
					podmpriv->SupportAbility  &= (~DYNAMIC_BB_ANT_DIV);
				}
				else if (dm_func == 6){/* turn on all dynamic func */
					if (!(podmpriv->SupportAbility  & DYNAMIC_BB_DIG))
					{
						DIG_T	*pDigTable = &podmpriv->DM_DigTable;
						pDigTable->CurIGValue= rtw_read8(Adapter,0xc50);
					}
					podmpriv->SupportAbility = DYNAMIC_ALL_FUNC_ENABLE;
					DBG_88E("==> Turn on all dynamic function...\n");
				}
			}
			break;
		case HAL_DEF_DBG_DUMP_RXPKT:
			pHalData->bDumpRxPkt = *(( u8*)pValue);
			break;
		case HAL_DEF_DBG_DUMP_TXPKT:
			pHalData->bDumpTxPkt = *(( u8*)pValue);
			break;
		case HW_DEF_FA_CNT_DUMP:
			{
				u8 bRSSIDump = *((u8*)pValue);
				PDM_ODM_T		pDM_Odm = &(pHalData->odmpriv);
				if (bRSSIDump)
					pDM_Odm->DebugComponents	=	ODM_COMP_DIG|ODM_COMP_FA_CNT	;
				else
					pDM_Odm->DebugComponents	= 0;

			}
			break;
		case HW_DEF_ODM_DBG_FLAG:
			{
				u8Byte	DebugComponents = *((u8Byte*)pValue);
				PDM_ODM_T	pDM_Odm = &(pHalData->odmpriv);
				pDM_Odm->DebugComponents = DebugComponents;
			}
			break;
		default:
			bResult = _FAIL;
			break;
	}

	return bResult;
}

static void _update_response_rate(_adapter *padapter,unsigned int mask)
{
	u8	RateIndex = 0;
	/*  Set RRSR rate table. */
	rtw_write8(padapter, REG_RRSR, mask&0xff);
	rtw_write8(padapter,REG_RRSR+1, (mask>>8)&0xff);

	/*  Set RTS initial rate */
	while (mask > 0x1)
	{
		mask = (mask>> 1);
		RateIndex++;
	}
	rtw_write8(padapter, REG_INIRTS_RATE_SEL, RateIndex);
}

static void UpdateHalRAMask8188EUsb(PADAPTER padapter, u32 mac_id, u8 rssi_level)
{
	u8	init_rate=0;
	u8	networkType, raid;
	u32	mask,rate_bitmap;
	u8	shortGIrate = false;
	int	supportRateNum = 0;
	struct sta_info	*psta;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	WLAN_BSSID_EX		*cur_network = &(pmlmeinfo->network);

	if (mac_id >= NUM_STA) /* CAM_SIZE */
		return;

	psta = pmlmeinfo->FW_sta_info[mac_id].psta;
	if (psta == NULL)
		return;

	switch (mac_id)
	{
		case 0:/*  for infra mode */
			supportRateNum = rtw_get_rateset_len(cur_network->SupportedRates);
			networkType = judge_network_type(padapter, cur_network->SupportedRates, supportRateNum) & 0xf;
			raid = networktype_to_raid(networkType);

			mask = update_supported_rate(cur_network->SupportedRates, supportRateNum);
			mask |= (pmlmeinfo->HT_enable)? update_MSC_rate(&(pmlmeinfo->HT_caps)): 0;


			if (support_short_GI(padapter, &(pmlmeinfo->HT_caps)))
			{
				shortGIrate = true;
			}

			break;

		case 1:/* for broadcast/multicast */
			supportRateNum = rtw_get_rateset_len(pmlmeinfo->FW_sta_info[mac_id].SupportedRates);
			if (pmlmeext->cur_wireless_mode & WIRELESS_11B)
				networkType = WIRELESS_11B;
			else
				networkType = WIRELESS_11G;
			raid = networktype_to_raid(networkType);
			mask = update_basic_rate(cur_network->SupportedRates, supportRateNum);


			break;

		default: /* for each sta in IBSS */
			supportRateNum = rtw_get_rateset_len(pmlmeinfo->FW_sta_info[mac_id].SupportedRates);
			networkType = judge_network_type(padapter, pmlmeinfo->FW_sta_info[mac_id].SupportedRates, supportRateNum) & 0xf;
			raid = networktype_to_raid(networkType);
			mask = update_supported_rate(cur_network->SupportedRates, supportRateNum);

			/* todo: support HT in IBSS */

			break;
	}

	rate_bitmap = 0x0fffffff;
	rate_bitmap = ODM_Get_Rate_Bitmap(&pHalData->odmpriv,mac_id,mask,rssi_level);
	DBG_88E("%s => mac_id:%d, networkType:0x%02x, mask:0x%08x\n\t ==> rssi_level:%d, rate_bitmap:0x%08x\n",
		 __func__,mac_id,networkType,mask,rssi_level,rate_bitmap);

	mask &= rate_bitmap;

	init_rate = get_highest_rate_idx(mask)&0x3f;

	if (pHalData->fw_ractrl == true)
	{
		u8 arg = 0;

		arg = mac_id&0x1f;/* MACID */

		arg |= BIT(7);

		if (shortGIrate==true)
			arg |= BIT(5);
		mask |= ((raid<<28)&0xf0000000);
		DBG_88E("update raid entry, mask=0x%x, arg=0x%x\n", mask, arg);
		psta->ra_mask=mask;
		mask |= ((raid<<28)&0xf0000000);

		/* to do ,for 8188E-SMIC */
		/*
		*(pu4Byte)&RateMask=EF4Byte((ratr_bitmap&0x0fffffff) | (ratr_index<<28));
		RateMask[4] = macId | (bShortGI?0x20:0x00) | 0x80;
		*/
		rtl8188e_set_raid_cmd(padapter, mask);

	}
	else
	{

#if (RATE_ADAPTIVE_SUPPORT == 1)

		ODM_RA_UpdateRateInfo_8188E(
				&(pHalData->odmpriv),
				mac_id,
				raid,
				mask,
				shortGIrate
				);

#endif
	}


	/* set ra_id */
	psta->raid = raid;
	psta->init_rate = init_rate;


}

static void SetBeaconRelatedRegisters8188EUsb(PADAPTER padapter)
{
	u32	value32;
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u32 bcn_ctrl_reg			= REG_BCN_CTRL;
	/* reset TSF, enable update TSF, correcting TSF On Beacon */

	/* BCN interval */
	rtw_write16(padapter, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);
	rtw_write8(padapter, REG_ATIMWND, 0x02);/*  2ms */

	_InitBeaconParameters(padapter);

	rtw_write8(padapter, REG_SLOT, 0x09);

	value32 =rtw_read32(padapter, REG_TCR);
	value32 &= ~TSFRST;
	rtw_write32(padapter,  REG_TCR, value32);

	value32 |= TSFRST;
	rtw_write32(padapter, REG_TCR, value32);

	/*  NOTE: Fix test chip's bug (about contention windows's randomness) */
	rtw_write8(padapter,  REG_RXTSF_OFFSET_CCK, 0x50);
	rtw_write8(padapter, REG_RXTSF_OFFSET_OFDM, 0x50);

	_BeaconFunctionEnable(padapter, true, true);

	ResumeTxBeacon(padapter);




	rtw_write8(padapter, bcn_ctrl_reg, rtw_read8(padapter, bcn_ctrl_reg)|BIT(1));

}

static void rtl8188eu_init_default_value(_adapter * padapter)
{
	PHAL_DATA_TYPE pHalData;
	struct pwrctrl_priv *pwrctrlpriv;
	u8 i;

	pHalData = GET_HAL_DATA(padapter);
	pwrctrlpriv = &padapter->pwrctrlpriv;


	/* init default value */
	pHalData->fw_ractrl = false;
	if (!pwrctrlpriv->bkeepfwalive)
		pHalData->LastHMEBoxNum = 0;

	/* init dm default value */
	pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized = false;
	pHalData->odmpriv.RFCalibrateInfo.TM_Trigger = 0;/* for IQK */
	pHalData->pwrGroupCnt = 0;
	pHalData->PGMaxGroup= 13;
	pHalData->odmpriv.RFCalibrateInfo.ThermalValue_HP_index = 0;
	for (i = 0; i < HP_THERMAL_NUM; i++)
		pHalData->odmpriv.RFCalibrateInfo.ThermalValue_HP[i] = 0;
}

static u8 rtl8188eu_ps_func(PADAPTER Adapter,HAL_INTF_PS_FUNC efunc_id, u8 *val)
{
	u8 bResult = true;
	switch (efunc_id){

		#if defined(CONFIG_AUTOSUSPEND)
		case HAL_USB_SELECT_SUSPEND:
			{
				u8 bfwpoll = *(( u8*)val);
			}
			break;
		#endif /* CONFIG_AUTOSUSPEND */

		default:
			break;
	}
	return bResult;
}

void rtl8188eu_set_hal_ops(_adapter * padapter)
{
	struct hal_ops	*pHalFunc = &padapter->HalFunc;

_func_enter_;

	padapter->HalData = rtw_zmalloc(sizeof(HAL_DATA_TYPE));
	if (padapter->HalData == NULL){
		DBG_88E("cant not alloc memory for HAL DATA\n");
	}
	padapter->hal_data_sz = sizeof(HAL_DATA_TYPE);

	pHalFunc->hal_power_on = rtl8188eu_InitPowerOn;
	pHalFunc->hal_init = &rtl8188eu_hal_init;
	pHalFunc->hal_deinit = &rtl8188eu_hal_deinit;


	pHalFunc->inirp_init = &rtl8188eu_inirp_init;
	pHalFunc->inirp_deinit = &rtl8188eu_inirp_deinit;

	pHalFunc->init_xmit_priv = &rtl8188eu_init_xmit_priv;
	pHalFunc->free_xmit_priv = &rtl8188eu_free_xmit_priv;

	pHalFunc->init_recv_priv = &rtl8188eu_init_recv_priv;
	pHalFunc->free_recv_priv = &rtl8188eu_free_recv_priv;
	pHalFunc->InitSwLeds = &rtl8188eu_InitSwLeds;
	pHalFunc->DeInitSwLeds = &rtl8188eu_DeInitSwLeds;

	pHalFunc->init_default_value = &rtl8188eu_init_default_value;
	pHalFunc->intf_chip_configure = &rtl8188eu_interface_configure;
	pHalFunc->read_adapter_info = &ReadAdapterInfo8188EU;




	pHalFunc->SetHwRegHandler = &SetHwReg8188EU;
	pHalFunc->GetHwRegHandler = &GetHwReg8188EU;
	pHalFunc->GetHalDefVarHandler = &GetHalDefVar8188EUsb;
	pHalFunc->SetHalDefVarHandler = &SetHalDefVar8188EUsb;

	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8188EUsb;
	pHalFunc->SetBeaconRelatedRegistersHandler = &SetBeaconRelatedRegisters8188EUsb;


	pHalFunc->hal_xmit = &rtl8188eu_hal_xmit;
	pHalFunc->mgnt_xmit = &rtl8188eu_mgnt_xmit;

	pHalFunc->interface_ps_func = &rtl8188eu_ps_func;

	rtl8188e_set_hal_ops(pHalFunc);
_func_exit_;

}
