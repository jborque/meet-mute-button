#ifndef _OUR_DESCRIPTOR_H_
#define _OUR_DESCRIPTOR_H_

#include <stdint.h>
#include <telephony_device.h>

enum
{
  REPORT_ID_TELEPHONY = 1,
  REPORT_ID_CONSUMER_CONTROL,
  REPORT_ID_COUNT
};


extern const uint8_t our_report_descriptor[];
extern const uint32_t our_report_descriptor_length;

#endif
