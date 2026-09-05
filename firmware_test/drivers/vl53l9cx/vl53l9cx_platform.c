/*
 * Platform layer for ST's VL53L9 driver, on Zephyr I2C.
 *
 * This is the whole port. ST's driver is platform-independent C that reaches
 * hardware only through the thirteen functions below; implement them and their
 * driver runs unmodified. Keeping ST's source byte-identical to their release
 * means their updates drop in cleanly and the boundary between "our bug" and
 * "their bug" stays sharp — which matters a great deal when a bring-up goes
 * quiet and there is no reference board to compare against.
 *
 * Signatures verified against st/vl53l9_platform.h (X-CUBE-53L9A1 v1.0.0),
 * 2026-09-01. The reference implementation this was written beside is
 * st-reference/vl53l9/vl53l9_platform.c — ST's own port, for an STM32H5 over
 * the I3C peripheral in legacy I2C mode.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include "vl53l9cx_private.h"
#include "vl53l9.h"
#include "vl53l9_platform.h"

LOG_MODULE_DECLARE(vl53l9cx, CONFIG_VL53L9CX_LOG_LEVEL);

/*
 * TRAP 1 — the register index is 16 bits and BIG-endian, while the data is
 * LITTLE-endian.
 *
 * Two separate things, and mixing them up is the classic way this port fails.
 *
 * (a) Zephyr's i2c_reg_read_byte_dt() and friends assume an 8-bit register
 *     address and WILL NOT WORK here. Every access writes a two-byte
 *     big-endian index first.
 * (b) The payload is little-endian. ST's own read16 assembles
 *     `data_read[0] | (data_read[1] << 8)` (st-reference/.../vl53l9_platform.c:130).
 *
 * The failure mode for (a) is the device acknowledging its address and then
 * returning nonsense; for (b) it is plausible-looking distances that are byte-
 * swapped, which is worse because it looks like a sensor problem.
 */
#define IDX_LEN 2

static inline void idx_to_buf(uint16_t idx, uint8_t buf[IDX_LEN])
{
	sys_put_be16(idx, buf);
}

static inline const struct i2c_dt_spec *bus(void *const p_dev)
{
	const struct device *dev = (const struct device *)p_dev;
	const struct vl53l9cx_config *cfg = dev->config;

	return &cfg->i2c;
}

/*
 * TRAP 4 — NO REPEATED START. EVER.
 *
 * A read is START/WRITE-index/STOP, then START/READ/STOP: two complete,
 * separate transactions. The obvious i2c_write_read_dt() is WRONG here — it
 * emits a repeated start between the index and the data, which this part does
 * not support (datasheet "known limitations").
 *
 * The failure mode is what makes this worth four paragraphs: a repeated start
 * does not merely fail the transfer, it latches the device into NAK-EVERYTHING
 * until a clean STOP escapes it. So the first bad read poisons every
 * subsequent one, and the sensor presents as dead from that point on — which
 * on a board with no known-good reference is a day lost to suspecting the
 * hardware.
 *
 * ST does the same split: in the legacy-I2C branch of their platform layer,
 * both the index write and the data read use I2C_PRIVATE_WITHOUT_ARB_STOP
 * ("Stop between each I2C Private message") and are issued as two separate HAL
 * transactions (st-reference/vl53l9/vl53l9_platform.c:_i3c_read). Only their
 * DMA path uses I2C_PRIVATE_WITH_ARB_RESTART.
 *
 * Yes, this releases the bus between index and data. On a multi-master bus
 * another master could interleave and lose us the index. This part gives us no
 * choice, so if a second master is ever added, the fix is bus-level locking,
 * not a repeated start.
 */
