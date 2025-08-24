#ifndef _TELEPHONY_DEVICE_H_
#define _TELEPHONY_DEVICE_H_

#include <class/hid/hid_device.h>

enum {
  HID_USAGE_TELEPHONY_HEADSET                           = 0x05,
  HID_USAGE_TELEPHONY_LED_MUTE                          = 0x09,
  HID_USAGE_TELEPHONY_LED_OFF_HOOK                      = 0x17,
  HID_USAGE_TELEPHONY_LED_RING                          = 0X18,
  HID_USAGE_TELEPHONY_LED_MICROPHONE                    = 0X21,
  HID_USAGE_TELEPHONY_HEADSET_HOOK_SWITCH               = 0x20,
  HID_USAGE_TELEPHONY_HEADSET_MUTE                      = 0x2F,

};

// Telephony Control Report Descriptor Template
#define TUD_HID_REPORT_DESC_TELEPHONY(...)                           \
  HID_USAGE_PAGE ( HID_USAGE_PAGE_TELEPHONY    )                    ,\
  HID_USAGE      ( HID_USAGE_TELEPHONY_HEADSET )                    ,\
  HID_COLLECTION ( HID_COLLECTION_APPLICATION  )                    ,\
      /* Report ID if any */\
    __VA_ARGS__ \
    HID_LOGICAL_MIN  ( 0x00                                       ) ,\
    HID_LOGICAL_MAX  ( 0x01                                       ) ,\
    HID_USAGE        ( HID_USAGE_TELEPHONY_HEADSET_MUTE           ) ,\
    HID_REPORT_COUNT ( 1                                          ) ,\
    HID_REPORT_SIZE  ( 1                                          ) ,\
    HID_INPUT        ( HID_DATA | HID_VARIABLE | HID_RELATIVE     ) ,\
    HID_USAGE        ( HID_USAGE_TELEPHONY_HEADSET_HOOK_SWITCH    ) ,\
    HID_REPORT_COUNT ( 1                                          ) ,\
    HID_REPORT_SIZE  ( 1                                          ) ,\
    HID_INPUT        ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE | HID_PREFERRED_NO ) ,\
    HID_REPORT_COUNT ( 1                                          ) ,\
    HID_REPORT_SIZE  ( 6                                          ) ,\
    HID_INPUT        ( HID_CONSTANT | HID_ARRAY | HID_ABSOLUTE ) ,\
    HID_USAGE_PAGE   ( HID_USAGE_PAGE_LED                         ) ,\
    HID_USAGE        ( HID_USAGE_TELEPHONY_LED_OFF_HOOK           ) ,\
    HID_USAGE        ( HID_USAGE_TELEPHONY_LED_MUTE               ) ,\
    HID_REPORT_COUNT ( 2                                          ) ,\
    HID_REPORT_SIZE  ( 1                                          ) ,\
    HID_OUTPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE     ) ,\
    HID_REPORT_COUNT ( 1                                          ) ,\
    HID_REPORT_SIZE  ( 6                                          ) ,\
    HID_OUTPUT       ( HID_CONSTANT | HID_ARRAY | HID_ABSOLUTE ) ,\
    HID_COLLECTION_END \

#define TUD_HID_REPORT_DESC_CUSTOM_CONSUMER(...)                     \
  HID_USAGE_PAGE ( HID_USAGE_PAGE_CONSUMER    )              ,\
  HID_USAGE      ( HID_USAGE_CONSUMER_CONTROL )              ,\
  HID_COLLECTION ( HID_COLLECTION_APPLICATION )              ,\
    /* Report ID if any */\
    __VA_ARGS__ \
    HID_LOGICAL_MIN  ( 0x00                                       ) ,\
    HID_LOGICAL_MAX  ( 0x01                                       ) ,\
    HID_USAGE        ( HID_USAGE_CONSUMER_VOLUME_INCREMENT        ) ,\
    HID_USAGE        ( HID_USAGE_CONSUMER_VOLUME_DECREMENT        ) ,\
    HID_REPORT_SIZE  ( 1                                          ) ,\
    HID_REPORT_COUNT ( 2                                          ) ,\
    HID_INPUT        ( HID_DATA | HID_VARIABLE | HID_RELATIVE     ) ,\
    HID_REPORT_COUNT ( 6                                          ) ,\
    HID_INPUT        ( HID_CONSTANT | HID_ARRAY | HID_ABSOLUTE    ) ,\
  HID_COLLECTION_END \



#endif
