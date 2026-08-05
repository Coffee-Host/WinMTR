//*****************************************************************************
// FILE:            WinMTRGlobal.h
//
//
// DESCRIPTION:
//   
//
// NOTES:
//    
//
//*****************************************************************************

#ifndef GLOBAL_H_
#define GLOBAL_H_

#define VC_EXTRALEAN

#include <afxwin.h>
#include <afxext.h>
#include <afxdisp.h>
#include <afxdtctl.h>

#ifndef _AFX_NO_AFXCMN_SUPPORT
  #include <afxcmn.h>
#endif 
#include <afxsock.h>

#include <process.h>
#include <stdio.h>
#include <io.h> 
#include <time.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <memory.h>
#include <math.h>
#include <sys/types.h>
#include <sys/timeb.h>
#include <sys/stat.h>

#include "resource.h"
#include "WinMTRCustomization.h"

#define WINMTR_LICENSE	"GPL - GNU Public License"
#define WINMTR_COPYRIGHT "WinMTR (c) 2010-2011 Appnor MSP"

#define DEFAULT_PING_SIZE	64
#define DEFAULT_INTERVAL	1.0
#define DEFAULT_MAX_LRU		128
#define DEFAULT_DNS			TRUE
#define DEFAULT_MAX_HOPS        30
#define DEFAULT_TIMEOUT_MS      3000
#define DEFAULT_CYCLES          0
#define DEFAULT_TOS             0
#define DEFAULT_BIT_PATTERN     32
#define DEFAULT_DONT_FRAGMENT   TRUE
#define DEFAULT_ASN_LOOKUP      TRUE
#define DEFAULT_IPV4            TRUE
#define DEFAULT_IPV6            TRUE
#define DEFAULT_FIRST_TTL        1
#define DEFAULT_DUE_TTL          0
#define DEFAULT_MAX_UNKNOWN      5
#define DEFAULT_MAX_DISPLAY_PATHS 8
#define DEFAULT_CACHE_SECONDS    0
#define RECOMMENDED_SHARE_PACKETS 100

#define SAVED_PINGS 100
#define MaxHost 256
//#define MaxSequence 65536
#define MaxSequence 32767
//#define MaxSequence 5

#define MAXPACKET 4096
#define MINPACKET 64

#define MaxTransit 4

 
#define ICMP_ECHO		8
#define ICMP_ECHOREPLY		0

#define ICMP_TSTAMP		13
#define ICMP_TSTAMPREPLY	14

#define ICMP_TIME_EXCEEDED	11

#define ICMP_HOST_UNREACHABLE 3

#define MAX_UNKNOWN_HOSTS 10

#define IP_HEADER_LENGTH   20


#define MTR_NR_COLS 14
const UINT MTR_COL_RESOURCE_IDS[MTR_NR_COLS] = {
        IDS_COL_HOST,
        IDS_COL_HOP,
        IDS_COL_LOSS,
        IDS_COL_SENT,
        IDS_COL_RECEIVED,
        IDS_COL_BEST,
        IDS_COL_AVERAGE,
        IDS_COL_WORST,
        IDS_COL_LAST,
        IDS_COL_JITTER,
        IDS_COL_STDDEV,
        IDS_COL_COUNTRY,
        IDS_COL_ASN,
        IDS_COL_ISP
};
const int MTR_COL_LENGTH[ MTR_NR_COLS ] = {
        200, 35, 60, 45, 45, 45, 45, 45, 45, 50, 65, 50, 70, 160
};
#endif // ifndef GLOBAL_H_