static int rd(void *const p_dev, uint16_t address, uint8_t *values, uint32_t size)
{
	const struct i2c_dt_spec *i2c;
	uint8_t idx[IDX_LEN];

	if (p_dev == NULL || values == NULL || size == 0U) {
		return VL53L9_ERROR_PLATFORM;
	}

	i2c = bus(p_dev);
	idx_to_buf(address, idx);

	/* The Zephyr return code is worth printing, because it separates two
	 * failures that mean completely different things:
	 *
	 *   -EIO        the device did not acknowledge. The bus works; nothing
	 *               is answering at this address.
	 *   -ETIMEDOUT  the transfer never completed. That is a stuck bus, not
	 *               a silent device — SDA or SCL never reached the level
	 *               the controller was waiting for. Missing pull-ups and a
	 *               device holding SCL low both look like this.
	 *
	 * Without the code, both arrive as "no answer from the sensor" and the
	 * search goes to the wrong place.
	 */
	int ret;

	/* Transaction 1: write the index, then STOP. */
	ret = i2c_write_dt(i2c, idx, IDX_LEN);
	if (ret < 0) {
		LOG_ERR("read failed at index write: idx 0x%04x, errno %d (%s)",
			address, ret,
			ret == -ETIMEDOUT ? "TIMEOUT — bus stuck, suspect pull-ups"
					  : "no acknowledge");
		return VL53L9_ERROR_PLATFORM;
	}

	/* Transaction 2: fresh START, read, STOP. */
	ret = i2c_read_dt(i2c, values, size);
	if (ret < 0) {
		LOG_ERR("read failed at data phase: idx 0x%04x, %u bytes, "
			"errno %d (%s)", address, size, ret,
			ret == -ETIMEDOUT ? "TIMEOUT — bus stuck, suspect pull-ups"
					  : "no acknowledge");
		return VL53L9_ERROR_PLATFORM;
	}

	return VL53L9_ERROR_NONE;
}

int vl53l9_read(void *const p_dev, uint16_t address, uint8_t *p_values, uint32_t size)
{
	return rd(p_dev, address, p_values, size);
}

int vl53l9_read8(void *const p_dev, uint16_t address, uint8_t *p_value)
{
	return rd(p_dev, address, p_value, 1U);
}

int vl53l9_read16(void *const p_dev, uint16_t address, uint16_t *p_value)
{
	uint8_t buf[2];
	int ret = rd(p_dev, address, buf, sizeof(buf));

	if (ret == VL53L9_ERROR_NONE) {
		*p_value = sys_get_le16(buf);
	}
	return ret;
}

int vl53l9_read32(void *const p_dev, uint16_t address, uint32_t *p_value)
{
	uint8_t buf[4];
	int ret = rd(p_dev, address, buf, sizeof(buf));

	if (ret == VL53L9_ERROR_NONE) {
		*p_value = sys_get_le32(buf);
	}
	return ret;
}

/*
 * Async read — deliberately not implemented yet.
 *
 * ST pairs this with vl53l9_get_frame_async()/_async_ack() for a DMA-backed
 * split frame read. The synchronous vl53l9_get_frame() path is complete
 * without it, so the honest thing during bring-up is to fail loudly rather
 * than ship a synchronous stub pretending to be asynchronous: a caller that
 * thinks a transfer is in flight and reads the buffer early gets a torn frame,
 * which looks exactly like a sensor fault.
 *
 * Worth implementing later if reading a 14.8 KB frame needs to overlap with
 * compute. Zephyr's i2c_transfer_cb_dt() is the hook.
 */
int vl53l9_read_async(void *const p_dev, uint16_t address,
		      volatile uint8_t *p_values, uint32_t size)
{
	ARG_UNUSED(p_dev);
	ARG_UNUSED(address);
	ARG_UNUSED(p_values);
	ARG_UNUSED(size);

	LOG_ERR("vl53l9_read_async is not implemented — use the synchronous "
		"vl53l9_get_frame() path");
	return VL53L9_ERROR_PLATFORM;
}

/*
 * TRAP 2 — vl53l9_write() is called with the firmware blob: 9,865 bytes in one
 * logical write (st/vl53l9.c:179).
 *
 * A single i2c_write_dt() of that size needs a contiguous buffer holding
 * index + payload, which we are not going to allocate. Chunk it, carrying the
 * register index forward by the offset — ST does the same
 * (st-reference/.../vl53l9_platform.c:184) — and use a two-message transfer so
 * no copy is needed at all. ST memcpy's each chunk into a stack buffer; we do
 * not have to.
 *
 * Chunk size is a devicetree property because it is a real tuning knob: it
 * trades transaction overhead against nothing much on our side, and the blob
 * upload happens on every cold start, so its cost feeds straight into the
 * power model.
 */
