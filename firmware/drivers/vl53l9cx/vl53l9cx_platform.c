/*
 * Platform layer for ST's VL53L9CX Ultra Lite Driver, on Zephyr I2C.
 *
 * This is the whole port. ST's driver is platform-independent C that reaches
 * hardware only through the functions below; implement them and the ULD runs
 * unmodified. Keeping ST's source byte-identical to their release means their
 * updates drop in cleanly and the boundary between "our bug" and "their bug"
 * stays sharp — which matters a great deal when a bring-up goes quiet.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * ---------------------------------------------------------------------------
 * BEFORE THIS COMPILES
 *
 * Download X-CUBE-53L9A1 from ST and place it in vendor/ (gitignored — the
 * package is licensed and is not vendored into this repository). Then VERIFY
 * the function names and signatures below against their platform header: they
 * follow the VL53L5CX/VL53L8CX convention, which is stable across the family,
 * but have not been confirmed for the L9.
 * ---------------------------------------------------------------------------
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include "vl53l9cx_platform.h"

LOG_MODULE_DECLARE(vl53l9cx, CONFIG_VL53L9CX_LOG_LEVEL);

/*
 * TRAP 1 — register indices are 16 bits, not 8.
 *
 * Zephyr's i2c_reg_read_byte_dt() and friends assume an 8-bit register
 * address and WILL NOT WORK with this part. Every access has to write a
 * two-byte big-endian index first. This is the most common way this port
 * fails, and it presents as the device acknowledging its address but
 * returning nonsense.
 */
#define IDX_LEN 2

static inline void idx_to_buf(uint16_t idx, uint8_t buf[IDX_LEN])
{
	buf[0] = (uint8_t)(idx >> 8);
	buf[1] = (uint8_t)(idx & 0xFF);
}

uint8_t VL53L9CX_RdByte(VL53L9CX_Platform *p, uint16_t idx, uint8_t *value)
{
	return VL53L9CX_RdMulti(p, idx, value, 1);
}

uint8_t VL53L9CX_WrByte(VL53L9CX_Platform *p, uint16_t idx, uint8_t value)
{
	return VL53L9CX_WrMulti(p, idx, &value, 1);
}

uint8_t VL53L9CX_RdMulti(VL53L9CX_Platform *p, uint16_t idx,
			 uint8_t *buf, uint32_t count)
{
	uint8_t idx_buf[IDX_LEN];

	idx_to_buf(idx, idx_buf);

	/* Write index then read, as one transaction with a repeated start.
	 * Splitting it into two transactions releases the bus in between and
	 * loses the index on a multi-master bus.
	 */
	if (i2c_write_read_dt(p->i2c, idx_buf, IDX_LEN, buf, count) < 0) {
		LOG_ERR("read failed: idx 0x%04x, %u bytes", idx, count);
		return VL53L9CX_STATUS_ERROR;
	}
	return VL53L9CX_STATUS_OK;
}

/*
 * TRAP 2 — WrMulti is called with the firmware blob, tens of kilobytes in one
 * logical write.
 *
 * A single i2c_write_dt() of that size needs a contiguous buffer holding index
 * + payload, which we are not going to allocate. Chunk it instead, carrying the
 * index forward, and use a two-message transfer so no copy is needed at all.
 *
 * Chunk size is a devicetree property because it is a real tuning knob: it
 * trades transaction overhead against peak buffer use, and the blob upload
 * happens on every cold start, so its cost feeds directly into the power model.
 */
uint8_t VL53L9CX_WrMulti(VL53L9CX_Platform *p, uint16_t idx,
			 uint8_t *buf, uint32_t count)
{
	uint32_t sent = 0;

	while (sent < count) {
		uint32_t chunk = MIN(count - sent, (uint32_t)p->chunk_size);
		uint8_t idx_buf[IDX_LEN];
		struct i2c_msg msg[2];

		idx_to_buf((uint16_t)(idx + sent), idx_buf);

		msg[0].buf = idx_buf;
		msg[0].len = IDX_LEN;
		msg[0].flags = I2C_MSG_WRITE;

		msg[1].buf = buf + sent;
		msg[1].len = chunk;
		msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		if (i2c_transfer_dt(p->i2c, msg, 2) < 0) {
			LOG_ERR("write failed: idx 0x%04x, offset %u",
				idx, sent);
			return VL53L9CX_STATUS_ERROR;
		}
		sent += chunk;
	}
	return VL53L9CX_STATUS_OK;
}

/*
 * TRAP 3 — WaitMs is called frequently during blob upload.
 *
 * k_sleep() yields the CPU, which is what we want: it lets the system idle
 * instead of spinning, and over a multi-second upload that is real energy.
 * But k_sleep() cannot resolve below one tick, so a sub-tick delay silently
 * becomes a full tick — which at CONFIG_SYS_CLOCK_TICKS_PER_SEC=100 turns a
 * requested 1 ms into 10 ms and stretches the upload tenfold.
 *
 * So: busy-wait below one tick, sleep above it.
 */
uint8_t VL53L9CX_WaitMs(VL53L9CX_Platform *p, uint32_t ms)
{
	ARG_UNUSED(p);

	if (ms == 0) {
		return VL53L9CX_STATUS_OK;
	}

	if (k_ms_to_ticks_ceil32(ms) <= 1) {
		k_busy_wait(ms * USEC_PER_MSEC);
	} else {
		k_sleep(K_MSEC(ms));
	}
	return VL53L9CX_STATUS_OK;
}

/*
 * Endianness swap over a 32-bit-word buffer. Pure C, no Zephyr contact — kept
 * here only because ST's driver expects it from the platform layer.
 */
uint8_t VL53L9CX_SwapBuffer(uint8_t *buf, uint16_t size)
{
	for (uint16_t i = 0; i < size; i += 4) {
		uint32_t w;

		memcpy(&w, &buf[i], sizeof(w));
		w = __builtin_bswap32(w);
		memcpy(&buf[i], &w, sizeof(w));
	}
	return VL53L9CX_STATUS_OK;
}
