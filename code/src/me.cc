#include <me.h>
#include <pico/unique_id.h>

const char manufacturer[] = "Shh";
const char product[] = "Mute button";
const char version[] = "0.1";
char serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1] = "";

// Assign the SN using the unique flash id
void me_init() {
    pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
}
