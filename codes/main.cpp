#include "mbed.h"
#include "rtos.h" 
#include "ADXL345.h"
#include "TMP102.h"
#include <cmath>
#include <cstring>
using namespace std;

// --- I2C and SPI configuration ---
#define I2CSDA D4
#define I2CSCL D5
#define TMP102_TEMP_REG 0x00 
#define TMP102_CONFIG_REG 0x01 

#define SPI1_MOSI D11
#define SPI1_MISO D12
#define SPI1_SCLK D13
#define SPI1_CS A3

// --- Control pin definitions ---
#define BUZZER_PIN D2
#define LOCKED_LED_PIN A1 
#define UNLOCKED_LED_PIN A2 
#define ALERT_LED_PIN D0   
#define SWITCH_ONE_PIN D10 
#define SWITCH_ZERO_PIN A0  
#define PASSWORD_CORRECT_LED_PIN D3      
#define PASSWORD_INCORRECT_LED_PIN D6   
#define WAKE_INTERRUPT_PIN D9 

#define DEBOUNCE_TIME_MS 200

const int PASSWORD_LENGTH = 5;
const int PASSWORD_SEQUENCE[PASSWORD_LENGTH] = {1,0,1,1,0}; // The passcode

const int TMP102_ADDRESS = 0x90;
const float TEMPERATURE_THRESHOLD_HIGH = 30.0; 
const float TEMPERATURE_THRESHOLD_LOW = 10.0;  
const float ADXL345_TAMPER_THRESHOLD_G = 0.1; 

bool stable_timer_running = false; 
volatile bool wake_requested = false;

#define ADXL_SLEEP_THRESHOLD_XY 1.0f
#define ADXL_SLEEP_THRESHOLD_Z_LOW 0.8f
#define ADXL_SLEEP_THRESHOLD_Z_HIGH 1.2f
#define ENV_SLEEP_CHECK_DURATION 30s

// --- Hardware interface objects ---
I2C tmp102_i2c(I2CSDA, I2CSCL);
SPI adxl345_spi(SPI1_MOSI, SPI1_MISO, SPI1_SCLK);
DigitalOut adxl345_cs(SPI1_CS);
DigitalOut buzzer(BUZZER_PIN);
DigitalOut locked_led(LOCKED_LED_PIN);
DigitalOut unlocked_led(UNLOCKED_LED_PIN);
DigitalOut alert_led(ALERT_LED_PIN);
InterruptIn switch_one(SWITCH_ONE_PIN);
InterruptIn switch_zero(SWITCH_ZERO_PIN);
InterruptIn wake_interrupt(WAKE_INTERRUPT_PIN);
DigitalOut password_correct_led(PASSWORD_CORRECT_LED_PIN);
DigitalOut password_incorrect_led(PASSWORD_INCORRECT_LED_PIN);

// --- System state variables ---
volatile bool system_locked = true;
volatile int incorrect_password_attempts = 0;
volatile int current_password_input_index = 0;
volatile bool switch_one_pressed = false;
volatile bool switch_zero_pressed = false;
volatile bool sensors_sleeping = false;

// --- Events and timers ---
EventFlags password_event_flags;
#define PASSWORD_INPUT_READY 0x01
Timer debounce_timer_one;
Timer debounce_timer_zero;
Timer stable_condition_timer;

// --- Sensor data buffers ---
char adxl_buffer[6];
int16_t adxl_raw_data[3];
float accel_x, accel_y, accel_z;
char tmp102_temp_reg_data[2];
float current_temperature_c;

// --- Interrupt handlers: switch debouncing ---
void on_switch_one_press() {
    if (debounce_timer_one.elapsed_time().count() > DEBOUNCE_TIME_MS * 1000) {
        switch_one_pressed = true;
        password_event_flags.set(PASSWORD_INPUT_READY);
        debounce_timer_one.reset();
    }
}

void on_switch_zero_press() {
    if (debounce_timer_zero.elapsed_time().count() > DEBOUNCE_TIME_MS * 1000) {
        switch_zero_pressed = true;
        password_event_flags.set(PASSWORD_INPUT_READY);
        debounce_timer_zero.reset();
    }
}

