//
//  can4osx_usb_core.h
//
//
// Copyright (c) 2014 - 2018 Alexander Philipp. All rights reserved.
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

#ifndef CAN4OSX_USB_CORE_H
#define CAN4OSX_USB_CORE_H 1

#include <stdio.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>

#include "can4osx.h"
#include "can4osx_internal.h"


canStatus CAN4OSX_usbSendCommand(Can4osxUsbDeviceHandleEntry *pSelf, void *pCmd, size_t cmdLen);
void CAN4OSX_usbReadFromBulkInPipe(Can4osxUsbDeviceHandleEntry *pSelf);


#endif /* CAN4OSX_USB_CORE_H */
