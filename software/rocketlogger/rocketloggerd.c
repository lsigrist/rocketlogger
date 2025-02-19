/**
 * Copyright (c) 2016-2020, ETH Zurich, Computer Engineering Group
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * * Neither the name of the copyright holder nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <signal.h>
#include <sys/reboot.h>
#include <unistd.h>

#include "gpio.h"
#include "log.h"
#include "rl.h"
#include "rl_lib.h"

/// Minimal time interval between two interrupts (in seconds)
#define RL_DAEMON_MIN_INTERVAL 1

/// Duration of calibration run (in seconds)
#define RL_CALIBRATION_DURATION_SEC 1

/// Delay on cape power up (in microseconds)
#define RL_POWERUP_DELAY_US 5000

/// Min duration of a long button press (in seconds)
#define RL_BUTTON_LONG_PRESS_SEC 3

/// Min duration of a very long button press (in seconds)
#define RL_BUTTON_VERY_LONG_PRESS_SEC 6

/// Min duration of an extra long button press (in seconds)
#define RL_BUTTON_EXTRA_LONG_PRESS_SEC 10

/**
 * Daemon exit system action definition
 */
enum system_action {
    SYSTEM_ACTION_NONE,     //!< perform no system action
    SYSTEM_ACTION_REBOOT,   //!< reboot the system
    SYSTEM_ACTION_POWEROFF, //!< power off the system
};

/**
 * Typedef for daemon exit system action
 */
typedef enum system_action system_action_t;


/// RocketLogger daemon log file.
static char const *const log_filename = RL_DAEMON_LOG_FILE;

/// Flag to terminate the infinite daemon loop
volatile bool daemon_shutdown = false;

/// System action to perform when exiting the daemon
volatile system_action_t system_action = SYSTEM_ACTION_NONE;

/// GPIO handle for power switch
gpio_t *gpio_power = NULL;

/// GPIO handle for user button
gpio_t *gpio_button = NULL;

/**
 * Perform RocketLogger ADC reference voltage calibration.
 *
 * Run a measurement without data output to file or web for the given duration.
 * Resets the sampling status to default after completion.
 *
 * @param duration The duration of the calibration run in seconds
 * @return Measurement run return value, 0 on success, negative on failure with
 * errno set accordingly
 */
int adc_calibrate(uint64_t duration) {
    if (duration == 0) {
        errno = EINVAL;
        return -1;
    }

    // configure run with minimal data output
    static rl_config_t rl_config_calibration;
    rl_config_reset(&rl_config_calibration);
    rl_config_calibration.background_enable = false;
    rl_config_calibration.interactive_enable = false;
    rl_config_calibration.web_enable = false;
    rl_config_calibration.file_enable = false;
    rl_config_calibration.sample_rate = RL_SAMPLE_RATE_MIN;
    rl_config_calibration.sample_limit = RL_SAMPLE_RATE_MIN * duration;

    // perform calibration run
    int res = rl_run(&rl_config_calibration);

    // reset sampling status counters
    rl_status_t rl_status;
    rl_status_read(&rl_status);
    rl_status.sample_count = 0;
    rl_status.buffer_count = 0;
    rl_status_write(&rl_status);

    return res;
}

/**
 * GPIO interrupt handler
 *
 * @param value GPIO value after interrupt
 */
void button_interrupt_handler(int value) {
    static time_t timestamp_down = -1;
    time_t timestamp = time(NULL);

    // capture timestamp on button press
    if (value == 0) {
        timestamp_down = timestamp;
    }
    // process button action on button release
    if (value == 1) {
        // get duration and reset timestamp down
        time_t duration = timestamp - timestamp_down;
        timestamp_down = -1;

        // skip further processing on invalid timestamp
        if (duration > timestamp) {
            return;
        }

        // get RocketLogger status
        rl_status_t status;
        int ret = rl_status_read(&status);
        if (ret < 0) {
            rl_log(RL_LOG_ERROR, "Failed reading status; %d message: %s", errno,
                   strerror(errno));
        }

        if (duration >= RL_BUTTON_EXTRA_LONG_PRESS_SEC) {
            rl_log(RL_LOG_INFO,
                   "Registered extra long press, requesting system shutdown.");
            daemon_shutdown = true;
            system_action = SYSTEM_ACTION_POWEROFF;
            if (!status.sampling) {
                return;
            }
        } else if (duration >= RL_BUTTON_VERY_LONG_PRESS_SEC) {
            rl_log(RL_LOG_INFO,
                   "Registered very long press, requesting system reboot.");
            daemon_shutdown = true;
            system_action = SYSTEM_ACTION_REBOOT;
            if (!status.sampling) {
                return;
            }
        } else if (duration >= RL_BUTTON_LONG_PRESS_SEC) {
            rl_log(RL_LOG_INFO,
                   "Registered long press, requesting daemon restart.");
            daemon_shutdown = true;
            system_action = SYSTEM_ACTION_NONE;
            if (!status.sampling) {
                return;
            }
        }

        char *cmd[3] = {"rocketlogger", NULL, NULL};
        if (status.sampling) {
            cmd[1] = "stop";
        } else {
            cmd[1] = "start";
        }

        // create child process to start RocketLogger
        pid_t pid = fork();
        if (pid < 0) {
            rl_log(RL_LOG_ERROR, "Failed forking process; %d message: %s",
                   errno, strerror(errno));
        }
        if (pid == 0) {
            // in child process, execute RocketLogger
            execvp(cmd[0], cmd);
            rl_log(RL_LOG_ERROR,
                   "Failed executing `rocketlogger %s`; %d message: %s", cmd,
                   errno, strerror(errno));
        } else {
            // in parent process log pid
            rl_log(RL_LOG_INFO, "Started RocketLogger with pid=%d.", pid);
        }
    }
    // interrupt rate control
    sleep(RL_DAEMON_MIN_INTERVAL);
}

