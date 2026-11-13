#pragma once
/* Fixed-width, pointer-free ABI shared by the host and KMDF probe. */
#define LHDC_ABI_VERSION 4u
#define LHDC_AVDTP_PSM 0x0019u
#define LHDC_AVCTP_PSM 0x0017u
#define LHDC_MAX_SDU 4096u
/* Bound outstanding Bluetooth transfers independently of the PCM queue.
   The service keeps at least this many packets in flight and grows the window
   until the outstanding media covers LHDC_MEDIA_SEND_DEPTH_MS of audio at the
   negotiated bitrate. A 1000 kbps LHDC stream submits about 190 packets per
   second, so four packets only cover 21 ms there; a slow Bluetooth completion
   outruns that depth, the media thread stalls, and the dropped PCM surfaces
   as audible stutter. Measuring the depth in time keeps a low bitrate at its
   previous shallow window while a high bitrate gains the room it needs. */
#define LHDC_MEDIA_SEND_WINDOW 4u
#define LHDC_MEDIA_SEND_DEPTH_MS 96u
#define LHDC_MEDIA_SEND_WINDOW_MAX 32u
/* Bluetooth transfers the transport driver may hand to the stack at once.
   This is a driver-side limit and is independent of the service window
   above: the service may believe that many packets are outstanding while
   only this many BRBs are actually in flight. The value is read from the
   registry so one build can be swept across the range. */
#define LHDC_TRANSPORT_INFLIGHT_DEFAULT 4u
#define LHDC_TRANSPORT_INFLIGHT_MAX 64u
#define LHDC_TRANSPORT_INFLIGHT_REGISTRY L"MediaInflightBrbs"
#define LHDC_CHANNEL_SIGNAL 1u
#define LHDC_CHANNEL_MEDIA 2u
#define LHDC_CHANNEL_AVRCP 3u
#define LHDC_CHANNEL_COUNT 3u
#define IOCTL_LHDC_QUERY_INFO CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_LHDC_OPEN CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
#define IOCTL_LHDC_CLOSE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
#define IOCTL_LHDC_SEND CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_WRITE_DATA)
#define IOCTL_LHDC_RECEIVE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x804, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_LHDC_AUDIO_PRESENT CTL_CODE(FILE_DEVICE_UNKNOWN, 0x805, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
/* {D94DF262-37A7-40EF-8EA0-C3B72DE69ED7} */
DEFINE_GUID(GUID_DEVINTERFACE_LHDC_TRANSPORT,
    0xd94df262, 0x37a7, 0x40ef, 0x8e, 0xa0, 0xc3, 0xb7, 0x2d, 0xe6, 0x9e, 0xd7);
typedef struct LHDC_HEADER {
    ULONG Size;
    ULONG Version;
} LHDC_HEADER;
typedef struct LHDC_AUDIO_PRESENT_INPUT {
    LHDC_HEADER Header;
    ULONG Present;
    ULONG Reserved;
} LHDC_AUDIO_PRESENT_INPUT;
typedef struct LHDC_OPEN_INPUT {
    LHDC_HEADER Header;
    ULONGLONG ExpectedRemoteAddress;
    ULONG ChannelId;
    ULONG Reserved;
} LHDC_OPEN_INPUT;
typedef struct LHDC_CHANNEL_INPUT {
    LHDC_HEADER Header;
    ULONG ChannelId;
    ULONG Reserved;
} LHDC_CHANNEL_INPUT;
typedef struct LHDC_INFO {
    LHDC_HEADER Header;
    ULONGLONG LocalAddress;
    ULONGLONG RemoteAddress;
    ULONG Connected;
    ULONG ChannelId;
    ULONG InMtu;
    ULONG OutMtu;
    LONG LastNtStatus;
    ULONG LastBtStatus;
    ULONGLONG SubmittedBytes;
    ULONGLONG CompletedBytes;
    ULONGLONG ReceivedBytes;
    ULONG PendingRequests;
    ULONG CancelledRequests;
    /* Diagnostics for the media path. AclMode is the Bluetooth link mode
       read when the channel opened; MaxBrbLatencyUs is the slowest observed
       submission-to-completion time of a media BRB, LastBrbLatencyUs the
       most recent one and BrbCompletions counts the completed transfers.
       InflightBrbs is the driver-side concurrency limit in force. */
    ULONG AclMode;
    ULONG InflightBrbs;
    ULONGLONG MaxBrbLatencyUs;
    ULONGLONG LastBrbLatencyUs;
    ULONGLONG BrbCompletions;
} LHDC_INFO;
typedef struct LHDC_SDU_HEADER {
    LHDC_HEADER Header;
    ULONG ChannelId;
    ULONG PayloadSize;
} LHDC_SDU_HEADER;
