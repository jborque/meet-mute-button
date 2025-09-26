#include <bsp/board.h>
#include <tusb.h>

#include <hardware/gpio.h>
#include <hardware/pwm.h>
#include <pico/bootrom.h>
#include <pico/unique_id.h>
#include <pico/stdio.h>

#include "encoder.h"
#include "button.h"
#include "ws2812.h"
#include "our_descriptor.h"
#include "me.h"

// --- Constants for Readability ---
namespace constants {
    // Timings
    constexpr uint32_t BUTTON_POLL_INTERVAL_MS = 10;
    constexpr uint32_t USB_INIT_DELAY_MS = 10;
    constexpr uint32_t BLINK_DELAY_MS = 200;
    constexpr uint32_t BLINK_ON_INTERVAL_MS = 1000;
    constexpr uint32_t BLINK_NOT_MOUNTED_MS = 100;
    constexpr uint32_t BLINK_MOUNTED_MS = 5000;
    constexpr uint32_t BLINK_SUSPENDED_MS = 20000;
    constexpr uint32_t BLINK_STEP_MS = 60;
    constexpr uint32_t LONG_PRESS_DURATION_MS = 500;
    
    // LED Colors (GRB format)
    constexpr uint32_t LED_COLOR_RED = 0x000f00;
    constexpr uint32_t LED_COLOR_YELLOW = 0x0f0f00;
    constexpr uint32_t LED_COLOR_GREEN = 0x0f0000;
    constexpr uint32_t LED_COLOR_BLUE = 0x00000f;
    constexpr uint32_t LED_COLOR_PURPLE = 0x000f0f;
    constexpr uint32_t LED_COLOR_OFF = 0x000000;
    constexpr uint32_t LED_COLOR_STARTUP_BLINK = 0x0f0f0f;
    
    // Rotary Encoder
    constexpr uint32_t ENCODER_CLK_PIN = 7;
    constexpr uint32_t ENCODER_DT_PIN = 8;
    constexpr uint32_t ENCODER_SW_PIN = 9;
    constexpr long int ENCODER_THRESHOLD = 3;

    // Neopixel
    constexpr bool IS_RGBW = false;
    constexpr uint32_t WS2812_PIN = 2;
    constexpr uint32_t NUM_PIXELS = 1;

    // Button Pins
    constexpr uint32_t MUTE_BUTTON_PIN = 19;
    constexpr uint32_t HOOK_BUTTON_PIN = 21;
    constexpr uint32_t VOLU_BUTTON_PIN = 18;
    constexpr uint32_t VOLD_BUTTON_PIN = 20;

}

// State Definitions
enum class DeviceState : uint8_t {
    USB_ON          = 1 << 0,
    USB_MOUNTED     = 1 << 1,
    USB_SUSPENDED   = 1 << 2,
    USB_READY       = 1 << 3,
    MUTE_ACTIVE     = 1 << 4,
    ON_CALL         = 1 << 5,
};

static uint8_t device_state_flags = 0x00;

// Queue definitions
#define Q_LENGTH 10

enum class Event {
    NOTHING,
    MUTE_DOWN,
    MUTE_UP,
    HOOK_DOWN,
    HOOK_UP,
    VOLU_DOWN,
    VOLD_DOWN,
    VOL_RELEASE
};

static Event queue[Q_LENGTH] = {Event::NOTHING};
static uint8_t queue_start=0;
static uint8_t queue_end=0;

/**
 * @brief Defines the possible states for the LED task state machine.
 */
enum class LedState {
    BREATHING,
    SOLID_GREEN,
    SOLID_RED,
};

bool state_get(DeviceState s);
void state_set(DeviceState s);
void state_unset(DeviceState s);

void led_init();
void led_set(uint32_t color);
void led_toggle(uint32_t color);
void led_blink(uint32_t color);
void led_task(void);

void input_init();
void input_onchange(rotary_encoder_t *encoder);
void input_onpress(button_t *button);

void hid_task(void);

/**
 * @brief Main program entry point.
 * Initializes hardware, USB stack, and enters the main processing loop.
 * 
 * @return int Should not return.
 */
