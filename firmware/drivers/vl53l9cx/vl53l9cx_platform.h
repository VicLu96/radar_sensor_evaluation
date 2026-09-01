/*
 * Platform binding for ST's VL53L9CX ULD.
 *
 * ST's driver includes a header of this name and expects the VL53L9CX_Platform
 * type and the function prototypes below. This file provides them for Zephyr.
 *
 * VERIFY against X-CUBE-53L9A1 once downloaded: ST may name the struct or its
 * members differently for the L9, and may add hooks (a GetTickCount, or a
 * second address for I3C dynamic assignment). The shape below follows the
 * VL53L5CX / VL53L8CX convention.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VL53L9CX_PLATFORM_H_
#define VL53L9CX_PLATFORM_H_

#include <zephyr/drivers/i2c.h>
#include <stdint.h>

#define VL53L9CX_STATUS_OK    0U
#define VL53L9CX_STATUS_ERROR 255U

/*
 * Passed by ST's driver into every platform call. Holds everything the
 * platform layer needs and nothing it does not — notably no driver state,
 * so the platform layer stays trivially testable.
 */
typedef struct {
	const struct i2c_dt_spec *i2c;
	uint16_t chunk_size; /* blob upload chunk, from devicetree */
} VL53L9CX_Platform;

uint8_t VL53L9CX_RdByte(VL53L9CX_Platform *p, uint16_t idx, uint8_t *value);
uint8_t VL53L9CX_WrByte(VL53L9CX_Platform *p, uint16_t idx, uint8_t value);
uint8_t VL53L9CX_RdMulti(VL53L9CX_Platform *p, uint16_t idx,
			 uint8_t *buf, uint32_t count);
uint8_t VL53L9CX_WrMulti(VL53L9CX_Platform *p, uint16_t idx,
			 uint8_t *buf, uint32_t count);
uint8_t VL53L9CX_WaitMs(VL53L9CX_Platform *p, uint32_t ms);
uint8_t VL53L9CX_SwapBuffer(uint8_t *buf, uint16_t size);

#endif /* VL53L9CX_PLATFORM_H_ */
