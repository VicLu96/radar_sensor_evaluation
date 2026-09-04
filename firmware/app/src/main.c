/*
 * VL53L9CX bring-up application.
 *
 * This is not a demo. It walks the staged gates from
 * firmware/drivers/vl53l9cx/README.md in order, and each stage prints enough
 * to tell success from the specific way it failed — because this board has no
 * known-good reference, and "nothing happened" is the expensive outcome.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

#include <vl53l9cx/vl53l9cx.h>

LOG_MODULE_REGISTER(bringup, LOG_LEVEL_INF);

#define TOF_NODE DT_ALIAS(tof0)

/* ~18 KB. Static, not on the stack — one of these is most of a thread stack. */
static struct vl53l9cx_frame frame;

/*
 * GATE 0 — say out loud what the board file claims.
 *
 * Two of these three are placeholders until Victor confirms them against the
 * schematic, and a wrong rail value does not fail loudly: it misconfigures the
 * analogue front end and the sensor returns plausible nonsense. Printing them
 * first means a wrong one is visible before anything else has a chance to
 * confuse the picture.
 */
static void report_board_config(void)
{
	LOG_INF("---- board configuration (from devicetree) ----");
	LOG_INF("  I2C address    0x%02x (7-bit)", DT_REG_ADDR(TOF_NODE));
	LOG_INF("  VDDA           %d uV   <-- CONFIRM against the schematic",
		DT_PROP(TOF_NODE, vdda_microvolt));
	LOG_INF("  VDDIO          %d uV   <-- CONFIRM against the schematic",
		DT_PROP(TOF_NODE, vddio_microvolt));
	LOG_INF("  AP_CLK         %d Hz", DT_PROP(TOF_NODE, ext_clock_frequency));
	LOG_INF("  AP_CLK source  %s",
		DT_NODE_HAS_PROP(TOF_NODE, clock_pwms) ? "MCU PWM" : "board oscillator");
	LOG_INF("  blob chunk     %d bytes", DT_PROP(TOF_NODE, blob_chunk_size));
	LOG_INF("----------------------------------------------");
	LOG_INF("If the sensor never answers: scope AP_CLK first. No clock, no ACK.");
}

/*
 * Zephyr's device PM is a state machine, and the transitions are not free-form:
 * TURN_OFF is only legal from SUSPENDED, and TURN_ON lands in SUSPENDED rather
 * than ACTIVE. So powering the sensor down is SUSPEND then TURN_OFF, and
 * bringing it back is TURN_ON then RESUME. Skipping a step returns -ENOTSUP
 * rather than doing something dangerous, but it does mean nothing happens.
 */
static int sensor_off(const struct device *dev)
{
	int ret = pm_device_action_run(dev, PM_DEVICE_ACTION_SUSPEND);

	if (ret < 0 && ret != -EALREADY) {
		LOG_ERR("SUSPEND failed (%d)", ret);
		return ret;
	}

	ret = pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_OFF);
	if (ret < 0) {
		LOG_ERR("TURN_OFF failed (%d)", ret);
	}
	return ret;
}

static int sensor_on(const struct device *dev)
{
	int ret = pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_ON);

	if (ret < 0) {
		LOG_ERR("TURN_ON failed (%d) — the blob reload did not complete",
			ret);
		return ret;
	}

	ret = pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME);
	if (ret < 0) {
		LOG_ERR("RESUME failed (%d)", ret);
	}
	return ret;
}

