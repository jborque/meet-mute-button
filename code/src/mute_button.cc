#include <bsp/board.h>
#include <tusb.h>

#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <pico/bootrom.h>
#include <pico/unique_id.h>
#include <pico/stdio.h>

#include "our_descriptor.h"
#include "ws2812.h"

#include "me.h"

// Neopixel definitions
#define IS_RGBW false
#define WS2812_PIN 2
#define NUM_PIXELS 1

#define BLINK_ON_INTERVAL 1000
#define BLINK_NOT_MOUNTED 100
#define BLINK_MOUNTED 5000  
#define BLINK_SUSPENDED 20000
#define BLINK_STEP 100



// Button definitions
#define MUTE_BUTTON_PIN 1 << 19
#define HOOK_BUTTON_PIN 1 << 21
#define VOLU_BUTTON_PIN 1 << 18
#define VOLD_BUTTON_PIN 1 << 20

const uint32_t BUTTON_MASK = MUTE_BUTTON_PIN | HOOK_BUTTON_PIN | VOLU_BUTTON_PIN | VOLD_BUTTON_PIN;


// State Definitions
#define STATE_USB_ON          0x01 << 0
#define STATE_USB_MOUNTED     0x01 << 1
#define STATE_USB_SUSPENDED   0x01 << 2
#define STATE_USB_READY       0x01 << 3
#define STATE_MUTE            0x01 << 4
#define STATE_ONCALL          0x01 << 5

static uint8_t state = 0x00;

// Queue definitions
#define Q_LENGTH 10

#define NOTHING   0
#define MUTE_DOWN 1
#define MUTE_UP   2
#define HOOK_DOWN 3
#define HOOK_UP   4
#define VOLU_DOWN 5
#define VOLD_DOWN 6
#define VOLX_UP   7

static uint8_t queue[Q_LENGTH] = {0};
static uint8_t queue_start=0;
static uint8_t queue_end=0;

void led_init(uint pin, bool isRGBW);
void led_toggle(uint32_t color);
void led_blink(uint32_t color);

void button_init(uint button);
void buttons_init();

void led_task(void);
void button_task(void);
void hid_task(void);

int main() {

    board_init();
    me_init();
    stdio_init_all();
    led_init(WS2812_PIN, IS_RGBW);
    buttons_init();
    tusb_init();


    sleep_ms(10);
    if (!gpio_get(19)) {
        sleep_ms(200);
        led_blink(0xffff00);
        sleep_ms(200);
        reset_usb_boot(0, 0);
    }
    
    static char serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    pico_get_unique_board_id_string(serial_str, sizeof(serial_str));

    printf("Shhh - Mute button 0x01\nSerial: %s\n",serial_str);

    led_blink(0x0f0f0f);
    sleep_ms(200);

    while (true) {
        tud_task();
        led_task();    
        button_task();    
        hid_task();
    }

    return 0;

}


void q_push(uint8_t u) {
    uint8_t next_queue_end=queue_end;
    if( ++next_queue_end >= Q_LENGTH ) next_queue_end = 0;    
    if( next_queue_end == queue_start ) {
        printf("Queue Full: Ignoring");
        return;
    }
    queue[queue_end]=u;
    queue_end=next_queue_end;
}

uint8_t q_pop(void) {
    if( queue_start == queue_end ) return NOTHING;
    uint8_t ret = queue[queue_start];
    queue[queue_start] = 0;
    if( ++queue_start >= Q_LENGTH ) queue_start = 0;
    return ret;
}

void set_state(uint8_t s) {
    state |= s;
}

void unset_state(uint8_t s) {
    state &= ~s;
}

bool get_state(uint8_t s) {
    return s & state;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void) {
    set_state(STATE_USB_MOUNTED);
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
    unset_state(STATE_USB_ON);
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
    (void) remote_wakeup_en;
    set_state(STATE_USB_SUSPENDED);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
    unset_state(STATE_USB_SUSPENDED);
    if (tud_mounted()) {
        set_state(STATE_USB_MOUNTED);
    } else {
        unset_state(STATE_USB_MOUNTED);
    }
}

