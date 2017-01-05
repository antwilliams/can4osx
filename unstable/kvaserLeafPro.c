//
//  kvaserLeafPro.c
//
//
// Copyright (c) 2014 - 2017 Alexander Philipp. All rights reserved.
//
//
// License: GPLv2
//
// =============================================================================
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation version 2
// of the license.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street,
// Fifth Floor, Boston, MA  02110-1301, USA.
//
// =============================================================================
//
// Disclaimer:     IMPORTANT: THE SOFTWARE IS PROVIDED ON AN "AS IS" BASIS. THE
// AUTHOR MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION
// THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A
// PARTICULAR PURPOSE, REGARDING THE SOFTWARE OR ITS USE AND OPERATION ALONE OR
// IN COMBINATION WITH YOUR PRODUCTS.
//
// IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL
// OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION, MODIFICATION
// AND/OR DISTRIBUTION OF SOFTWARE, HOWEVER CAUSED AND WHETHER UNDER THEORY OF
// CONTRACT, TORT (INCLUDING NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN IF
// THE AUTHOR HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
// =============================================================================
//
//


#include <stdio.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>


/* can4osx */
#include "can4osx.h"
#include "can4osx_debug.h"
#include "can4osx_internal.h"

#include "kvaserLeafPro.h"

#define LEAFPRO_COMMAND_SIZE 32u

#define LEAFPRO_CMD_SET_BUSPARAMS_REQ           16u
#define LEAFPRO_CMD_CHIP_STATE_EVENT            20u
#define LEAFPRO_CMD_SET_DRIVERMODE_REQ          21u
#define LEAFPRO_CMD_START_CHIP_REQ              26u
#define LEAFPRO_CMD_START_CHIP_RESP             27u
#define LEAFPRO_CMD_TX_CAN_MESSAGE              33u
#define LEAFPRO_CMD_GET_CARD_INFO_REQ           34u
#define LEAFPRO_CMD_GET_CARD_INFO_RESP          35u
#define LEAFPRO_CMD_GET_SOFTWARE_INFO_REQ       38u
#define LEAFPRO_CMD_GET_SOFTWARE_INFO_RESP      39u
#define LEAFPRO_CMD_SET_BUSPARAMS_FD_REQ        69u
#define LEAFPRO_CMD_SET_BUSPARAMS_FD_RESP       70u
#define LEAFPRO_CMD_SET_BUSPARAMS_RESP          85u
#define LEAFPRO_CMD_LOG_MESSAGE                 106u
#define LEAFPRO_CMD_MAP_CHANNEL_REQ             200u
#define LEAFPRO_CMD_MAP_CHANNEL_RESP            201u
#define LEAFPRO_CMD_GET_SOFTWARE_DETAILS_REQ    202u
#define LEAFPRO_CMD_GET_SOFTWARE_DETAILS_RESP   203u
#define LEAFPRO_CMD_RX_MESSAGE_FD               226u

/* extended FD able command code */
#define LEAFPRO_CMD_CAN_FD                      255u




#define LEAFPRO_HE_ILLEGAL      0x3eu
#define LEAFPRO_HE_ROUTER       0x00u

#define LEAFPRO_TIMEOUT_ONE_MS 1000000
#define LEAFPRO_TIMEOUT_TEN_MS 10*LEAFPRO_TIMEOUT_ONE_MS

static char* pDeviceString = "Kvaser Leaf Pro v2";

static UInt32 getCommandSize(proCommand_t *pCmd);
static UInt8 decodeFdDlc(UInt8 dlc);

static void LeafProDecodeCommand(Can4osxUsbDeviceHandleEntry *pSelf,
                                 proCommand_t *pCmd);
static void LeafProDecodeCommandExt(Can4osxUsbDeviceHandleEntry *pSelf,
                                 proCommandExt_t *pCmd);

static void LeafProMapChannels(Can4osxUsbDeviceHandleEntry *pSelf);

static void LeafProGetCardInfo(Can4osxUsbDeviceHandleEntry *pSelf);

static canStatus LeafProCanSetBusParams ( const CanHandle hnd, SInt32 freq,
                                         unsigned int tseg1,
                                         unsigned int tseg2,
                                         unsigned int sjw,
                                         unsigned int noSamp,
                                         unsigned int syncmode );

static canStatus LeafProCanRead (const CanHandle hnd,
                                 UInt32 *id,
                                 void *msg,
                                 UInt16 *dlc,
                                 UInt32 *flag,
                                 UInt32 *time);

static canStatus LeafProCanWrite(const CanHandle hnd, UInt32 id, void *msg,
                                 UInt16 dlc, UInt32 flag);


static canStatus LeafProCanTranslateBaud (SInt32 *const freq,
                                          unsigned int *const tseg1,
                                          unsigned int *const tseg2,
                                          unsigned int *const sjw,
                                          unsigned int *const nosamp,
                                          unsigned int *const syncMode);

static IOReturn LeafProWriteCommandWait(Can4osxUsbDeviceHandleEntry *pSelf,
                                        proCommand_t cmd,
                                        UInt8 reason);