int main() {

    board_init();
    me_init();
    stdio_init_all();
    led_init();
    input_init();
    tusb_init();


    sleep_ms(constants::USB_INIT_DELAY_MS);
    // Check if the mute button is held down on boot to enter bootloader mode.
    if (( ~gpio_get_all() ) & ( (1 << constants::MUTE_BUTTON_PIN) | (1 << constants::ENCODER_SW_PIN) )) {
        for(uint8_t i=0; i<3; i++) {
            led_blink(constants::LED_COLOR_PURPLE);
            sleep_ms(constants::BLINK_DELAY_MS);
        }
        reset_usb_boot(0, 0);
    }

    printf("Shhh - Mute button 0x01\nSerial: %s\n",serial_str);

    led_blink(constants::LED_COLOR_STARTUP_BLINK);
    sleep_ms(constants::BLINK_DELAY_MS);


    while (true) {
        tud_task();
        led_task();    
        hid_task();
    }

    return 0;

}

//--------------------------------------------------------------------+
// Event Queue stuff
//--------------------------------------------------------------------+
/**
 * @brief Pushes an event onto the circular event queue.
 * 
 * @param e The event to be added to the queue.
 */
void q_push(Event e) {
// Use a critical section to prevent race conditions from interrupts.
    uint32_t status = save_and_disable_interrupts();
    printf("Pushing: %d\n",static_cast<uint8_t>(Event::HOOK_DOWN));
    uint8_t next_queue_end=queue_end;
    if( ++next_queue_end >= Q_LENGTH ) next_queue_end = 0;    
    if( next_queue_end == queue_start ) {
        printf("Queue Full: Ignored\n");
        restore_interrupts(status);
        return;
    }
    queue[queue_end]=e;
    queue_end=next_queue_end;
    restore_interrupts(status);
}

/**
 * @brief Pops an event from the circular event queue. This is an atomic operation.
 * 
 * @return Event The event from the front of the queue, or Event::NOTHING if empty.
 */
Event q_pop(void) {
    if( queue_start == queue_end ) return Event::NOTHING;
    // Critical section to ensure atomicity of queue access
    uint32_t status = save_and_disable_interrupts();
    Event ret = queue[queue_start];
    queue[queue_start] = Event::NOTHING;
    if( ++queue_start >= Q_LENGTH ) queue_start = 0;
    restore_interrupts(status);
    return ret;
}

//--------------------------------------------------------------------+
// Device State stuff
//--------------------------------------------------------------------+
/**
 * @brief Sets a specific state flag in the global device state.
 * 
 * @param s The DeviceState flag to set.
 */
void state_set(DeviceState s) {
    device_state_flags |= static_cast<uint8_t>(s);
}

/**
 * @brief Unsets a specific state flag in the global device state.
 * _
 * @param s The DeviceState flag to unset.
 */
void state_unset(DeviceState s) {
    device_state_flags &= ~static_cast<uint8_t>(s);
}

/**
 * @brief Checks if a specific state flag is set in the global device state.
 * 
 * @param s The DeviceState flag to check.
 * @return true if the flag is set, false otherwise.
 */
bool state_get(DeviceState s) {
    return (static_cast<uint8_t>(s) & device_state_flags);
}

//--------------------------------------------------------------------+
// Input Buttons and Encoder Interrupt Callbacks and Init
//--------------------------------------------------------------------+
/**
 * @brief Function to call on a rotary encoder change event
 * @param encoder The rotary encoder structure
 */
void input_onchange(rotary_encoder_t *encoder) {

    printf("Position: %li\n", encoder->position);
    printf("State: %d%d\n", encoder->state & 0b10 ? 1 : 0, encoder->state & 0b01);
    if(encoder->position > constants::ENCODER_THRESHOLD) {
        q_push(Event::VOLU_DOWN);
        printf("VOLU_DOWN\n");
    } else if (encoder->position < -constants::ENCODER_THRESHOLD) {
        q_push(Event::VOLD_DOWN);
        printf("VOLU_DOWN\n");
    } else return;
    q_push(Event::VOL_RELEASE);
    printf("VOLX_UP\n");
    encoder->position=0;


}

/**
 * @brief Function to call on a button press event
 * @param button The button structure
 */
