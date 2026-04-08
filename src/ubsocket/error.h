// Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
// Description: The universal error type used in ubsocket.

#ifndef UBSOCKET_ERROR_H
#define UBSOCKET_ERROR_H

#include <cstdint>

namespace ubsocket {
enum class Error : uint32_t {
    kOK = 0,
    kPERM = 1,               // Operation not permitted
    kENOENT = 2,             // No such file or directory
    kESRCH = 3,              // No such process
    kEINTR = 4,              // Interrupted system call
    kEIO = 5,                // Input/output error
    kENXIO = 6,              // No such device or address
    kE2BIG = 7,              // Argument list too long
    kENOEXEC = 8,            // Exec format error
    kEBADF = 9,              // Bad file descriptor
    kECHILD = 10,            // No child processes
    kEAGAIN = 11,            // Resource temporarily unavailable
    kENOMEM = 12,            // Cannot allocate memory
    kEACCES = 13,            // Permission denied
    kEFAULT = 14,            // Bad address
    kENOTBLK = 15,           // Block device required
    kEBUSY = 16,             // Device or resource busy
    kEEXIST = 17,            // File exists
    kEXDEV = 18,             // Invalid cross-device link
    kENODEV = 19,            // No such device
    kENOTDIR = 20,           // Not a directory
    kEISDIR = 21,            // Is a directory
    kEINVAL = 22,            // Invalid argument
    kENFILE = 23,            // Too many open files in system
    kEMFILE = 24,            // Too many open files
    kENOTTY = 25,            // Inappropriate ioctl for device
    kETXTBSY = 26,           // Text file busy
    kEFBIG = 27,             // File too large
    kENOSPC = 28,            // No space left on device
    kESPIPE = 29,            // Illegal seek
    kEROFS = 30,             // Read-only file system
    kEMLINK = 31,            // Too many links
    kEPIPE = 32,             // Broken pipe
    kEDOM = 33,              // Numerical argument out of domain
    kERANGE = 34,            // Numerical result out of range
    kEDEADLK = 35,           // Resource deadlock avoided
    kENAMETOOLONG = 36,      // File name too long
    kENOLCK = 37,            // No locks available
    kENOSYS = 38,            // Function not implemented
    kENOTEMPTY = 39,         // Directory not empty
    kELOOP = 40,             // Too many levels of symbolic links
    kEWOULDBLOCK = 11,       // Resource temporarily unavailable
    kENOMSG = 42,            // No message of desired type
    kEIDRM = 43,             // Identifier removed
    kECHRNG = 44,            // Channel number out of range
    kEL2NSYNC = 45,          // Level 2 not synchronized
    kEL3HLT = 46,            // Level 3 halted
    kEL3RST = 47,            // Level 3 reset
    kELNRNG = 48,            // Link number out of range
    kEUNATCH = 49,           // Protocol driver not attached
    kENOCSI = 50,            // No CSI structure available
    kEL2HLT = 51,            // Level 2 halted
    kEBADE = 52,             // Invalid exchange
    kEBADR = 53,             // Invalid request descriptor
    kEXFULL = 54,            // Exchange full
    kENOANO = 55,            // No anode
    kEBADRQC = 56,           // Invalid request code
    kEBADSLT = 57,           // Invalid slot
    kEDEADLOCK = 35,         // Resource deadlock avoided
    kEBFONT = 59,            // Bad font file format
    kENOSTR = 60,            // Device not a stream
    kENODATA = 61,           // No data available
    kETIME = 62,             // Timer expired
    kENOSR = 63,             // Out of streams resources
    kENONET = 64,            // Machine is not on the network
    kENOPKG = 65,            // Package not installed
    kEREMOTE = 66,           // Object is remote
    kENOLINK = 67,           // Link has been severed
    kEADV = 68,              // Advertise error
    kESRMNT = 69,            // Srmount error
    kECOMM = 70,             // Communication error on send
    kEPROTO = 71,            // Protocol error
    kEMULTIHOP = 72,         // Multihop attempted
    kEDOTDOT = 73,           // RFS specific error
    kEBADMSG = 74,           // Bad message
    kEOVERFLOW = 75,         // Value too large for defined data type
    kENOTUNIQ = 76,          // Name not unique on network
    kEBADFD = 77,            // File descriptor in bad state
    kEREMCHG = 78,           // Remote address changed
    kELIBACC = 79,           // Can not access a needed shared library
    kELIBBAD = 80,           // Accessing a corrupted shared library
    kELIBSCN = 81,           // .lib section in a.out corrupted
    kELIBMAX = 82,           // Attempting to link in too many shared libraries
    kELIBEXEC = 83,          // Cannot exec a shared library directly
    kEILSEQ = 84,            // Invalid or incomplete multibyte or wide character
    kERESTART = 85,          // Interrupted system call should be restarted
    kESTRPIPE = 86,          // Streams pipe error
    kEUSERS = 87,            // Too many users
    kENOTSOCK = 88,          // Socket operation on non-socket
    kEDESTADDRREQ = 89,      // Destination address required
    kEMSGSIZE = 90,          // Message too long
    kEPROTOTYPE = 91,        // Protocol wrong type for socket
    kENOPROTOOPT = 92,       // Protocol not available
    kEPROTONOSUPPORT = 93,   // Protocol not supported
    kESOCKTNOSUPPORT = 94,   // Socket type not supported
    kEOPNOTSUPP = 95,        // Operation not supported
    kEPFNOSUPPORT = 96,      // Protocol family not supported
    kEAFNOSUPPORT = 97,      // Address family not supported by protocol
    kEADDRINUSE = 98,        // Address already in use
    kEADDRNOTAVAIL = 99,     // Cannot assign requested address
    kENETDOWN = 100,         // Network is down
    kENETUNREACH = 101,      // Network is unreachable
    kENETRESET = 102,        // Network dropped connection on reset
    kECONNABORTED = 103,     // Software caused connection abort
    kECONNRESET = 104,       // Connection reset by peer
    kENOBUFS = 105,          // No buffer space available
    kEISCONN = 106,          // Transport endpoint is already connected
    kENOTCONN = 107,         // Transport endpoint is not connected
    kESHUTDOWN = 108,        // Cannot send after transport endpoint shutdown
    kETOOMANYREFS = 109,     // Too many references: cannot splice
    kETIMEDOUT = 110,        // Connection timed out
    kECONNREFUSED = 111,     // Connection refused
    kEHOSTDOWN = 112,        // Host is down
    kEHOSTUNREACH = 113,     // No route to host
    kEALREADY = 114,         // Operation already in progress
    kEINPROGRESS = 115,      // Operation now in progress
    kESTALE = 116,           // Stale file handle
    kEUCLEAN = 117,          // Structure needs cleaning
    kENOTNAM = 118,          // Not a XENIX named type file
    kENAVAIL = 119,          // No XENIX semaphores available
    kEISNAM = 120,           // Is a named type file
    kEREMOTEIO = 121,        // Remote I/O error
    kEDQUOT = 122,           // Disk quota exceeded
    kENOMEDIUM = 123,        // No medium found
    kEMEDIUMTYPE = 124,      // Wrong medium type
    kECANCELED = 125,        // Operation canceled
    kENOKEY = 126,           // Required key not available
    kEKEYEXPIRED = 127,      // Key has expired
    kEKEYREVOKED = 128,      // Key has been revoked
    kEKEYREJECTED = 129,     // Key was rejected by service
    kEOWNERDEAD = 130,       // Owner died
    kENOTRECOVERABLE = 131,  // State not recoverable
    kERFKILL = 132,          // Operation not possible due to RF-kill
    kEHWPOISON = 133,        // Memory page has hardware error
    kENOTSUP = 95,           // Operation not supported

