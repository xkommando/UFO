//
// Created by cbw on 9/26/16.
//
// configurations, see UFOContext::read_config()
//(Bowen 2017-10-13)
//

#ifndef UFO_DEFS_H
#define UFO_DEFS_H

// for test only, do not disable it
//#define BUF_EVENT_ON 1

// sync file on flush(), should always on
#define SYNC_AT_FLUSH


// enable statistic
#define STAT_ON 1


const unsigned long long UFO_VERSION = 20170901;

typedef unsigned char Byte;
typedef unsigned short TidType;

const unsigned int MAX_THREAD = 8192;

#define PACKED_STRUCT(NAME)\
  struct __attribute__ ((__packed__)) NAME

#define __HOT_CODE\
    __attribute__((optimize("unroll-loops")))\
    __attribute__((hot))\


// for benchmark only
// if disabled, will do nothing, compiler-rt framework overhead -> 2X
const char* const ENV_UFO_ON = "UFO_ON";

// assuming no stack variables are shared among threads.
// do not record or match mem acc events on stack or static region
const char* const ENV_NO_STACK_ACC = "UFO_NO_STACK";

//const int NO_STACK_ACC = 0;
const char * const ENV_NO_VALUE = "UFO_NO_VALUE";

const char* const ENV_TL_BUF_SIZE = "UFO_TL_BUF";
//const unsigned long DEFAULT_BUF_PRE_THR = 64; // 64 MB
const unsigned long DEFAULT_BUF_PRE_THR = 200; // 64 MB


const char * const ENV_USE_COMPRESS = "UFO_COMPRESS";
const int COMPRESS_ON = 1;

const char * const ENV_USE_IO_Q = "UFO_ASYNC_IO";
const int ASYNC_IO_ON = 0;

const char * const ENV_IO_Q_SIZE = "UFO_IO_Q";
const int DEFAULT_IO_Q_SIZE = 4;

const char* const ENV_TRACE_FUNC = "UFO_CALL";

const char* const ENV_PRINT_STAT = "UFO_STAT";

/* deprecated
const char* const ENV_PTR_PROP = "UFO_PTR_PROP";
const unsigned int DIR_MAX_LEN = 255;

const char* const NAME_MODULE_INFO = "/_module_info.txt";
const char* const NAME_STAT_FILE = "/_statistics.txt";
const char* const NAME_STAT_CSV = "/_statistics.csv";
*/

/**
 * dev stopped
 * memory threshold:
 * if threshold 1 is exceeded, reduce current tl buffer size by 2;
 * if threshold 2 is exceeded, reduce current tl buffer size by 4.
const char * const ENV_UFO_MEM_T1 = "UFO_MEM_T1";
const char * const ENV_UFO_MEM_T2 = "UFO_MEM_T2";

const unsigned long long DEFAULT_MEM_THRESHOLD_1 = 128ul * 80; // 10G
const unsigned long long DEFAULT_MEM_THRESHOLD_2 = 128ul * 120; // 15G

const unsigned long long MIN_BUF_SZ = 32 * 1024 * 1024;
 */


const unsigned long long DEFAULT_PTR_BUF_SIZE = 2 * 1024 * 1024;//2MB for pointers, 256K ptr

// max mem a thread can hold
// max mem defer dealloc, 4096MB(4G)
const unsigned long DEFAULT_HOLD_MEM_SIZE = (unsigned long) 1024 * 4;
//const unsigned long long DEFAULT_HOLD_MEM_SIZE = (unsigned long) 1024 * 1024 * 256;
const char* const ENV_MAX_MEM_HOLD = "UFO_MEM_HOLD";

// config tl mem in hold
const unsigned long long DEFAULT_ONLINE_THR_NUM = 200;

#endif //UFO_DEFS_H