void input_onpress(button_t *button) {
    Event e=Event::NOTHING;

    printf("Button pressed: %s\n", button->state ? "Released" : "Pressed");
    
    switch (button->pin) {
        case constants::HOOK_BUTTON_PIN:
            e=button->state ? Event::HOOK_UP : Event::HOOK_DOWN;
            break;
        case constants::VOLD_BUTTON_PIN:
            e=button->state ? Event::VOL_RELEASE : Event::VOLD_DOWN;
            break;
        case constants::VOLU_BUTTON_PIN:
            e=button->state ? Event::VOL_RELEASE : Event::VOLU_DOWN;
            break;
        case constants::ENCODER_SW_PIN:
        case constants::MUTE_BUTTON_PIN:
        {
            uint32_t current_ms = board_millis();
            static uint32_t last_pressed_ms = 0;
            if(!button->state) {
                if (current_ms - last_pressed_ms < constants::LONG_PRESS_DURATION_MS) {
                    q_push(Event::HOOK_DOWN);
                }
                e = Event::MUTE_DOWN;
            } else {
                if (!state_get(DeviceState::MUTE_ACTIVE) && (current_ms - last_pressed_ms > constants::LONG_PRESS_DURATION_MS)) {
                    q_push(Event::MUTE_UP);
                    q_push(Event::MUTE_DOWN);
                }
                e = Event::MUTE_UP;
            }
            last_pressed_ms = current_ms; // This line should be outside the if/else for MUTE_BUTTON_PIN
            break;
        }
        default:
            return; 
    }
    q_push(e);
}

/**
 * @brief Initializes all button GPIO pins defined in the BUTTON_MASK.
 */
void input_init() {

    create_button(constants::ENCODER_SW_PIN, input_onpress);
    create_button(constants::HOOK_BUTTON_PIN, input_onpress);
    create_button(constants::MUTE_BUTTON_PIN, input_onpress);
    create_button(constants::VOLU_BUTTON_PIN, input_onpress);
    create_button(constants::VOLD_BUTTON_PIN, input_onpress);
    create_encoder(constants::ENCODER_DT_PIN, constants::ENCODER_CLK_PIN, input_onchange);
    
}

//--------------------------------------------------------------------+
// HID Device stuff
//--------------------------------------------------------------------+
/**
 * @brief TinyUSB callback invoked when the device is mounted.
 */
void tud_mount_cb(void) {
    state_set(DeviceState::USB_MOUNTED);
}

/**
 * @brief TinyUSB callback invoked when the device is unmounted.
 */
void tud_umount_cb(void) {
    state_unset(DeviceState::USB_ON);
}

/**
 * @brief TinyUSB callback invoked when the USB bus is suspended.
 * 
 * @param remote_wakeup_en true if the host allows remote wakeup.
 */
void tud_suspend_cb(bool remote_wakeup_en) {
    (void) remote_wakeup_en;
    state_set(DeviceState::USB_SUSPENDED);
}

/**
 * @brief TinyUSB callback invoked when the USB bus is resumed.
 */
void tud_resume_cb(void) {
    state_unset(DeviceState::USB_SUSPENDED);
    if (tud_mounted()) {
        state_set(DeviceState::USB_MOUNTED);
    } else {
        state_unset(DeviceState::USB_MOUNTED);
    }
}

/**
 * @brief TinyUSB callback invoked when a SET_REPORT request is received from the host.
 * This is used by the host (e.g., meeting software) to update the device's state,
 * such as whether it is in a call or muted.
 */
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    printf("tud_hid_set_report_cb: itf=%u report_id=%u  report_type=%u bufsize=%u\n",itf,report_id,report_type,bufsize);    
    if (report_type == HID_REPORT_TYPE_OUTPUT && bufsize >= 1 && report_id == REPORT_ID_TELEPHONY ) {
        
        if(buffer[0] & 0x01) state_set(DeviceState::ON_CALL);
        else state_unset(DeviceState::ON_CALL);
        
        if( buffer[0] & 0x02 ) state_set(DeviceState::MUTE_ACTIVE);
        else state_unset(DeviceState::MUTE_ACTIVE);
        
        printf("tud_hid_set_report_cb: state=%u\n",device_state_flags);

        if (state_get(DeviceState::ON_CALL)) {
            q_push(Event::HOOK_UP);
            printf("tud_hid_set_report_cb: HOOK_UP");
        }

    }

}

/**
 * @brief TinyUSB callback invoked when a GET_REPORT request is received from the host.
 * The application must fill the buffer with the report data and return its length.
 */
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    return 0;
}

/**
 * @brief Processes events from the queue and sends HID reports to the host.
 * It handles telephony reports (mute, hook) and consumer control reports (volume).
 */
