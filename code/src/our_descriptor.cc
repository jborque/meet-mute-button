#include "our_descriptor.h"

const uint8_t our_report_descriptor[] = {

    TUD_HID_REPORT_DESC_TELEPHONY( HID_REPORT_ID(REPORT_ID_TELEPHONY) ),
    TUD_HID_REPORT_DESC_CUSTOM_CONSUMER( HID_REPORT_ID(REPORT_ID_CONSUMER_CONTROL )),

};

const uint32_t our_report_descriptor_length = sizeof(our_report_descriptor);