/**
 * Signal handler to catch interrupt signals.
 *
 * @param signal_number The number of the signal to handle
 */
static void signal_handler(int signal_number) {
    // signal to terminate the daemon (systemd stop)
    if (signal_number == SIGTERM) {
        signal(signal_number, SIG_IGN);
        rl_log(RL_LOG_INFO, "Received SIGTERM, shutting down daemon.");
        daemon_shutdown = true;
    }
}

/**
 * RocketLogger deamon program. Continuously waits on interrupt on button GPIO
 * and starts/stops RocketLogger
 *
 * Arguments: none
 * @return standard Linux return codes
 */
int main(void) {
    int ret = SUCCESS;

    // init log module
    rl_log_init(log_filename, RL_LOG_VERBOSE);

    // set effective user ID of the process
    ret = setuid(0);
    if (ret < 0) {
        rl_log(RL_LOG_ERROR, "Failed setting effective user ID; %d message: %s",
               errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    rl_log(RL_LOG_VERBOSE, "running with real user ID: %d", getuid());
    rl_log(RL_LOG_VERBOSE, "running with effective user ID: %d", geteuid());
    rl_log(RL_LOG_VERBOSE, "running with real group ID: %d", getgid());
    rl_log(RL_LOG_VERBOSE, "running with effective group ID: %d", getegid());

    // initialize GPIO module
    gpio_init();

    // hardware initialization
    gpio_t *gpio_power = gpio_setup(GPIO_POWER, GPIO_MODE_OUT, "rocketloggerd");
    if (gpio_power == NULL) {
        rl_log(RL_LOG_ERROR, "Failed configuring power switch; %d message: %s",
               errno, strerror(errno));
        exit(EXIT_FAILURE);
    }
    ret = gpio_set_value(gpio_power, 1);
    if (ret < 0) {
        rl_log(RL_LOG_ERROR, "Failed powering up cape; %d message: %s", errno,
               strerror(errno));
        exit(EXIT_FAILURE);
    }

    // wait for converter soft start (> 1 ms)
    usleep(RL_POWERUP_DELAY_US);

    gpio_t *gpio_button =
        gpio_setup_interrupt(GPIO_BUTTON, GPIO_INTERRUPT_BOTH, "rocketloggerd");
    if (gpio_button == NULL) {
        rl_log(RL_LOG_ERROR, "Failed configuring button; %d message: %s", errno,
               strerror(errno));
        exit(EXIT_FAILURE);
    }

    // create shared memory for state
    ret = rl_status_shm_init();
    if (ret < 0) {
        rl_log(RL_LOG_ERROR, "Failed initializing status shared memory.");
        exit(EXIT_FAILURE);
    }

    // register signal handler for SIGTERM (for stopping daemon)
    struct sigaction signal_action;
    signal_action.sa_handler = signal_handler;
    sigemptyset(&signal_action.sa_mask);
    signal_action.sa_flags = 0;

    ret = sigaction(SIGTERM, &signal_action, NULL);
    if (ret < 0) {
        rl_log(RL_LOG_ERROR,
               "can't register signal handler for SIGTERM; %d message: %s",
               errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    // calibrate ADC reference voltage
    rl_log(RL_LOG_INFO, "Performing ADC reference calibration.");
    adc_calibrate(RL_CALIBRATION_DURATION_SEC);

    // check error status
    rl_status_t status;
    rl_status_read(&status);
    if (status.error) {
        rl_log(RL_LOG_ERROR, "ADC reference calibration failed, terminating.");
    } else {
        // daemon main loop
        rl_log(RL_LOG_INFO, "RocketLogger daemon running.");

        daemon_shutdown = false;
        while (!daemon_shutdown) {
            // wait for interrupt with infinite timeout
            int value = gpio_wait_interrupt(gpio_button, NULL);
            button_interrupt_handler(value);
        }

        rl_log(RL_LOG_INFO, "RocketLogger daemon stopped.");
    }

    rl_status_read(&status);

    // remove shared memory for state
    rl_status_shm_deinit();

    // deinitialize and shutdown hardware
    gpio_set_value(gpio_power, 0);
    gpio_release(gpio_power);
    gpio_release(gpio_button);
    gpio_deinit();

    // perform requested system action
    if (system_action == SYSTEM_ACTION_REBOOT) {
        rl_log(RL_LOG_INFO, "Rebooting system.");
        sync();
        reboot(RB_AUTOBOOT);
    } else if (system_action == SYSTEM_ACTION_POWEROFF) {
        rl_log(RL_LOG_INFO, "Powering off system.");
        sync();
        reboot(RB_POWER_OFF);
    }

    if (status.error) {
        exit(EXIT_FAILURE);
    }
    exit(EXIT_SUCCESS);
}