static LeafProCommandMsgBuf_t* LeafProCreateCommandBuffer(UInt32 bufferSize);
static void LeafProReleaseCommandBuffer(LeafProCommandMsgBuf_t* pBufferRef);
static UInt8 LeafProTestFullCommandBuffer(LeafProCommandMsgBuf_t* bufferRef);
static UInt8 LeafProTestEmptyCommandBuffer(LeafProCommandMsgBuf_t* pBufferRef);
static UInt8 LeafProWriteCommandBuffer(LeafProCommandMsgBuf_t* pBufferRef,
                                       proCommand_t newCommand);

static UInt16 LeafProFillBulkPipeBuffer(LeafProCommandMsgBuf_t* bufferRef,
                                        UInt8 *pPipe, UInt16 maxPipeSize);
static IOReturn LeafProWriteBulkPipe(Can4osxUsbDeviceHandleEntry *pSelf);
static void LeafProReadFromBulkInPipe(Can4osxUsbDeviceHandleEntry *self);
static void LeafProBulkReadCompletion(void *refCon,
                                      IOReturn result,
                                      void *arg0);


//Hardware interface function
static canStatus LeafProInitHardware(const CanHandle hnd);
static CanHandle LeafProCanOpenChannel(int channel, int flags);
static canStatus LeafProCanStartChip(CanHandle hdl);

Can4osxHwFunctions leafProHardwareFunctions = {
    .can4osxhwInitRef = LeafProInitHardware,
    .can4osxhwCanOpenChannel = LeafProCanOpenChannel,
    .can4osxhwCanSetBusParamsRef = LeafProCanSetBusParams,
    .can4osxhwCanBusOnRef = LeafProCanStartChip,
    .can4osxhwCanBusOffRef = NULL,
    .can4osxhwCanWriteRef = LeafProCanWrite,
    .can4osxhwCanReadRef = LeafProCanRead,
    .can4osxhwCanCloseRef = NULL,
};


static canStatus LeafProInitHardware(
        const CanHandle hnd
        )
{
Can4osxUsbDeviceHandleEntry *pSelf = &can4osxUsbDeviceHandle[hnd];
pSelf->privateData = calloc(1,sizeof(LeafProPrivateData_t));

    if ( pSelf->privateData != NULL ) {
    LeafProPrivateData_t *pPriv = (LeafProPrivateData_t *)pSelf->privateData;
        
        pPriv->cmdBufferRef = LeafProCreateCommandBuffer(1000);
        if ( pPriv->cmdBufferRef == NULL ) {
            free(pPriv);
            return canERR_NOMEM;
        }

        pPriv->semaTimeout = dispatch_semaphore_create(0);
        
    } else {
        return canERR_NOMEM;
    }

    
    // Set some device Infos
    sprintf((char*)pSelf->devInfo.deviceString, "%s",pDeviceString);
    
    // Trigger next read
    LeafProReadFromBulkInPipe(pSelf);
    
    // Set up channels
    LeafProMapChannels(pSelf);
    
    // Get channel info
    LeafProGetCardInfo(pSelf);
    
    
    return canOK;
}


static CanHandle LeafProCanOpenChannel(
        int channel,
        int flags
        )
{
Can4osxUsbDeviceHandleEntry *pSelf = &can4osxUsbDeviceHandle[channel];
LeafProPrivateData_t *pPriv = (LeafProPrivateData_t *)pSelf->privateData;
    
    // set CAN Mode
    if ((flags & canOPEN_CAN_FD) ==  canOPEN_CAN_FD) {
        pPriv->canFd = 1;
    } else {
        pPriv->canFd = 0;
    }

    return (CanHandle)channel;
}


