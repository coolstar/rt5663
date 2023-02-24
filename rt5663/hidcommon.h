#if !defined(_RT5663_COMMON_H_)
#define _RT5663_COMMON_H_

//
//These are the device attributes returned by vmulti in response
// to IOCTL_HID_GET_DEVICE_ATTRIBUTES.
//

#define RT5663_PID              0x5663
#define RT5663_VID              0x10EC
#define RT5663_VERSION          0x0001

//
// These are the report ids
//

#define REPORTID_MEDIA	0x01
#define REPORTID_SPECKEYS		0x02

#pragma pack(1)
typedef struct _RT5663_MEDIA_REPORT
{

	BYTE      ReportID;

	BYTE	  ControlCode;

} Rt5663MediaReport;
#pragma pack()

#define CONTROL_CODE_JACK_TYPE 0x1

#pragma pack(1)
typedef struct _CSAUDIO_SPECKEY_REPORT
{

	BYTE      ReportID;

	BYTE	  ControlCode;

	BYTE	  ControlValue;

} CsAudioSpecialKeyReport;

#pragma pack()

#pragma pack(1)
typedef struct _CSAUDIO_SPECKEYREQ_REPORT
{

	BYTE      ReportID;

	BYTE	  AnyCode;

} CsAudioSpecialKeyRequestReport;
#pragma pack()

#endif
