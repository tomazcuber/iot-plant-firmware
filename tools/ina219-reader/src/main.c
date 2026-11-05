/*
 * INA219 current reader (docs/superpowers/specs/2026-11-03-ina219-reader-design.md).
 *
 * Reads the INA219 shunt-voltage register every 500 us (RD-05), samples two
 * phase-marker inputs after each read (RD-08) and streams text lines over the
 * native USB-Serial/JTAG (RD-02, RD-10). Raw register values only (RD-07,
 * ADR-0007): conversion to mA happens on the host.
 *
 * Threads: sampler (reads, fills the queue) -> writer (drains to USB) and the
 * main thread (command parser). Console/logging stay on UART0.
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/atomic.h>

#include "reader_git.h" /* generated at build time (CMakeLists.txt) */

#ifndef READER_GIT
#define READER_GIT "unknown"
#endif

/* --- INA219 (datasheet SBOS448, register map and configuration register) --- */
#define INA219_ADDR        0x40
#define INA219_REG_CONFIG  0x00
#define INA219_REG_SHUNT   0x01
#define INA219_REG_BUS     0x02
#define INA219_BUS_CNVR    BIT(1)

/*
 * Configuration register fields: bit 13 BRNG, bits 12-11 PG, bits 10-7 BADC,
 * bits 6-3 SADC, bits 2-0 MODE.
 *   BRNG = 0     16 V bus range
 *   PG   = 0b11  /8, +-320 mV
 *   BADC = 0b0011 12-bit (used by the vbus snapshot only)
 *   SADC = 0b0011 12-bit, 1 sample, 532 us        (Trace)
 *   SADC = 0b1111 128 samples, 68.10 ms           (Averaged)
 *   MODE = 0b101 shunt voltage, continuous
 */
#define CFG_FIELDS(sadc, mode) \
	((0U << 13) | (0x3U << 11) | (0x3U << 7) | ((sadc) << 3) | (mode))
#define CFG_TRACE     CFG_FIELDS(0x3U, 0x5U)
#define CFG_AVERAGED  CFG_FIELDS(0xFU, 0x5U)
/* vbus snapshot: same fields, MODE = 0b010 bus voltage, triggered. */
#define CFG_VBUS      CFG_FIELDS(0x3U, 0x2U)
/* Idle configuration (RD-03): configured, not streaming. */
#define CFG_IDLE      CFG_TRACE

BUILD_ASSERT(CFG_TRACE == 0x199D, "RD-04: Trace configuration register");
BUILD_ASSERT(CFG_AVERAGED == 0x19FD, "RD-06: Averaged configuration register");

#define SAMPLE_PERIOD_US 500
#define QUEUE_DEPTH      4096
#define OVF_REPORT_MS    1000
/* Data: tolerate a host stall up to about the queue's capacity (4096 samples at
 * 2 kHz = 2.05 s); beyond that samples are dropped and counted (RD-09). */
#define TX_STALL_DATA_MS  2000
/* Replies: short, so a missing host never blocks the command thread for long. */
#define TX_STALL_REPLY_MS 200
/* vbus: an Averaged conversion (68.1 ms) may be in progress, plus margin. */
#define VBUS_TIMEOUT_MS   150

enum run_cfg { CFG_NONE, RUN_TRACE, RUN_AVERAGED };

struct sample {
	uint64_t t_us;
	uint32_t seq;
	int16_t shunt;
	uint8_t phase;
};

static const struct device *const i2c = DEVICE_DT_GET(DT_NODELABEL(i2c0));
static const struct device *const usb = DEVICE_DT_GET(DT_NODELABEL(usb_serial));
static const struct gpio_dt_spec marker[2] = {
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), marker_gpios, 0), /* bit 1 */
	GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), marker_gpios, 1), /* bit 0 */
};