//Set bit timing
static canStatus LeafProCanSetBusParams (
        const CanHandle hnd,
        SInt32 freq,
        unsigned int tseg1,
        unsigned int tseg2,
        unsigned int sjw,
        unsigned int noSamp,
        unsigned int syncmode
        )
{
proCommand_t   cmd;
UInt32         tmp, PScl;
int            retVal;
    
Can4osxUsbDeviceHandleEntry *pSelf = &can4osxUsbDeviceHandle[hnd];
LeafProPrivateData_t *pPriv = (LeafProPrivateData_t *)pSelf->privateData;
    
    CAN4OSX_DEBUG_PRINT("leaf pro: _set_busparam\n");
    
    if ( canOK != LeafProCanTranslateBaud(&freq, &tseg1, &tseg2, &sjw,
                                          &noSamp, &syncmode)) {
        // TODO
        CAN4OSX_DEBUG_PRINT(" can4osx strange bitrate\n");
        return -1;
    }
    
    
    // Check bus parameters
    tmp = freq * (tseg1 + tseg2 + 1);
    if (tmp == 0) {
        CAN4OSX_DEBUG_PRINT("leaf: _set_busparams() tmp == 0!\n");
        return VCAN_STAT_BAD_PARAMETER;
    }
    
    PScl = 16000000UL / tmp;
    
    if (PScl </*=*/ 1 || PScl > 256) {
        CAN4OSX_DEBUG_PRINT("hwif_set_chip_param() prescaler wrong (%d)\n",
                PScl & 1 /* even */);
        return VCAN_STAT_BAD_PARAMETER;
    }
    memset(&cmd, 0 , sizeof(cmd));
    
    cmd.proCmdSetBusparamsReq.header.cmdNo = LEAFPRO_CMD_SET_BUSPARAMS_REQ;
    cmd.proCmdSetBusparamsReq.header.address = pPriv->address;
    cmd.proCmdSetBusparamsReq.header.transitionId = 0x0000;
    
    cmd.proCmdSetBusparamsReq.bitRate = freq;
    cmd.proCmdSetBusparamsReq.sjw     = (UInt8)sjw;
    cmd.proCmdSetBusparamsReq.tseg1   = (UInt8)tseg1;
    cmd.proCmdSetBusparamsReq.tseg2   = (UInt8)tseg2;
    cmd.proCmdSetBusparamsReq.noSamp  = 1;
    
    retVal = LeafProWriteCommandWait( pSelf, cmd,
                LEAFPRO_CMD_SET_BUSPARAMS_RESP);
    
    // FIXME
#warning remove static FD
    pPriv->canFd = 1;
    
    if (pPriv->canFd) {
        cmd.proCmdSetBusparamsReq.header.cmdNo = LEAFPRO_CMD_SET_BUSPARAMS_FD_REQ;
        cmd.proCmdSetBusparamsReq.open_as_canfd = 1;
        
        
            cmd.proCmdSetBusparamsReq.bitRate = 500000;
    cmd.proCmdSetBusparamsReq.sjw     = 8;//(UInt8)1;
    cmd.proCmdSetBusparamsReq.tseg1   = 64;//(UInt8)5;
    cmd.proCmdSetBusparamsReq.tseg2   = 16;//(UInt8)2;
    cmd.proCmdSetBusparamsReq.noSamp  = 0;
        cmd.proCmdSetBusparamsReq.bitRateFd = 1000000L;
        cmd.proCmdSetBusparamsReq.tseg1Fd = 32;//15;
        cmd.proCmdSetBusparamsReq.tseg2Fd = 8;//4;
        cmd.proCmdSetBusparamsReq.sjwFd = 2;//4;
        cmd.proCmdSetBusparamsReq.noSampFd = 0;
            retVal = LeafProWriteCommandWait( pSelf, cmd,
                LEAFPRO_CMD_SET_BUSPARAMS_FD_RESP);
        
           // cmd.proCmdHead.cmdNo = 0x1e;
        
                retVal = LeafProWriteCommandWait( pSelf, cmd,
                LEAFPRO_CMD_SET_BUSPARAMS_FD_RESP);
            //cmd.proCmdHead.cmdNo = 0x61;
        
                retVal = LeafProWriteCommandWait( pSelf, cmd,
                LEAFPRO_CMD_SET_BUSPARAMS_FD_RESP);
        //cmd.proCmdSetBusparamsReq.header.cmdNo = LEAFPRO_CMD_SET_BUSPARAMS_FD_REQ;
        
                retVal = LeafProWriteCommandWait( pSelf, cmd,
                LEAFPRO_CMD_SET_BUSPARAMS_FD_RESP);


    } else {
        retVal = LeafProWriteCommandWait( pSelf, cmd,
                LEAFPRO_CMD_SET_BUSPARAMS_RESP);
    }
    
    
    return retVal;
}

//Go bus on
static canStatus LeafProCanStartChip(
        CanHandle hdl
        )
{
int retVal = 0;
proCommand_t        cmd;
Can4osxUsbDeviceHandleEntry *pSelf = &can4osxUsbDeviceHandle[hdl];
LeafProPrivateData_t *pPriv = (LeafProPrivateData_t *)pSelf->privateData;

    memset(&cmd, 0u, sizeof(cmd));
    cmd.proCmdHead.cmdNo = LEAFPRO_CMD_SET_DRIVERMODE_REQ;
    //cmd.proCmdHead.transitionId = 0xec00;
    cmd.proCmdHead.address = pPriv->address;
    cmd.proCmdRaw.data[0] = 0x01;
    LeafProWriteCommandWait(pSelf, cmd, LEAFPRO_CMD_MAP_CHANNEL_RESP);

    
    CAN4OSX_DEBUG_PRINT("CAN BusOn Command %d\n", hdl);
    memset(&cmd, 0u, sizeof(cmd));
    
    cmd.proCmdHead.cmdNo = LEAFPRO_CMD_START_CHIP_REQ;
    cmd.proCmdHead.address = pPriv->address;
    cmd.proCmdHead.transitionId = 1u;
    retVal = LeafProWriteCommandWait(pSelf, cmd, LEAFPRO_CMD_CHIP_STATE_EVENT);
    
    return retVal;
}


/******************************************************************************/
static canStatus LeafProCanRead (
        const   CanHandle hnd,
        UInt32  *id,
        void    *msg,
        UInt16  *dlc,
        UInt32  *flag,
        UInt32  *time
        )
{
Can4osxUsbDeviceHandleEntry *pSelf = &can4osxUsbDeviceHandle[hnd];
    
    if ( pSelf->privateData != NULL ) {
        
        CanMsg canMsg;
        
        if ( CAN4OSX_ReadCanEventBuffer(pSelf->canEventMsgBuff, &canMsg) ) {
            
            *id = canMsg.canId;
            *dlc = canMsg.canDlc;
            *time =canMsg.canTimestamp;
            
            memcpy(msg, canMsg.canData, *dlc);
            
            *flag = canMsg.canFlags;
            
            return canOK;
        } else {
            return canERR_NOMSG;
        }
    } else {
        return canERR_INTERNAL;
    }
    
}


