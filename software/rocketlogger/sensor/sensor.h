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

#ifndef SENSOR_SENSOR_H_
#define SENSOR_SENSOR_H_

#include <stdbool.h>
#include <stdint.h>

#include "../rl_file.h"

#define MAX_MESSAGE_LENGTH 1000

#ifndef I2C_BUS_FILENAME
#define I2C_BUS_FILENAME "/dev/i2c-2"
#endif

/// Number of sensor registered
#define SENSOR_REGISTRY_SIZE 5

#define SENSOR_NAME_LENGTH (RL_FILE_CHANNEL_NAME_LENGTH)

/**
 * Standardized RL sensor interface definition
 */
struct rl_sensor {
    char name[SENSOR_NAME_LENGTH];
    int identifier;
    int channel;
    rl_unit_t unit;
    int32_t scale;
    int (*init)(int);
    void (*deinit)(int);
    int (*read)(int);
    int32_t (*get_value)(int, int);
};

/**
 * Typedef for standardized RocketLogger sensor interface definition
 */
typedef struct rl_sensor rl_sensor_t;

/**
 * The sensor registry structure.
 *
 * Register your sensor (channels) here. Multiple channels from the same
 * sensor should be added as consecutive entries.
 */
extern const rl_sensor_t SENSOR_REGISTRY[SENSOR_REGISTRY_SIZE];

/**
 * Initialize the shared I2C sensor bus.
 *
 * @return Returns 0 on success, negative on failure with errno set accordingly
 */
int sensors_init(void);

/**
 * Deinitialize the shared I2C sensor bus.
 */
void sensors_deinit(void);

/**
 * Open a new handle of the I2C bus.
 *
 * @return Returns 0 on success, negative on failure with errno set accordingly
 */
int sensors_open_bus(void);

/**
 * Close a I2C sensor bus.
 *
 * @param bus The I2C bus to close
 * @return Returns 0 on success, negative on failure with errno set accordingly
 */
int sensors_close_bus(int bus);

/**
 * Get the shared I2C bus handle.
 *
 * @return The I2C bus handle, or negative value if bus unavailable
 */
int sensors_get_bus(void);

/**
 * Initiate an I2C communication with a device.
 *
 * @param device_address The I2C address of the device
 * @return Returns 0 on success, negative on failure with errno set accordingly
 */
int sensors_init_comm(uint8_t device_address);

/**
 * Scan the I2C sensor for sensor in the registry and initialize them.
 *
 * @param sensor_available List of sensors of the registry available
 * @return Number of sensors from the registry found on the bus
 */
int sensors_scan(bool sensor_available[SENSOR_REGISTRY_SIZE]);

/**
 * Read available sensors on the I2C bus.
 *
 * @param sensor_data Data array to store the sensor values to
 * @param sensor_available List of available (previously initialized) sensors
 * @return Number of sensors read on success, negative on failure with errno set
 * accordingly
 */
int sensors_read(int32_t *const sensor_data,
                 bool const sensor_available[SENSOR_REGISTRY_SIZE]);

/**
 * Close all sensors used on the I2C bus.
 *
 * @param sensor_available List of available (previously initialized) sensors
 */
void sensors_close(bool const sensor_available[SENSOR_REGISTRY_SIZE]);

#endif /* SENSOR_SENSOR_H_ */
