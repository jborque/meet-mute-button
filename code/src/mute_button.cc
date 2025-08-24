#include <bsp/board.h>
#include <tusb.h>

#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <pico/bootrom.h>
#include <pico/unique_id.h>
#include <pico/stdio.h>

#include "our_descriptor.h"
#include "me.h"

#define MUTE_BUTTON_PIN 1 << 19
#define HOOK_BUTTON_PIN 1 << 21
#define VOLU_BUTTON_PIN 1 << 18
#define VOLD_BUTTON_PIN 1 << 20

const uint32_t BUTTON_MASK = MUTE_BUTTON_PIN | HOOK_BUTTON_PIN | VOLU_BUTTON_PIN | VOLD_BUTTON_PIN;

#define PWM_LED_PIN 11
#define LED_B_PIN 1 << 11
#define LED_G_PIN 1 << 12
#define LED_R_PIN 1 << 13

//#define LED_MASK LED_B_PIN | LED_G_PIN | LED_R_PIN
#define LED_MASK LED_G_PIN | LED_R_PIN

#define BLINK_ON_INTERVAL 45
#define BLINK_NOT_MOUNTED 250
#define BLINK_MOUNTED 3000  
#define BLINK_SUSPENDED 1500

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

static uint8_t led_usb_state = 0x00;
static uint8_t led_mute_state = 0x00;
static uint8_t led_oncall_state = 0x00;

uint slice_num;

void led_init(uint32_t led);
void led_toggle(uint32_t led);
void led_blink(uint32_t led);
void leds_init();
void leds_update();

void button_init(uint button);
void buttons_init();

void led_blinking_task(void);
void hid_task(void);

int main() {
    board_init();
    me_init();
    stdio_init_all();
    leds_init();
    buttons_init();
    tusb_init();

    static char serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    pico_get_unique_board_id_string(serial_str, sizeof(serial_str));

    printf("Shhh - Mute button 0x01\nSerial: %s\n",serial_str);
    
    led_blink(LED_MASK);

    gpio_set_function(PWM_LED_PIN, GPIO_FUNC_PWM);
    slice_num = pwm_gpio_to_slice_num(PWM_LED_PIN);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 8.f);
    pwm_init(slice_num, &config, true);

    while (true) {
        tud_task();
        led_blinking_task();        
        hid_task();
    }

    return 0;

}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void) {
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en) {
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void) {
  blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

// Invoked when HID report set
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    printf("tud_hid_set_report_cb: itf=%u report_id=%u  report_type=%u bufsize=%u\n",itf,report_id,report_type,bufsize);    
    if (bufsize >= 1 && report_id == REPORT_ID_TELEPHONY ) {
        led_oncall_state = buffer[0] & 0x01;
        led_mute_state = ( buffer[0] & 0x02 ) >> 1;
        printf("tud_hid_set_report_cb: led_oncall_state=%u led_mute_state=%u\n",led_oncall_state,led_mute_state);
        leds_update();

        static uint8_t prev_led_oncall_state=0x00;
        if (prev_led_oncall_state != led_oncall_state) {
            uint8_t hook = ( led_oncall_state << 1 );
            tud_hid_report(REPORT_ID_TELEPHONY, &hook, 1);
            printf("REPORT_ID_TELEPHONY: tud_hid_set_report_cb led_oncall_state=%u hook=%u\n",led_oncall_state,hook);
        }
        prev_led_oncall_state=led_oncall_state;

    }

}

// Invoked when HID report get
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    return 0;
}