/******************************************************************************/
static canStatus LeafProCanWrite(
        const CanHandle hnd,
        UInt32 id,
        void *msg,
        UInt16 dlc,
        UInt32 flag
    )
{
Can4osxUsbDeviceHandleEntry *pSelf = &can4osxUsbDeviceHandle[hnd];
    
    if ( pSelf->privateData != NULL ) {
        LeafProPrivateData_t *pPriv = (LeafProPrivateData_t*)pSelf->privateData;
        proCommand_t cmd;
        
        if (flag & canMSG_EXT)  {
            cmd.proCmdTxMessage.canId = LEAFPRO_EXT_MSG;
        } else {
            cmd.proCmdTxMessage.canId = 0u;
        }
        cmd.proCmdTxMessage.canId += id;
        cmd.proCmdTxMessage.dlc = dlc & 0x0F;
        memcpy(cmd.proCmdTxMessage.data, msg, 8);
        
        cmd.proCmdTxMessage.flags = 0;
        // RTR Frame
        if ( flag & canMSG_RTR ) {
            cmd.proCmdTxMessage.flags |= LEAFPRO_MSG_FLAG_REMOTE_FRAME;
        }
        
        cmd.proCmdHead.cmdNo = LEAFPRO_CMD_TX_CAN_MESSAGE;
        cmd.proCmdHead.address = pPriv->address;
        cmd.proCmdHead.transitionId = 10;
        
        LeafProWriteCommandBuffer(pPriv->cmdBufferRef, cmd);

        LeafProWriteBulkPipe(pSelf);
        
        return canOK;
        
    } else {
        return canERR_INTERNAL;
    }
    
}

                                 
/******************************************************************************/
// Translate from baud macro to bus params
/******************************************************************************/
static canStatus LeafProCanTranslateBaud (
        SInt32 *const freq,
        unsigned int *const tseg1,
        unsigned int *const tseg2,
        unsigned int *const sjw,
        unsigned int *const nosamp,
        unsigned int *const syncMode
    )
{
    switch (*freq) {
        case canBITRATE_1M:
            *freq     = 1000000L;
            *tseg1    = 6;
            *tseg2    = 1;
            *sjw      = 1;
            *nosamp   = 1;
            *syncMode = 0;
            break;
            
        case canBITRATE_500K:
            *freq     = 500000L;
            *tseg1    = 12;//6;
            *tseg2    = 3;//1;
            *sjw      = 1;
            *nosamp   = 1;
            *syncMode = 0;
            break;
            
        case canBITRATE_250K:
            *freq     = 250000L;
            *tseg1    = 6;
            *tseg2    = 1;
            *sjw      = 1;
            *nosamp   = 1;
            *syncMode = 0;
            break;
            
        case canBITRATE_125K:
            *freq     = 125000L;
            *tseg1    = 13;
            *tseg2    = 2;
            *sjw      = 1;
            *nosamp   = 1;
            *syncMode = 0;
            break;
            
        case canBITRATE_100K:
            *freq     = 100000L;
            *tseg1    = 13;
            *tseg2    = 2;
            *sjw      = 1;
            *nosamp   = 1;
            *syncMode = 0;
            break;
            
        case canBITRATE_83K:
            *freq     = 83333L;
            *tseg1    = 5;
            *tseg2    = 2;
            *sjw      = 2;
            *nosamp   = 1;
            *syncMode = 0;
            break;
            
        case canBITRATE_62K:
            *freq     = 62500L;
            *tseg1    = 10;
            *tseg2    = 5;
            *sjw      = 1;
            *nosamp   = 1;
            *syncMode = 0;
            break;
            
        case canBITRATE_50K:
            *freq     = 50000L;
            *tseg1    = 10;
            *tseg2    = 5;
            *sjw      = 1;
            *nosamp   = 1;
            *syncMode = 0;
            break;
                        
        default:
            return canERR_PARAM;
    }
    
    return canOK;
}


#pragma mark LeafPro command stuff
/******************************************************************************/
/******************************************************************************/
/**************************** Command Stuff ***********************************/
/******************************************************************************/
/******************************************************************************/
static UInt32 getCommandSize(proCommand_t *pCmd)
{
    if (pCmd->proCmdHead.cmdNo == LEAFPRO_CMD_CAN_FD)  {
        return(((proCmdFdHead_t *)(pCmd))->len);
    } else {
        return(LEAFPRO_COMMAND_SIZE);
    }
}

/******************************************************************************/
/**
* \brief decodeFdDlc - decode the dlc to data length
*
* \return the datalength
*/
static UInt8 decodeFdDlc(UInt8 dlc)
{
    switch(dlc)  {
        case 0u:
        case 1u:
        case 2u:
        case 3u:
        case 4u:
        case 5u:
        case 6u:
        case 7u:
        case 8u:
            return dlc;
            break;
        case 9u:
            return 12u;
            break;
        case 10u:
            return 16u;
            break;
        case 11u:
            return 20u;
            break;
        case 12u:
            return 24u;
            break;
        case 13u:
            return 32u;
            break;
        case 14u:
            return 48u;
            break;
        case 15u:
            return 64u;
            break;
        default:
            break;
    }
    return 0u;
}