    // UMQ 相关错误
    kUMQ_CREATE = 1025,
    kUMQ_DEV_ADD,
    kUMQ_BIND_INFO_GET,
    kUMQ_BIND,
    kUMQ_BUF_ALLOC,
    kUMQ_POST,
    kUMQ_MAX,

    // ubsocket 相关错误
    kUBSOCKET_SET_DEV_INFO = 4097,
    kUBSOCKET_PREFILL_RX,
    kUBSOCKET_INIT_SHARED_JFR_RX_QUEUE,
    kUBSOCKET_NEW_SOCKET_FD,
    kUBSOCKET_TCP_EXCHANGE,
    kUBSOCKET_UB_ACCEPT,
    kUBSOCKET_MAX,

    // 特殊标记位，可与上述 2 种错误共存
    kRETRYABLE = 0x40000000,   // 可重试错误
    kDEGRADABLE = 0x20000000,  // 可降级错误
};

inline Error operator|(Error lhs, Error rhs)
{
    const uint32_t e = static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs);
    return static_cast<Error>(e);
}

inline Error operator-(Error lhs, Error rhs)
{
    const uint32_t e = static_cast<uint32_t>(lhs) & (~static_cast<uint32_t>(rhs));
    return static_cast<Error>(e);
}

inline Error WithoutFlags(Error err)
{
    return err - Error::kRETRYABLE - Error::kDEGRADABLE;
}

inline bool Retryable(Error err)
{
    const uint32_t e = static_cast<uint32_t>(err);
    return e & static_cast<uint32_t>(Error::kRETRYABLE);
}

inline bool Degradable(Error err)
{
    const uint32_t e = static_cast<uint32_t>(err);
    return e & static_cast<uint32_t>(Error::kDEGRADABLE);
}

inline bool IsOk(Error err)
{
    return err == Error::kOK;
}

inline bool IsPosixError(Error err)
{
    const uint32_t e = static_cast<uint32_t>(WithoutFlags(err));
    return e > 0 && e < static_cast<uint32_t>(Error::kUMQ_CREATE);
}

inline bool IsUmqError(Error err)
{
    const uint32_t e = static_cast<uint32_t>(WithoutFlags(err));
    return e >= static_cast<uint32_t>(Error::kUMQ_CREATE) && e < static_cast<uint32_t>(Error::kUMQ_MAX);
}

inline bool IsUbsocketError(Error err)
{
    const uint32_t e = static_cast<uint32_t>(WithoutFlags(err));
    return e >= static_cast<uint32_t>(Error::kUBSOCKET_SET_DEV_INFO) && e < static_cast<uint32_t>(Error::kUBSOCKET_MAX);
}

inline int Raw(Error err)
{
    const uint32_t e = static_cast<uint32_t>(err);
    return static_cast<int>(e);
}

inline Error FromRaw(int err)
{
    const uint32_t e = static_cast<uint32_t>(err);
    return static_cast<Error>(e);
}

}  // namespace ubsocket

#endif