void hid_task() {

    // Poll every 10ms
    const uint32_t interval_ms = 10;
    uint32_t current_ms = board_millis();
    static uint32_t start_ms = 0;
    static uint32_t pressed_ms = 0;

    static uint32_t prev_button_state = 0x000000;

    start_ms += interval_ms;
    
    if (!tud_ready()) {
        led_mute_state = 0x00;
        led_oncall_state = 0x00;
        return;
    }
    
    if ( current_ms - start_ms < interval_ms || !tud_hid_ready() ) return; // not enough time

    if ( ! led_oncall_state ) return; // Don't do anything if not in a call.

    uint32_t button_state = ( ~ gpio_get_all() ) & BUTTON_MASK;
    
    if ( button_state == prev_button_state ) return;

    if ( (button_state & MUTE_BUTTON_PIN) && !(prev_button_state & MUTE_BUTTON_PIN) ) {
        if(current_ms-pressed_ms<500) {
            uint8_t hook = 0; 
            tud_hid_report(REPORT_ID_TELEPHONY, &hook, 1);
            while (!tud_hid_ready()) {tud_task(); };
            printf("REPORT_ID_TELEPHONY: down led_oncall_state=%u hook=%u\n",led_oncall_state,hook);
        }
        
        uint8_t mute = led_oncall_state << 1 | 0x01;
        tud_hid_report(REPORT_ID_TELEPHONY, &mute, 1);
        printf("REPORT_ID_TELEPHONY: down mute=%u\n",mute);
        pressed_ms = current_ms;
    }

    else if ( !(button_state & MUTE_BUTTON_PIN) && (prev_button_state & MUTE_BUTTON_PIN) ) {
        uint8_t mute;
        if (!led_mute_state && (current_ms-pressed_ms > 500)) {
            mute = led_oncall_state << 1 | 0x00;
            tud_hid_report(REPORT_ID_TELEPHONY, &mute, 1);
            while (!tud_hid_ready()) {tud_task(); };
            printf("REPORT_ID_TELEPHONY: AUTO mute=%u\n",mute);
            mute = led_oncall_state << 1 | 0x01;
            tud_hid_report(REPORT_ID_TELEPHONY, &mute, 1);
            while (!tud_hid_ready()) {tud_task(); };
            printf("REPORT_ID_TELEPHONY: AUTO mute=%u\n",mute);
        }
        mute = led_oncall_state << 1 | 0x00;
        tud_hid_report(REPORT_ID_TELEPHONY, &mute, 1);
        printf("REPORT_ID_TELEPHONY: up mute=%u\n",mute);
    }

    else if ( (button_state & HOOK_BUTTON_PIN) && !(prev_button_state & HOOK_BUTTON_PIN) ) {
        uint8_t hook = 0; 
        tud_hid_report(REPORT_ID_TELEPHONY, &hook, 1);
        printf("REPORT_ID_TELEPHONY: down led_oncall_state=%u hook=%u\n",led_oncall_state,hook);
    }

    else if ( (button_state & VOLD_BUTTON_PIN) && !(prev_button_state & VOLD_BUTTON_PIN) ) {
        uint16_t volume_down = HID_USAGE_CONSUMER_VOLUME_DECREMENT;
        tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &volume_down, 1);
    }

    else if ( (button_state & VOLU_BUTTON_PIN) && !(prev_button_state & VOLU_BUTTON_PIN) ) {
        uint16_t volume_up = HID_USAGE_CONSUMER_VOLUME_INCREMENT;
        tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &volume_up, 1);
    }

    else if ( !(button_state & (VOLU_BUTTON_PIN | VOLD_BUTTON_PIN) ) && (prev_button_state & (VOLU_BUTTON_PIN | VOLD_BUTTON_PIN) ) ) {
        uint16_t empty_key = 0;
        tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &empty_key, 1);
    }
    
    prev_button_state = button_state;

}


//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void) {

    static uint32_t start_ms = 0;
    static uint32_t next_blink_interval_ms = 0;
    // blink is disabled
    if (!blink_interval_ms) return;

    // Blink every interval ms
    if ( board_millis() - start_ms < next_blink_interval_ms) return; // not enough time
    
    start_ms += next_blink_interval_ms;

    static int fade = 0;
    static bool going_up = true;

    if(led_usb_state) {
        if (going_up) {
            fade+=16;
            if (fade > 255) {
                fade = 255;
                going_up = false;
            }
        } else {
            fade-=16;
            if (fade < 0) {
                fade = 0;
                going_up = true;
                led_usb_state = false;
            }
        }
        pwm_set_gpio_level(PWM_LED_PIN, fade * fade);
        next_blink_interval_ms = 10;
    } else {
        led_usb_state = true;
        next_blink_interval_ms = blink_interval_ms;
    }
  
}

//--------------------------------------------------------------------+
// Init GPIO output leds with LED_MASK
//--------------------------------------------------------------------+
void leds_init() {
    gpio_init_mask(LED_MASK);
    gpio_set_dir_out_masked(LED_MASK);
    gpio_put_masked(LED_MASK,0x00000000);
}    

//--------------------------------------------------------------------+
// Toggle leds defined in mask
//--------------------------------------------------------------------+
void led_toggle(uint32_t led_mask) {
    gpio_put_masked(led_mask,~gpio_get_all());
}

//--------------------------------------------------------------------+
// Blink leds defined in mask
//--------------------------------------------------------------------+
void led_blink(uint32_t led_mask) {
    led_toggle(led_mask);
    sleep_ms(100);
    led_toggle(led_mask);
}

void leds_update() {
    uint32_t leds=0x00000000;
 
    if(led_oncall_state) {
        pwm_set_gpio_level(PWM_LED_PIN, 0);
        sleep_ms(5);
        pwm_set_enabled(slice_num,false);

        if(led_mute_state){
            leds=LED_R_PIN;
        } else {
            leds=LED_G_PIN;
        }
    } else {
        pwm_set_enabled(slice_num,true);

    }
    
    gpio_put_masked(LED_MASK,leds);
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