/* GATE 3 — one frame, and enough about it to know whether it is real. */
static int capture_and_report(const struct device *dev, enum vl53l9cx_res res,
			      const char *label)
{
	uint32_t valid = 0, sum = 0, min = UINT32_MAX, max = 0;
	uint16_t zones;
	int64_t t0 = k_uptime_get();
	int ret;

	ret = vl53l9cx_capture(dev, res, &frame, K_SECONDS(2));
	if (ret < 0) {
		LOG_ERR("%s: capture failed (%d)", label, ret);
		return ret;
	}

	zones = (uint16_t)frame.cols * frame.rows;

	for (uint16_t i = 0; i < zones; i++) {
		if (!frame.zone[i].valid) {
			continue;
		}
		valid++;
		sum += frame.zone[i].distance_mm;
		min = MIN(min, frame.zone[i].distance_mm);
		max = MAX(max, frame.zone[i].distance_mm);
	}

	LOG_INF("%s: %ux%u in %lld ms — %u/%u zones valid",
		label, frame.cols, frame.rows, k_uptime_get() - t0, valid, zones);

	if (valid > 0) {
		LOG_INF("  distance min %u mm, mean %u mm, max %u mm",
			min, sum / valid, max);
	} else {
		LOG_WRN("  no valid zones. Either nothing is in range, or the "
			"depth word is being read wrong — check byte order "
			"before blaming the scene.");
	}

	/* The device's own frame counter, from the status line. If this stops
	 * advancing while ours does, the driver is reading a stale buffer; if
	 * it jumps, the sensor produced frames nobody collected. Neither is
	 * visible any other way.
	 */
	LOG_INF("  device frame counter %u (driver seq %u), temperature raw %u",
		frame.frame_counter, frame.seq, frame.temperature);

	return 0;
}

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(TOF_NODE);
	int ret;

	LOG_INF("VL53L9CX bring-up");
	report_board_config();

	/* GATE 1 — did the driver bind? A failure here is init: the blob upload
	 * did not complete, which usually means the sensor never answered.
	 */
	if (!device_is_ready(dev)) {
		LOG_ERR("device not ready — vl53l9cx_init() failed.");
		LOG_ERR("In order: (1) AP_CLK on a scope, (2) the sensor rail, "
			"(3) XSHUT actually driven high, (4) address 0x29 "
			"probed in read-byte mode — NOT a general i2cdetect "
			"sweep, which can wedge this part.");
		return -ENODEV;
	}

	/* GATE 2 — the blob upload duration. A measurement, not a log line: it
	 * sets the cost of PM_DEVICE_ACTION_TURN_OFF and therefore decides
	 * whether powering the sensor down between readings beats standby.
	 * Expected around 250 ms at 400 kHz for 9,865 bytes.
	 */
	LOG_INF("firmware blob upload: %u ms", vl53l9cx_last_boot_ms(dev));

	/* GATE 3 — start small. 12x10 is 880 bytes and is in the WIDE family,
	 * so it shares the full-resolution field of view. If this works and
	 * 54x42 does not, the problem is transfer size, not the sensor.
	 */
	ret = capture_and_report(dev, VL53L9CX_RES_12X10, "12x10");
	if (ret < 0) {
		return ret;
	}

	/* GATE 4 — the full frame: 14,842 bytes, ~404 ms at 400 kHz. */
	ret = capture_and_report(dev, VL53L9CX_RES_54X42, "54x42");
	if (ret < 0) {
		return ret;
	}

	/* GATE 5 — the power transition the whole project rests on. TURN_OFF
	 * drops the rail, XSHUT and AP_CLK; TURN_ON pays a full blob reload.
	 * If a capture works after that round trip, the stage-4 architecture is
	 * viable. Put the Power Profiler on the sensor rail here.
	 */
	LOG_INF("power cycling the sensor domain");

	ret = sensor_off(dev);
	if (ret < 0) {
		return ret;
	}

	k_sleep(K_SECONDS(1));

	ret = sensor_on(dev);
	if (ret < 0) {
		return ret;
	}

	LOG_INF("blob reload after power cycle: %u ms",
		vl53l9cx_last_boot_ms(dev));

	ret = capture_and_report(dev, VL53L9CX_RES_12X10, "12x10 after power cycle");
	if (ret < 0) {
		return ret;
	}

	/* GATE 6 — the dwell loop, which is what this node actually does: wake,
	 * one frame, sensor off. At 0.1 Hz the sensor should be off for over
	 * 90% of the time.
	 */
	LOG_INF("entering dwell loop: one frame every 10 s at 12x10");

	while (1) {
		if (sensor_on(dev) == 0) {
			(void)capture_and_report(dev, VL53L9CX_RES_12X10, "dwell");
			(void)sensor_off(dev);
		}
		k_sleep(K_SECONDS(10));
	}

	return 0;
}