/******************************************************************************/
static void LeafProDecodeCommand(
        Can4osxUsbDeviceHandleEntry *pSelf,
        proCommand_t *pCmd
    )
{
LeafProPrivateData_t *pPriv = (LeafProPrivateData_t *)pSelf->privateData;
CanMsg canMsg;

    CAN4OSX_DEBUG_PRINT("Pro-Decode cmd %d\n",(UInt8)pCmd->proCmdHead.cmdNo);

    switch (pCmd->proCmdHead.cmdNo) {
        case LEAFPRO_CMD_CAN_FD:
            CAN4OSX_DEBUG_PRINT("LEAFPRO_CMD_CAN_FD\n");
            LeafProDecodeCommandExt(pSelf, (proCommandExt_t *)pCmd);
            break;
        case LEAFPRO_CMD_LOG_MESSAGE:
            if ( pCmd->proCmdLogMessage.canId & LEAFPRO_EXT_MSG ) {
                canMsg.canId = pCmd->proCmdLogMessage.canId & ~LEAFPRO_EXT_MSG;
                canMsg.canFlags = canMSG_EXT;
            } else {
                canMsg.canId = pCmd->proCmdLogMessage.canId;
                canMsg.canFlags = canMSG_STD;
            }
            
            if (pCmd->proCmdLogMessage.flags & LEAFPRO_MSG_FLAG_OVERRUN) {
                //canMsg.canFlags |= canMSGERR_HW_OVERRUN | canMSGERR_SW_OVERRUN;
            }
            if (pCmd->proCmdLogMessage.flags & LEAFPRO_MSG_FLAG_REMOTE_FRAME) {
                canMsg.canFlags |= canMSG_RTR;
            }
            if (pCmd->proCmdLogMessage.flags & LEAFPRO_MSG_FLAG_ERROR_FRAME) {
                canMsg.canFlags |= canMSG_ERROR_FRAME;
            }
            if (pCmd->proCmdLogMessage.flags & LEAFPRO_MSG_FLAG_TXACK) {
                canMsg.canFlags |= canMSG_TXACK;
            }
            if (pCmd->proCmdLogMessage.flags & LEAFPRO_MSG_FLAG_TXRQ) {
                canMsg.canFlags |= canMSG_TXRQ;
            }
            /* classical CAN dlc */
            if ( pCmd->proCmdLogMessage.dlc > 8u ) {
                pCmd->proCmdLogMessage.dlc = 8u;
            }
            
            canMsg.canDlc = pCmd->proCmdLogMessage.dlc;
            
            memcpy(canMsg.canData, pCmd->proCmdLogMessage.data,
                   pCmd->proCmdLogMessage.dlc);
            
            // FIXME canMsg.canTimestamp = LeafCalculateTimeStamp(pCmd->proCmdLogMessage.time, 24) * 10;
            
            
            CAN4OSX_WriteCanEventBuffer(pSelf->canEventMsgBuff,canMsg);
            if (pSelf->canNotification.notifacionCenter) {
                CFNotificationCenterPostNotification (pSelf->canNotification.notifacionCenter,
                                                      pSelf->canNotification.notificationString, NULL, NULL, true);
            }
            
            
            CAN4OSX_DEBUG_PRINT("PRO_CMD_LOG_MESSAGE Channel: Id: %X Flags: %X\n",
                                pCmd->proCmdLogMessage.canId,
                                pCmd->proCmdLogMessage.flags);
            break;
        case LEAFPRO_CMD_MAP_CHANNEL_RESP:
            CAN4OSX_DEBUG_PRINT("LEAFPRO_CMD_MAP_CHANNEL_RESP chan %X\n",
                                pCmd->proCmdHead.transitionId);
            CAN4OSX_DEBUG_PRINT("LEAFPRO_CMD_MAP_CHANNEL_RESP adr %X\n",
                                pCmd->proCmdHead.address);
            if ((pCmd->proCmdHead.transitionId & 0xff) == 0x40u)  {
                CAN4OSX_DEBUG_PRINT("LEAFPRO_CMD_MAP_CHANNEL_RESP CAN\n");
                pPriv->address = (UInt8)pCmd->proCmdMapChannelResp.heAddress;
            }
            break;
        case LEAFPRO_CMD_GET_SOFTWARE_DETAILS_RESP:
            CAN4OSX_DEBUG_PRINT("LEAFPRO_CMD_GET_SOFTWARE_DETAILS_RESP\n");

            break;
            
        default:
            break;
    }
#if CAN4OSX_DEBUG
    CAN4OSX_DEBUG_PRINT("LEAFPRO_MESSAGE %d\n", pCmd->proCmdHead.cmdNo);
#endif /* CAN4OSX_DEBUG */
}