K_MSGQ_DEFINE(sample_q, sizeof(struct sample), QUEUE_DEPTH, 4);
K_MUTEX_DEFINE(tx_lock);
K_SEM_DEFINE(run_sem, 0, 1);      /* wakes the sampler at start */
K_SEM_DEFINE(sampler_idle, 0, 1); /* given by the sampler when it leaves a run */
static struct k_timer tick;

static atomic_t running;
static enum run_cfg run_cfg;
static uint32_t seq_next;       /* samples taken this run (sampler only) */
static atomic_t ovf_total;      /* samples lost before the host: queue full or writer gave up */
static atomic_t i2c_err_total;  /* failed shunt reads (not overflows) */
static atomic_t writer_busy;
static uint64_t t0_cycles;

/* --- USB transmit --------------------------------------------------------- */

/* Write len bytes to the USB FIFO (fifo_fill flushes a full packet per call,
 * unlike poll_out which flushes per byte). Gives up if the host makes no
 * progress for stall_ms. Returns the number of bytes actually written. */
static size_t usb_write(const char *buf, size_t len, int stall_ms)
{
	size_t done = 0;
	int64_t last_progress = k_uptime_get();

	while (done < len) {
		int n = uart_fifo_fill(usb, (const uint8_t *)buf + done, (int)(len - done));

		if (n > 0) {
			done += n;
			last_progress = k_uptime_get();
		} else if (k_uptime_get() - last_progress > stall_ms) {
			break; /* host not reading: drop rather than block forever */
		} else {
			k_busy_wait(100);
		}
	}
	return done;
}

static void reply(const char *fmt, ...) __printf_like(1, 2);
static void reply(const char *fmt, ...)
{
	char line[96];
	va_list ap;

	va_start(ap, fmt);
	int n = vsnprintk(line, sizeof(line) - 1, fmt, ap);
	va_end(ap);
	if (n < 0) {
		return;
	}
	if (n > (int)sizeof(line) - 2) {
		n = sizeof(line) - 2;
	}
	line[n++] = '\n';

	k_mutex_lock(&tx_lock, K_FOREVER);
	(void)usb_write(line, n, TX_STALL_REPLY_MS);
	k_mutex_unlock(&tx_lock);
}

/* --- INA219 --------------------------------------------------------------- */

static int ina_write(uint8_t reg, uint16_t val)
{
	uint8_t b[3] = {reg, val >> 8, val & 0xFF};

	return i2c_write(i2c, b, sizeof(b), INA219_ADDR);
}

static int ina_read(uint8_t reg, uint16_t *val)
{
	uint8_t b[2];
	int ret = i2c_write_read(i2c, INA219_ADDR, &reg, 1, b, sizeof(b));

	if (ret == 0) {
		*val = ((uint16_t)b[0] << 8) | b[1];
	}
	return ret;
}

/* RD-12: one triggered bus conversion, then back to the idle configuration.
 * Returns mV, -ETIMEDOUT if CNVR never set within VBUS_TIMEOUT_MS, or another
 * negative errno for an I2C failure. */
static int vbus_snapshot_mv(void)
{
	uint16_t bus = 0;
	int ret = ina_write(INA219_REG_CONFIG, CFG_VBUS);

	if (ret == 0) {
		int64_t deadline = k_uptime_get() + VBUS_TIMEOUT_MS;

		do {
			k_msleep(1);
			ret = ina_read(INA219_REG_BUS, &bus);
		} while (ret == 0 && !(bus & INA219_BUS_CNVR) && k_uptime_get() < deadline);
		if (ret == 0 && !(bus & INA219_BUS_CNVR)) {
			ret = -ETIMEDOUT;
		}
	}
	(void)ina_write(INA219_REG_CONFIG, CFG_IDLE);
	if (ret != 0) {
		return ret;
	}
	return (bus >> 3) * 4; /* bits 15-3, 4 mV per LSB */
}

/* --- sampler -------------------------------------------------------------- */