// --- Control LEDs and buzzer ---
void set_system_main_feedback(bool locked, bool alert) {
    locked_led = locked;
    unlocked_led = !locked;
    alert_led = alert;
    if (alert) buzzer = 1;
    else if (!password_incorrect_led) buzzer = 0;
}

// --- Tamper detection ---
void handle_tamper_detection() {
    if (fabs(accel_y) > ADXL345_TAMPER_THRESHOLD_G) {
        printf("Vibrations detected!\n");
        set_system_main_feedback(system_locked, true);
        ThisThread::sleep_for(1s);
        set_system_main_feedback(system_locked, false);
    }
}

// --- Environmental temperature anomaly detection ---
void handle_environmental_monitoring() {
    if (current_temperature_c > TEMPERATURE_THRESHOLD_HIGH || current_temperature_c < TEMPERATURE_THRESHOLD_LOW) {
        printf("Abnormal temperature: %.2f\n", current_temperature_c);
        set_system_main_feedback(system_locked, true);
        ThisThread::sleep_for(1s);
        set_system_main_feedback(system_locked, false);
    }
}

// --- Wake up ---
void wake_from_interrupt() {
    wake_requested = true;  
}
// --- Sleep judge ---
bool conditions_within_sleep_range() {
    return (current_temperature_c >= TEMPERATURE_THRESHOLD_LOW &&
            current_temperature_c <= TEMPERATURE_THRESHOLD_HIGH &&
            fabs(accel_x) <= ADXL_SLEEP_THRESHOLD_XY &&
            fabs(accel_y) <= ADXL_SLEEP_THRESHOLD_XY &&
            accel_z >= ADXL_SLEEP_THRESHOLD_Z_LOW && accel_z <= ADXL_SLEEP_THRESHOLD_Z_HIGH);
}

void set_adxl345_sleep_mode(bool sleep) {
    adxl345_cs = 0;
    adxl345_spi.write(0x2D);
    adxl345_spi.write(sleep ? 0x04 : 0x08);
    adxl345_cs = 1;
}

void set_tmp102_sleep_mode(bool sleep) {
    char config_data[3] = {TMP102_CONFIG_REG, (char)(sleep ? 0xE0 : 0x60), 0xA0};
    tmp102_i2c.write(TMP102_ADDRESS, (char*)config_data, 3);
}