/******************************************************************************/
static void LeafProDecodeCommandExt(
        Can4osxUsbDeviceHandleEntry *pSelf,
        proCommandExt_t *pCmd
    )
{
LeafProPrivateData_t *pPriv = (LeafProPrivateData_t *)pSelf->privateData;
CanMsg canMsg;

    switch (pCmd->proCmdFdHead.header.cmdNo)  {
        default:
            if (pCmd->proCmdFdRxMessage.flags & LEAFPRO_MSG_FLAG_ERROR_FRAME) {
                CAN4OSX_DEBUG_PRINT("LEAFPRO_MESSAGE ERROR_FRAME\n");
                break;
            }
        
            canMsg.canId = pCmd->proCmdFdRxMessage.canId;
            canMsg.canDlc = (pCmd->proCmdFdRxMessage.control>>8u) & 0x0fu;
            
            canMsg.canFlags = 0u;
            
            if (pCmd->proCmdFdRxMessage.flags & LEAFPRO_MSGFLAG_FDF)  {
                /* insanity check */
                if (pPriv->canFd == 0)  {
                    return;
                }
            
                CAN4OSX_DEBUG_PRINT("LEAFPRO_MESSAGE CAN-FD\n");
                canMsg.canFlags |= canFDMSG_FDF;
                
                /* test for other FD flags */
                if (pCmd->proCmdFdRxMessage.flags & LEAFPRO_MSGFLAG_BRS)  {
                    CAN4OSX_DEBUG_PRINT("LEAFPRO_MESSAGE CAN-FD - BRS\n");
                    canMsg.canFlags |= canFDMSG_BRS;
                }
                /* decode dlc to length */
                canMsg.canDlc = decodeFdDlc(canMsg.canDlc);
                
            } else {
                CAN4OSX_DEBUG_PRINT("LEAFPRO_MESSAGE CLASSIC\n");
                if (canMsg.canDlc > 8u)  {
                    canMsg.canDlc = 8u;
                }
            }
            
            if (pCmd->proCmdFdRxMessage.flags & LEAFPRO_MSG_FLAG_EXTENDED) {
                CAN4OSX_DEBUG_PRINT("LEAFPRO_MESSAGE EXTENDED\n");
                canMsg.canFlags |= canMSG_EXT;
            } else {
                CAN4OSX_DEBUG_PRINT("LEAFPRO_MESSAGE STD\n");
                canMsg.canFlags |= canMSG_STD;
            }
            
            memcpy(canMsg.canData, pCmd->proCmdFdRxMessage.data, canMsg.canDlc);
            
            CAN4OSX_WriteCanEventBuffer(pSelf->canEventMsgBuff,canMsg);
            
            if (pSelf->canNotification.notifacionCenter) {
                CFNotificationCenterPostNotification (pSelf->canNotification.notifacionCenter,
                    pSelf->canNotification.notificationString, NULL, NULL, true);
            }
            
            break;
    }
}


#pragma mark Leaf Pro mapping Stuff
/******************************************************************************/
/******************************************************************************/
/**************************** Mapping Stuff ***********************************/
/******************************************************************************/
/******************************************************************************/
static void LeafProMapChannels(
        Can4osxUsbDeviceHandleEntry
        *pSelf
    )
{
proCommand_t cmd;

    memset(&cmd, 0, 32);
    
    cmd.proCmdHead.cmdNo = LEAFPRO_CMD_MAP_CHANNEL_REQ;
    cmd.proCmdHead.address = LEAFPRO_HE_ROUTER;
    cmd.proCmdMapChannelReq.channel = 0;
    
    strcpy(cmd.proCmdMapChannelReq.name, "CAN");
    cmd.proCmdHead.transitionId = 0x40;
    LeafProWriteCommandWait(pSelf, cmd, LEAFPRO_CMD_MAP_CHANNEL_RESP);
        
    strcpy(cmd.proCmdMapChannelReq.name, "SYSDBG");
    cmd.proCmdHead.transitionId = 0x61;
    LeafProWriteCommandWait(pSelf, cmd, LEAFPRO_CMD_MAP_CHANNEL_RESP);
    
    return;
}


#pragma mark card info request
/******************************************************************************/
static void LeafProGetCardInfo(Can4osxUsbDeviceHandleEntry *pSelf)
{
proCommand_t cmd;

    memset(&cmd, 0u, sizeof(cmd));
    cmd.proCmdHead.address = LEAFPRO_HE_ILLEGAL;

    cmd.proCmdHead.cmdNo = LEAFPRO_CMD_GET_CARD_INFO_REQ;
    LeafProWriteCommandWait(pSelf, cmd, LEAFPRO_CMD_GET_CARD_INFO_RESP);
    
    cmd.proCmdHead.cmdNo = LEAFPRO_CMD_GET_SOFTWARE_INFO_REQ;
    cmd.proCmdGetSoftwareDetailsReq.useExt = 1u;
    LeafProWriteCommandWait(pSelf, cmd, LEAFPRO_CMD_GET_SOFTWARE_INFO_RESP);
    
    cmd.proCmdHead.cmdNo = LEAFPRO_CMD_GET_SOFTWARE_DETAILS_REQ;
    LeafProWriteCommandWait(pSelf, cmd, LEAFPRO_CMD_GET_SOFTWARE_INFO_RESP);
    
    return;
}


#pragma mark Command Buffer
/******************************************************************************/
static LeafProCommandMsgBuf_t* LeafProCreateCommandBuffer(
        UInt32 bufferSize
    )
{
LeafProCommandMsgBuf_t* pBufferRef = malloc(sizeof(LeafProCommandMsgBuf_t));

    if ( pBufferRef == NULL ) {
        return NULL;
    }
    
    pBufferRef->bufferSize = bufferSize;
    pBufferRef->bufferCount = 0;
    pBufferRef->bufferFirst = 0;
    
    pBufferRef->commandRef = malloc(bufferSize * sizeof(proCommand_t));
    
    if ( pBufferRef->commandRef == NULL ) {
        free(pBufferRef);
        return NULL;
    }
    
    pBufferRef->bufferGDCqueueRef = dispatch_queue_create(
                                        "com.can4osx.leafprocommandqueue", 0);
    if ( pBufferRef->bufferGDCqueueRef == NULL ) {
        LeafProReleaseCommandBuffer(pBufferRef);
        return NULL;
    }
    
    return pBufferRef;
}