// Invoked when HID report set
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    printf("tud_hid_set_report_cb: itf=%u report_id=%u  report_type=%u bufsize=%u\n",itf,report_id,report_type,bufsize);    
    if (bufsize >= 1 && report_id == REPORT_ID_TELEPHONY ) {
        
        if(buffer[0] & 0x01) set_state(STATE_ONCALL);
        else unset_state(STATE_ONCALL);
        
        if( buffer[0] & 0x02 ) set_state(STATE_MUTE);
        else unset_state(STATE_MUTE);
        
        printf("tud_hid_set_report_cb: state=%u\n",state);

        if (get_state(STATE_ONCALL)) {
            q_push(HOOK_UP);
            printf("tud_hid_set_report_cb: HOOK_UP");
        }

    }

}

// Invoked when HID report get
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    return 0;
}

void button_task() {
    // Poll every 10ms
    const uint32_t interval_ms = 100;
    uint32_t current_ms = board_millis();
    static uint32_t start_ms = 0;
    static uint32_t pressed_ms = 0;

    static uint32_t prev_button_state = 0x000000;

    if ( current_ms - start_ms < interval_ms || !tud_hid_ready() ) return; // not enough time
    
    start_ms += interval_ms;
    
    //if ( !( state & STATE_ONCALL ) ) return; // Don't do anything if not in a call.
    
    uint32_t button_state = ( ~ gpio_get_all() ) & BUTTON_MASK;
    
    if ( button_state == prev_button_state ) return;

    if ( (button_state & MUTE_BUTTON_PIN) && !(prev_button_state & MUTE_BUTTON_PIN) ) {
        if(current_ms-pressed_ms<500) {
            q_push(HOOK_DOWN);
            printf("HOOK_DOWN\n");
        }
        q_push(MUTE_DOWN);            
        printf("MUTE_DOWN\n");
        pressed_ms = current_ms;
    }

    else if ( !(button_state & MUTE_BUTTON_PIN) && (prev_button_state & MUTE_BUTTON_PIN) ) {
        if (!(state & STATE_MUTE) && (current_ms-pressed_ms > 500)) {
            q_push(MUTE_UP);
            printf("MUTE_UP\n");
            q_push(MUTE_DOWN);            
            printf("MUTE_DOWN\n");
        }
        q_push(MUTE_UP);
        printf("MUTE_UP\n");
        pressed_ms = current_ms;
    }
    
    else if ( (button_state & HOOK_BUTTON_PIN) && !(prev_button_state & HOOK_BUTTON_PIN) ) {
        q_push(HOOK_DOWN);
        printf("HOOK_DOWN\n");
    }
    
    else if ( !(button_state & HOOK_BUTTON_PIN) && (prev_button_state & HOOK_BUTTON_PIN) ) {
        q_push(HOOK_UP);
        printf("HOOK_UP\n");
    }
    
    else if ( (button_state & VOLD_BUTTON_PIN) && !(prev_button_state & VOLD_BUTTON_PIN) ) {
        q_push(VOLD_DOWN);
        printf("VOLD_DOWN\n");
    }
    
    else if ( (button_state & VOLU_BUTTON_PIN) && !(prev_button_state & VOLU_BUTTON_PIN) ) {
        q_push(VOLU_DOWN);
        printf("VOLU_DOWN\n");
    }
    
    else if ( !(button_state & (VOLU_BUTTON_PIN | VOLD_BUTTON_PIN) ) && (prev_button_state & (VOLU_BUTTON_PIN | VOLD_BUTTON_PIN) ) ) {
        q_push(VOLX_UP);
        printf("VOLX_UP\n");
    }
    prev_button_state = button_state;

}