void hid_task() {
    static uint8_t t_report=0x00;
    static uint8_t prev_t_report=t_report;

    static uint16_t c_report=0x00;
    static uint16_t prev_c_report=c_report;

    if (!tud_ready()) {
        state_unset(DeviceState::USB_READY);
        return;
    }

    state_set(DeviceState::USB_READY);

    if ( !tud_hid_ready() ) return; 
    
    switch (q_pop())
    {
    case Event::MUTE_DOWN:
        t_report |= 0x01;
        break;
    case Event::MUTE_UP:
        t_report &= ~0x01;
        break;        
    case Event::HOOK_DOWN:
        t_report |= 0x02;
        break;
    case Event::HOOK_UP:
        t_report &= ~0x02;
        break;  
    case Event::VOLD_DOWN:
        c_report = HID_USAGE_CONSUMER_VOLUME_DECREMENT;
        break;
    case Event::VOLU_DOWN:
        c_report = HID_USAGE_CONSUMER_VOLUME_INCREMENT;
        break;
    case Event::VOL_RELEASE:
        c_report = 0;
        break;
    case Event::NOTHING:
    default:
        return;
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

//--------------------------------------------------------------------+
// Neopixel LED stuff
//--------------------------------------------------------------------+
/**
 * @brief Manages the Neopixel LED to provide visual feedback.
 * It shows a "breathing" effect when idle and solid colors (red/green) for mute status during a call.
 */
void led_task(void) {
    static uint32_t start_ms = board_millis();
    static LedState led_state = LedState::BREATHING;
    static LedState prev_led_state = LedState::SOLID_RED; // Force initial update
    static uint32_t interval_ms = constants::BLINK_NOT_MOUNTED_MS;


    if (board_millis() - start_ms < interval_ms) return;
    start_ms = board_millis();

    // Determine the current state
    if(state_get(DeviceState::ON_CALL)) {
        if(state_get(DeviceState::MUTE_ACTIVE)) {
            led_state = LedState::SOLID_RED;
        } else {
            led_state = LedState::SOLID_GREEN;
        }
    } else {
        led_state = LedState::BREATHING;
    }

    if (led_state == prev_led_state && prev_led_state != LedState::BREATHING) return;
    prev_led_state = led_state;

    switch (led_state) {
        case LedState::BREATHING: {
            static int fade = 5;
            static bool going_up = true;

            if (going_up) {
                if (++fade > 10) {
                    fade = 10;
                    going_up = false;
                }
                interval_ms = constants::BLINK_STEP_MS;
            } else {
                if (--fade < 7) {
                    fade = 6;
                    going_up = true;
                    // Set the pause duration at the bottom of the breath
                    if (state_get(DeviceState::USB_MOUNTED)) {
                        interval_ms = constants::BLINK_MOUNTED_MS;
                    } else if (state_get(DeviceState::USB_SUSPENDED)) {
                        interval_ms = constants::BLINK_SUSPENDED_MS;
                    } else {
                        interval_ms = constants::BLINK_NOT_MOUNTED_MS;
                    }
                }
            }

            uint32_t c = (fade * fade * fade) / 216; // Non-linear brightness for better effect

            led_set(c << 16 | c << 8 | c);
            break;
        }

        case LedState::SOLID_GREEN: {
            led_set(constants::LED_COLOR_GREEN);
            interval_ms = constants::BLINK_STEP_MS;
            break;

        }

        case LedState::SOLID_RED: {
            led_set(constants::LED_COLOR_RED);
            interval_ms = constants::BLINK_STEP_MS;
            break;
        }
    }
}

/**
 * @brief Initializes the Neopixel hardware.
 */
void led_init() {
    neopixel_init(constants::WS2812_PIN, constants::IS_RGBW);
    led_set(constants::LED_COLOR_OFF);
}    

/**
 * @brief Sets the color of all Neopixels in the strip.
 * 
 * @param pixel_grb The color in GRB format (e.g., 0xGGRRBB).
 */
void led_set(uint32_t color) {
    for(uint8_t i=0; i<constants::NUM_PIXELS; i++) {
        put_pixel(color);
    }
}


/**
 * @brief Toggles the LED. (Currently a stub).
 * 
 * @param color The color to toggle to.
 */
void led_toggle(uint32_t color) {
   
}

/**
 * @brief Blinks the LED with a specific color for a short duration.
 * 
 * @param color The color to blink.
 */
void led_blink(uint32_t color) {
    led_set(color);
    sleep_ms(constants::BLINK_DELAY_MS);
    led_set(constants::LED_COLOR_OFF);
}