/******************************************************************************/
static void LeafProReleaseCommandBuffer(
        LeafProCommandMsgBuf_t* pBufferRef
    )
{
    if ( pBufferRef != NULL ) {
        if (pBufferRef->bufferGDCqueueRef != NULL) {
            dispatch_release(pBufferRef->bufferGDCqueueRef);
        }
        free(pBufferRef->commandRef);
        free(pBufferRef);
    }
    return;
}


/******************************************************************************/
static UInt8 LeafProWriteCommandBuffer(
        LeafProCommandMsgBuf_t* pBufferRef,
        proCommand_t newCommand
    )
{
__block UInt8 retval = 1;
    
    dispatch_sync(pBufferRef->bufferGDCqueueRef, ^{
        if (LeafProTestFullCommandBuffer(pBufferRef)) {
            retval = 0;
        } else {
            pBufferRef->commandRef[(pBufferRef->bufferFirst +
                                    pBufferRef->bufferCount++)
                                   % pBufferRef->bufferSize] = newCommand;
        }
    });
    
    return retval;
}


/******************************************************************************/
static UInt8 LeafReadCommandBuffer(
        LeafProCommandMsgBuf_t* pBufferRef,
        proCommand_t* pReadCommand
    )
{
__block UInt8 retval = 1;
    
    dispatch_sync(pBufferRef->bufferGDCqueueRef, ^{
        if (LeafProTestEmptyCommandBuffer(pBufferRef)) {
            retval = 0;
        } else {
            pBufferRef->bufferCount--;
            *pReadCommand = pBufferRef->commandRef[pBufferRef->bufferFirst++
                                                   % pBufferRef->bufferSize];
        }
        
    });
    
    return retval;
    
}


/******************************************************************************/
static UInt8 LeafProTestFullCommandBuffer(
        LeafProCommandMsgBuf_t* pBufferRef
        )
{
    if (pBufferRef->bufferCount == pBufferRef->bufferSize) {
        return 1;
    } else {
        return 0;
    }
}


/******************************************************************************/
static UInt8 LeafProTestEmptyCommandBuffer(
        LeafProCommandMsgBuf_t* pBufferRef
    )
{
    if ( pBufferRef->bufferCount == 0 ) {
        return 1;
    } else {
        return 0;
    }
}


#pragma mark USB-Stuff
/******************************************************************************/
/******************************************************************************/
/**************************** USB Low Level Stuff *****************************/
/******************************************************************************/
/******************************************************************************/

/******************************************************************************/
static void LeafProBulkWriteCompletion(
        void *refCon,
        IOReturn result,
        void *arg0
    )
{
Can4osxUsbDeviceHandleEntry *self = (Can4osxUsbDeviceHandleEntry *)refCon;
IOUSBInterfaceInterface **interface = self->can4osxInterfaceInterface;
    
    UInt32 numBytesWritten = (UInt32) arg0;
    
    (void)numBytesWritten;
    
    CAN4OSX_DEBUG_PRINT("Asynchronous bulk write complete\n");
    
    if (result != kIOReturnSuccess) {
        CAN4OSX_DEBUG_PRINT("error from asynchronous bulk write (%08x)\n", result);
        (void) (*interface)->USBInterfaceClose(interface);
        (void) (*interface)->Release(interface);
        return;
    }
    
    self->endpoitBulkOutBusy = FALSE;
    
    CAN4OSX_DEBUG_PRINT("Wrote %ld bytes to bulk endpoint\n", (long)numBytesWritten);
    
    LeafProWriteBulkPipe(self);
    
    return;
}


/******************************************************************************/
static IOReturn LeafProWriteBulkPipe(
        Can4osxUsbDeviceHandleEntry *pSelf
    )
{
IOReturn retval = kIOReturnSuccess;
IOUSBInterfaceInterface **interface = pSelf->can4osxInterfaceInterface;
LeafProPrivateData_t *pPriv = (LeafProPrivateData_t *)pSelf->privateData;
    
    if ( pSelf->endpoitBulkOutBusy == FALSE ) {
        pSelf->endpoitBulkOutBusy = TRUE;
        
        if (0 < LeafProFillBulkPipeBuffer(pPriv->cmdBufferRef,
                                       pSelf->endpointBufferBulkOutRef,
                                       pSelf->endpointMaxSizeBulkOut )) {
            
            retval = (*interface)->WritePipeAsync(interface,
                                                  pSelf->endpointNumberBulkOut,
                                                  pSelf->endpointBufferBulkOutRef,
                                                  pSelf->endpointMaxSizeBulkOut,
                                                  LeafProBulkWriteCompletion,
                                                  (void*)pSelf);
            
            if (retval != kIOReturnSuccess) {
                CAN4OSX_DEBUG_PRINT("Unable to perform asynchronous bulk write (%08x)\n", retval);
                (void) (*interface)->USBInterfaceClose(interface);
                (void) (*interface)->Release(interface);
            }
        } else {
            pSelf->endpoitBulkOutBusy = FALSE;
        }
    }
    
    return retval;
}