static uint8_t read_phase(void)
{
	int hi = gpio_pin_get_raw(marker[0].port, marker[0].pin);
	int lo = gpio_pin_get_raw(marker[1].port, marker[1].pin);

	return (hi > 0 ? 2 : 0) | (lo > 0 ? 1 : 0);
}

static void sampler_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		k_sem_take(&run_sem, K_FOREVER);

		int32_t last_raw = INT32_MIN;

		while (atomic_get(&running)) {
			k_timer_status_sync(&tick);
			if (!atomic_get(&running)) {
				break;
			}

			uint16_t raw;
			int err = ina_read(INA219_REG_SHUNT, &raw);
			uint64_t t_us = k_cyc_to_us_floor64(k_cycle_get_64() - t0_cycles);
			uint8_t phase = read_phase();

			if (err == 0 && run_cfg == RUN_AVERAGED && (int16_t)raw == last_raw) {
				continue; /* RD-06: emit only on change */
			}
			if (err == 0) {
				last_raw = (int16_t)raw;
			}

			struct sample s = {
				.t_us = t_us,
				.seq = seq_next++, /* counts every sample taken, dropped or not */
				.shunt = (int16_t)raw,
				.phase = phase,
			};

			if (err != 0) {
				atomic_inc(&i2c_err_total); /* RD-09: shows as a seq gap on the host */
			} else if (k_msgq_put(&sample_q, &s, K_NO_WAIT) != 0) {
				atomic_inc(&ovf_total); /* RD-09: shows as a seq gap on the host */
			}
		}
		k_sem_give(&sampler_idle);
	}
}

/* --- writer --------------------------------------------------------------- */

static void writer_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	static char out[1024];
	atomic_val_t reported = 0;
	atomic_val_t reported_i2c = 0;
	int64_t last_report = k_uptime_get();

	for (;;) {
		struct sample s;
		size_t len = 0;
		size_t lines = 0;

		if (k_msgq_get(&sample_q, &s, K_MSEC(10)) == 0) {
			atomic_set(&writer_busy, 1);
			do {
				lines++;
				len += snprintk(out + len, sizeof(out) - len, "%u,%llu,%d,%u%u\n",
						s.seq, s.t_us, s.shunt, (s.phase >> 1) & 1,
						s.phase & 1);
			} while (sizeof(out) - len > 64 && k_msgq_get(&sample_q, &s, K_NO_WAIT) == 0);

			k_mutex_lock(&tx_lock, K_FOREVER);
			size_t done = usb_write(out, len, TX_STALL_DATA_MS);

			k_mutex_unlock(&tx_lock);
			if (done < len) {
				/* Host stalled longer than the queue could cover: count every
				 * line not fully written (a straddling line is lost too). */
				size_t sent = 0;

				for (size_t i = 0; i < done; i++) {
					sent += (out[i] == '\n');
				}
				atomic_add(&ovf_total, (atomic_val_t)(lines - sent));
			}
			atomic_set(&writer_busy, 0);
		}

		if (atomic_get(&running) && k_uptime_get() - last_report >= OVF_REPORT_MS) {
			last_report = k_uptime_get();
			atomic_val_t now = atomic_get(&ovf_total);

			if (now != reported) {
				reported = now;
				reply("# ovf %ld", (long)now);
			}
			now = atomic_get(&i2c_err_total);
			if (now != reported_i2c) {
				reported_i2c = now;
				reply("# i2c_err %ld", (long)now);
			}
		}
	}
}

K_THREAD_DEFINE(sampler_tid, 2048, sampler_thread, NULL, NULL, NULL, K_PRIO_PREEMPT(1), 0, 0);
K_THREAD_DEFINE(writer_tid, 2048, writer_thread, NULL, NULL, NULL, K_PRIO_PREEMPT(5), 0, 0);

/* --- commands (RD-10) ----------------------------------------------------- */