void hid_task() {
    static uint8_t t_report=0x00;
    static uint8_t prev_t_report=t_report;

    static uint16_t c_report=0x00;
    static uint16_t prev_c_report=c_report;

    if (!tud_ready()) {
        unset_state(STATE_USB_READY);
        return;
    }

    set_state(STATE_USB_READY);

    if ( !tud_hid_ready() ) return; 
    
    switch (q_pop())
    {
        case MUTE_DOWN:
        t_report |= 0x01;
        break;
        case MUTE_UP:
        t_report &= ~0x01;
        break;        
    case HOOK_DOWN:
        t_report |= 0x02;
        break;
    case HOOK_UP:
        t_report &= ~0x02;
        break;  
    case VOLD_DOWN:
        c_report = HID_USAGE_CONSUMER_VOLUME_DECREMENT;
        break;
    case VOLU_DOWN:
        c_report = HID_USAGE_CONSUMER_VOLUME_INCREMENT;
        break;
    case VOLX_UP:
        c_report = 0;
        break;
    case NOTHING:
    default:
        return;
        break;
    }
    
    if ( prev_t_report != t_report ) {
        tud_hid_report(REPORT_ID_TELEPHONY, &t_report, 1);
        prev_t_report = t_report;
        return;
    }

    if ( prev_c_report != c_report ) {
        tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &c_report, 1);
        prev_c_report = c_report;
        return;
    }

}


void put_all_pixels(uint32_t pixel_grb) {
    for(uint8_t i=0; i<NUM_PIXELS; i++) {
        put_pixel(pixel_grb);
    }
}

//--------------------------------------------------------------------+
// LED TASK
//--------------------------------------------------------------------+
void led_task(void) {
    
    static uint32_t start_ms = board_millis();
    static uint32_t interval_ms = 20;
    static int fade = 5;
    static bool going_up = true;
    static bool must_update = true;
    
    if( !get_state(STATE_ONCALL) ) {
        if ( ( board_millis() - start_ms < interval_ms ) ) return;
        start_ms += interval_ms;
        interval_ms = BLINK_STEP;
        if (going_up) {
            fade+=1;
            if (fade > 10) {
                fade = 10;
                going_up = false;
            }
        } else {
            fade-=1;
            if (fade < 6) {
                fade = 5;
                going_up = true;
                if (get_state(STATE_USB_MOUNTED)) {
                    interval_ms = BLINK_MOUNTED;
                } else if (get_state(STATE_USB_SUSPENDED)) {
                    interval_ms = BLINK_SUSPENDED;
                } else {
                    interval_ms = BLINK_NOT_MOUNTED;
                }
            }
        }
        uint32_t c = fade;
        c <<= 8;
        c |= fade;
        c <<= 8;
        c |= fade;
        put_all_pixels(c);
        must_update = true;
    } else {
        interval_ms = 20;
        if ( ( board_millis() - start_ms < interval_ms ) ) return;
        start_ms += interval_ms;
        static bool prev_mute_state = true;
        bool mute_state = get_state(STATE_MUTE);
        must_update |= prev_mute_state != mute_state;
        if ( must_update ) {
            if(get_state(STATE_MUTE)) {
                put_all_pixels(0x00ff00);
            } else {
                put_all_pixels(0xff0000);
            }
            prev_mute_state = mute_state;
            must_update = false;
        }
    }


}

//--------------------------------------------------------------------+
// Init GPIO output leds with LED_MASK
//--------------------------------------------------------------------+
void led_init(uint pin, bool isRGBW) {
    neopixel_init(pin, isRGBW);
    for (int i = 0; i < NUM_PIXELS; i++) {
        put_all_pixels(0x000000);
    }

}    

//--------------------------------------------------------------------+
// Toggle leds defined in mask
//--------------------------------------------------------------------+
void led_toggle(uint32_t color) {
   
}

//--------------------------------------------------------------------+
// Blink leds defined in mask
//--------------------------------------------------------------------+
void led_blink(uint32_t color) {
    put_all_pixels(color);
    sleep_ms(200);
    put_all_pixels(0x000000);   
}

void button_init(uint button) {
    gpio_init(button);
    gpio_set_dir(button, GPIO_IN);
    gpio_pull_up(button);
}

void buttons_init() {
    
    gpio_init_mask(BUTTON_MASK);
    gpio_set_dir_in_masked(BUTTON_MASK);

    for ( uint8_t pin = 0; pin < NUM_BANK0_GPIOS; pin ++) {
        if ( ( BUTTON_MASK >> pin ) & 0x000001 ) {
            gpio_pull_up(pin);
        }
    }

}