/******************************************************************************/
static UInt16 LeafProFillBulkPipeBuffer(
        LeafProCommandMsgBuf_t* bufferRef,
        UInt8 *pPipe,
        UInt16 maxPipeSize
        )
{
UInt16 fillState = 0;
    
    while ( fillState < maxPipeSize ) {
        proCommand_t cmd;
        if ( LeafReadCommandBuffer(bufferRef, &cmd) ) {
            memcpy(pPipe, &cmd, LEAFPRO_COMMAND_SIZE);
            fillState += LEAFPRO_COMMAND_SIZE;
            pPipe += LEAFPRO_COMMAND_SIZE;
            //Will another command for in the pipe?
            if ( (fillState + LEAFPRO_COMMAND_SIZE) >= maxPipeSize ) {
                *pPipe = 0;
                break;
            }
            
        } else {
            *pPipe = 0;
            break;
        }
    }
    
    return fillState;
}


/******************************************************************************/
static IOReturn LeafProWriteCommandWait(
        Can4osxUsbDeviceHandleEntry *pSelf,
        proCommand_t cmd,
        UInt8 reason
        )
{
IOReturn retVal = kIOReturnSuccess;
IOUSBInterfaceInterface **interface = pSelf->can4osxInterfaceInterface;
LeafProPrivateData_t *pPriv = (LeafProPrivateData_t *)pSelf->privateData;
    
    if( pSelf->endpoitBulkOutBusy == FALSE ) {
        
        pSelf->endpoitBulkOutBusy = TRUE;
        
        retVal = (*interface)->WritePipe(interface,
                                         pSelf->endpointNumberBulkOut,
                                         &cmd, LEAFPRO_COMMAND_SIZE);
        
        if (retVal != kIOReturnSuccess) {
            CAN4OSX_DEBUG_PRINT("Unable to perform synchronous bulk write (%08x)\n", retVal);
            (void) (*interface)->USBInterfaceClose(interface);
            (void) (*interface)->Release(interface);
        }
        
        pSelf->endpoitBulkOutBusy = FALSE;
    } else {
        //Endpoint busy
        LeafProWriteCommandBuffer(pPriv->cmdBufferRef, cmd);
    }
    
    pPriv->timeOutReason = reason;
    
    if ( dispatch_semaphore_wait(pPriv->semaTimeout,
                                 dispatch_time(DISPATCH_TIME_NOW,
                                               LEAFPRO_TIMEOUT_TEN_MS)) )
    {
        return canERR_TIMEOUT;
    } else {
        return retVal;
    }
}


static void LeafProBulkReadCompletion(
        void *refCon,
        IOReturn result,
        void *arg0
        )
{
Can4osxUsbDeviceHandleEntry *pSelf = (Can4osxUsbDeviceHandleEntry *)refCon;
LeafProPrivateData_t *pPriv = (LeafProPrivateData_t *)pSelf->privateData;
IOUSBInterfaceInterface **interface = pSelf->can4osxInterfaceInterface;
UInt32 numBytesRead = (UInt32) arg0;
    
    CAN4OSX_DEBUG_PRINT("Asynchronous bulk read complete (%ld)\n",
                        (long)numBytesRead);
    
    if (result != kIOReturnSuccess) {
        printf("Error from async bulk read (%08x)\n", result);
        (void) (*interface)->USBInterfaceClose(interface);
        (void) (*interface)->Release(interface);
        return;
    }
    
    if (numBytesRead > 0u) {
        int count = 0;
        proCommand_t *pCmd;
        int loopCounter = pSelf->endpointMaxSizeBulkIn;
        
        while ( count < numBytesRead ) {
            if (loopCounter-- == 0) break;
            
            pCmd = (proCommand_t *)&(pSelf->endpointBufferBulkInRef[count]);
            
            if (pCmd->proCmdHead.cmdNo != 0u) {
                count += getCommandSize(pCmd);
                LeafProDecodeCommand(pSelf, pCmd);
            } else {
                /* No command */
                count += pSelf->endpointMaxSizeBulkIn;;
                count &= -pSelf->endpointMaxSizeBulkIn;
            }
            
            /* See if we had to wait */
            if (pCmd->proCmdHead.cmdNo == pPriv->timeOutReason) {
                pPriv->timeOutReason = 0;
                dispatch_semaphore_signal(pPriv->semaTimeout);
            }
        }
    }
    
    // Trigger next read
    LeafProReadFromBulkInPipe(pSelf);
}

static void LeafProReadFromBulkInPipe(
        Can4osxUsbDeviceHandleEntry *pSelf
        )
{
    IOReturn ret = (*(pSelf->can4osxInterfaceInterface))->ReadPipeAsync(pSelf->can4osxInterfaceInterface, pSelf->endpointNumberBulkIn, pSelf->endpointBufferBulkInRef, pSelf->endpointMaxSizeBulkIn, LeafProBulkReadCompletion, (void*)pSelf);
    
    if (ret != kIOReturnSuccess) {
        CAN4OSX_DEBUG_PRINT("Unable to read async interface (%08x)\n", ret);
    }
}
