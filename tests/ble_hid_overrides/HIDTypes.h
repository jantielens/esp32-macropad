#pragma once

#define USAGE_PAGE(size) 0x04
#define USAGE(size) 0x08
#define COLLECTION(size) 0xA0
#define REPORT_ID(size) 0x84
#define USAGE_MINIMUM(size) 0x18
#define USAGE_MAXIMUM(size) 0x28
#define LOGICAL_MINIMUM(size) 0x14
#define LOGICAL_MAXIMUM(size) 0x24
#define REPORT_SIZE(size) 0x74
#define REPORT_COUNT(size) 0x94
#define HIDINPUT(size) 0x80
#define HIDOUTPUT(size) 0x90
#define END_COLLECTION(size) 0xC0