int vl53l9_write(void *const p_dev, uint16_t address, uint8_t *p_values, uint32_t size)
{
	const struct device *dev = (const struct device *)p_dev;
	const struct vl53l9cx_config *cfg;
	uint32_t sent = 0U;

	if (p_dev == NULL || p_values == NULL || size == 0U) {
		return VL53L9_ERROR_PLATFORM;
	}
	cfg = dev->config;

	while (sent < size) {
		uint32_t chunk = MIN(size - sent, (uint32_t)cfg->blob_chunk_size);
		uint32_t addr32 = (uint32_t)address + sent;
		uint8_t idx[IDX_LEN];
		struct i2c_msg msg[2];

		if (addr32 > 0xFFFFU) {
			LOG_ERR("write would run past the 16-bit index space "
				"(idx 0x%04x + %u)", address, sent);
			return VL53L9_ERROR_PLATFORM;
		}

		idx_to_buf((uint16_t)addr32, idx);

		msg[0].buf = idx;
		msg[0].len = IDX_LEN;
		msg[0].flags = I2C_MSG_WRITE;

		msg[1].buf = p_values + sent;
		msg[1].len = chunk;
		msg[1].flags = I2C_MSG_WRITE | I2C_MSG_STOP;

		/* Two WRITE messages with no I2C_MSG_RESTART between them: one
		 * START, index bytes, payload, one STOP. A plain write, which
		 * is what the part wants — see TRAP 4 above.
		 */

		if (i2c_transfer_dt(&cfg->i2c, msg, 2) < 0) {
			LOG_ERR("write failed: idx 0x%04x, offset %u of %u",
				address, sent, size);
			return VL53L9_ERROR_PLATFORM;
		}
		sent += chunk;
	}
	return VL53L9_ERROR_NONE;
}

int vl53l9_write8(void *const p_dev, uint16_t address, uint8_t value)
{
	return vl53l9_write(p_dev, address, &value, 1U);
}

int vl53l9_write16(void *const p_dev, uint16_t address, uint16_t value)
{
	uint8_t buf[2];

	sys_put_le16(value, buf);
	return vl53l9_write(p_dev, address, buf, sizeof(buf));
}

int vl53l9_write32(void *const p_dev, uint16_t address, uint32_t value)
{
	uint8_t buf[4];

	sys_put_le32(value, buf);
	return vl53l9_write(p_dev, address, buf, sizeof(buf));
}

/*
 * TRAP 3 — wait_ms is called in tight loops.
 *
 * ST's _wait_for_state() and _write_cmd() poll with 1 ms waits, and the blob
 * upload path leans on them. k_sleep() yields the CPU, which is what we want
 * — spinning through a multi-second upload is real energy on a battery node.
 * But k_sleep() cannot resolve below one tick, so a sub-tick delay silently
 * becomes a full tick: at CONFIG_SYS_CLOCK_TICKS_PER_SEC=100 a requested 1 ms
 * becomes 10 ms and stretches every poll loop tenfold.
 *
 * So: busy-wait below one tick, sleep above it.
 */
int vl53l9_wait_ms(void *const p_dev, uint32_t delay_ms)
{
	ARG_UNUSED(p_dev);

	if (delay_ms == 0U) {
		return VL53L9_ERROR_NONE;
	}

	if (k_ms_to_ticks_ceil32(delay_ms) <= 1U) {
		k_busy_wait(delay_ms * USEC_PER_MSEC);
	} else {
		k_sleep(K_MSEC(delay_ms));
	}
	return VL53L9_ERROR_NONE;
}

/*
 * The three board-configuration getters.
 *
 * vl53l9_init() calls all three and writes each value into the device
 * (st/vl53l9.c:163-176). They are not advisory: a wrong VDDA or VDDIO
 * misconfigures the analogue front end rather than failing loudly, and the
 * external clock frequency is what the device's PLL is told to expect.
 *
 * All three come from devicetree, and the binding marks them required so a
 * board file cannot silently omit them.
 */
int vl53l9_get_config_vdda(void *const p_dev, vl53l9_vdda_t *voltage)
{
	const struct device *dev = (const struct device *)p_dev;
	const struct vl53l9cx_config *cfg = dev->config;

	*voltage = (vl53l9_vdda_t)cfg->vdda;
	return VL53L9_ERROR_NONE;
}

int vl53l9_get_config_vddio(void *const p_dev, vl53l9_vddio_t *voltage)
{
	const struct device *dev = (const struct device *)p_dev;
	const struct vl53l9cx_config *cfg = dev->config;

	*voltage = (vl53l9_vddio_t)cfg->vddio;
	return VL53L9_ERROR_NONE;
}

int vl53l9_get_config_ext_clock(void *const p_dev, uint32_t *ext_clock)
{
	const struct device *dev = (const struct device *)p_dev;
	const struct vl53l9cx_config *cfg = dev->config;

	*ext_clock = cfg->ext_clock_hz;
	return VL53L9_ERROR_NONE;
}