// password input
void password_input_thread() {
    while (true) {
        // Wait for a button press (either switch_one or switch_zero)
        password_event_flags.wait_any(PASSWORD_INPUT_READY);

        // Clear button flags immediately after the event is received
        // This is crucial for single-shot processing of a button press.
        bool current_switch_one_state = switch_one_pressed;
        bool current_switch_zero_state = switch_zero_pressed;
        switch_one_pressed = false;
        switch_zero_pressed = false;

        if (system_locked) { // Only process password input if the system is locked
            int input_digit = -1;
            if (current_switch_one_state) {
                input_digit = 1;
            } else if (current_switch_zero_state) {
                input_digit = 0;
            }

            if (input_digit != -1) {
                //printf("Password input: %d\n\r", input_digit);
                if (input_digit == PASSWORD_SEQUENCE[current_password_input_index]) {
                    current_password_input_index++;
                    if (current_password_input_index == PASSWORD_LENGTH) {
                        // Correct password entered!
                        printf("\033[2J\033[1;1H");
                        printf("Present state of input password: 10110\nPassword CORRECT! Locker UNLOCKED.\n\r");
                        system_locked = false; // Unlock the locker
                        incorrect_password_attempts = 0; // Reset incorrect attempts
                        current_password_input_index = 0; // Reset for next time
                        password_correct_led = 1; // Green LED on
                        password_incorrect_led = 0; // Red LED off
                        buzzer = 0; // Ensure buzzer is off, main system feedback might turn it on later
                        set_system_main_feedback(system_locked, false); // Update main system LEDs
                        ThisThread::sleep_for(2s); // Keep green LED on for 2 seconds
                        password_correct_led = 0; // Turn off green LED

                    } else {
                        //printf("Correct digit. Progress: %d/%d\n\r", current_password_input_index, PASSWORD_LENGTH);

                        printf("\033[2J\033[1;1H");
                        printf("Present state of input password: ");
                        switch (current_password_input_index){
                            case 1: printf("1****\n");    break;
                            case 2: printf("10***\n");    break;
                            case 3: printf("101**\n");    break;
                            case 4: printf("1011*\n");    break;
                            default: printf("something wrong\n");
                        }

                    }
                } else {
                    // Incorrect digit
                    printf("Password INCORRECT digit at position %d! Resetting.\n\r", current_password_input_index + 1);
                    incorrect_password_attempts++;
                    current_password_input_index = 0; // Reset password input sequence

                    password_incorrect_led = 1; // Red LED on
                    buzzer = 1; // Buzzer on for incorrect password
                    password_correct_led = 0; // Green LED off
                    
                    printf("Incorrect attempts: %d\n\r", incorrect_password_attempts);
                    if (incorrect_password_attempts >= 3) { // Example: 3 incorrect attempts for lockout
                        printf("Too many incorrect password attempts! System temporarily locked out for password input.\n\r");
                        // Implement a longer lockout period if desired, e.g., ThisThread::sleep_for(10s);
                        // For now, we just reset attempts.
                        incorrect_password_attempts = 0; // Reset attempts after lockout
                    }
                    ThisThread::sleep_for(1s); // Keep red LED and buzzer on for 1 second
                    password_incorrect_led = 0; // Turn off red LED
                    buzzer = 0; // Turn off buzzer
                }
            }
        } else { // System is UNLOCKED
            if (current_switch_one_state) { // Assuming Switch '1' can also re-lock if system is unlocked
                printf("Switch '1' pressed while unlocked. Relocking system...\n\r");
                system_locked = true;
                set_system_main_feedback(system_locked, false);
            }
            // Clear any flags if not used for re-locking, this part is already covered by clearing flags above.
        }
    }
}

// --- Enhanced Sensor Monitoring Thread ---

void enhanced_sensor_monitoring_thread() {
    wake_interrupt.fall(&wake_from_interrupt);
    wake_interrupt.mode(PullUp);
    
    while (true) {
        if (!sensors_sleeping) {
            printf("\033[2J\033[1;1H");
            read_adxl345_data();
            read_tmp102_data();

            if (conditions_within_sleep_range()) {
                if (!stable_timer_running) {
                    stable_condition_timer.start();
                    stable_timer_running = true;
                } else if (stable_condition_timer.elapsed_time() >= ENV_SLEEP_CHECK_DURATION) {
                    printf("\033[2J\033[1;1H");
                    printf("Stable 10s. Entering sleep.\n");
                    set_adxl345_sleep_mode(true);
                    set_tmp102_sleep_mode(true);
                    sensors_sleeping = true;
                    stable_condition_timer.stop();
                    stable_timer_running = false;
                }
            } else {
                stable_condition_timer.reset();
                stable_timer_running = false;
            }

            handle_tamper_detection();
            handle_environmental_monitoring();

        } else {
            if (wake_requested) {
                printf("Sensors have been woken up, resuming monitoring.\n");
                set_adxl345_sleep_mode(false);
                set_tmp102_sleep_mode(false);
                sensors_sleeping = false;
                stable_condition_timer.reset();
                stable_timer_running = false;
                wake_requested = false;
            }
        }

        ThisThread::sleep_for(500ms);
    }
}

int main() {
    initialize_adxl345_spi();
    configure_tmp102();

    debounce_timer_one.start();
    debounce_timer_zero.start();

    switch_one.fall(&on_switch_one_press);
    switch_one.mode(PullUp);
    switch_zero.fall(&on_switch_zero_press);
    switch_zero.mode(PullUp);

    set_system_main_feedback(system_locked, false);
    password_correct_led = 0;
    password_incorrect_led = 0;
    buzzer = 0;

    printf("System Init. State: %s\n", system_locked ? "LOCKED" : "UNLOCKED");

    Thread password_thread;
    password_thread.start(callback(password_input_thread));

    Thread sensor_thread;
    sensor_thread.start(callback(enhanced_sensor_monitoring_thread));

    while (true) {
        ThisThread::sleep_for(1s);
    }
}