static void cmd_start(enum run_cfg cfg)
{
	const uint16_t reg = (cfg == RUN_TRACE) ? CFG_TRACE : CFG_AVERAGED;

	if (ina_write(INA219_REG_CONFIG, reg) != 0) {
		reply("# err i2c");
		return;
	}
	run_cfg = cfg;
	seq_next = 0;
	atomic_set(&ovf_total, 0);
	atomic_set(&i2c_err_total, 0);
	k_msgq_purge(&sample_q);
	reply("# started %s 0x%04X", cfg == RUN_TRACE ? "trace" : "averaged", reg);

	t0_cycles = k_cycle_get_64();
	atomic_set(&running, 1);
	k_timer_start(&tick, K_USEC(SAMPLE_PERIOD_US), K_USEC(SAMPLE_PERIOD_US));
	k_sem_give(&run_sem);
}

static void cmd_stop(void)
{
	/* Elapsed run time at stop: lets the host weight the last Averaged value. */
	uint64_t stop_t_us = k_cyc_to_us_floor64(k_cycle_get_64() - t0_cycles);

	atomic_set(&running, 0);
	k_timer_stop(&tick);
	k_sem_take(&sampler_idle, K_FOREVER);
	while (k_msgq_num_used_get(&sample_q) > 0 || atomic_get(&writer_busy)) {
		k_msleep(2);
	}
	(void)ina_write(INA219_REG_CONFIG, CFG_IDLE);
	reply("# stopped n=%u ovf=%ld i2c=%ld t_us=%llu", seq_next, (long)atomic_get(&ovf_total),
	      (long)atomic_get(&i2c_err_total), stop_t_us);
}

static void handle_command(char *line)
{
	bool is_running = atomic_get(&running);

	if (strcmp(line, "id") == 0) {
		reply("# id %s %s", READER_GIT, __DATE__);
	} else if (strcmp(line, "vbus") == 0) {
		if (is_running) {
			reply("# err busy");
			return;
		}
		int mv = vbus_snapshot_mv();

		if (mv == -ETIMEDOUT) {
			reply("# err timeout");
		} else if (mv < 0) {
			reply("# err i2c");
		} else {
			reply("# vbus_mV %d", mv);
		}
	} else if (strcmp(line, "start trace") == 0 || strcmp(line, "start averaged") == 0) {
		if (is_running) {
			reply("# err busy");
			return;
		}
		cmd_start(line[6] == 't' ? RUN_TRACE : RUN_AVERAGED);
	} else if (strcmp(line, "stop") == 0) {
		if (!is_running) {
			reply("# err idle");
			return;
		}
		cmd_stop();
	} else {
		reply("# err unknown");
	}
}

int main(void)
{
	if (!device_is_ready(i2c) || !device_is_ready(usb)) {
		return -ENODEV;
	}
	for (int i = 0; i < 2; i++) {
		if (!gpio_is_ready_dt(&marker[i]) ||
		    gpio_pin_configure_dt(&marker[i], GPIO_INPUT | GPIO_PULL_DOWN) != 0) {
			return -ENODEV;
		}
	}
	k_timer_init(&tick, NULL, NULL);

	/* RD-03: configured and idle; announce once. */
	(void)ina_write(INA219_REG_CONFIG, CFG_IDLE);
	reply("# id %s %s", READER_GIT, __DATE__);

	char line[48];
	size_t n = 0;

	for (;;) {
		uint8_t c;

		if (uart_poll_in(usb, &c) != 0) {
			k_msleep(1);
			continue;
		}
		if (c == '\r') {
			continue;
		}
		if (c == '\n') {
			line[n] = '\0';
			if (n > 0) {
				handle_command(line);
			}
			n = 0;
		} else if (n < sizeof(line) - 1) {
			line[n++] = c;
		} else {
			n = 0; /* over-long line: discard */
			reply("# err unknown");
		}
	}
	return 0;
}
