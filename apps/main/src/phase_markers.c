/* Phase markers on GPIO18/GPIO19 (FR-MA-18); interface in phase_markers.h. */
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#include "phase_markers.h"

#define MARKERS DT_PATH(zephyr_user)

BUILD_ASSERT(DT_PROP_LEN(MARKERS, marker_gpios) == 2, "marker-gpios needs bit 1 and bit 0");
BUILD_ASSERT(DT_SAME_NODE(DT_GPIO_CTLR_BY_IDX(MARKERS, marker_gpios, 0),
			  DT_GPIO_CTLR_BY_IDX(MARKERS, marker_gpios, 1)),
	     "both marker pins must be on one port for a single register write");
BUILD_ASSERT((DT_GPIO_FLAGS_BY_IDX(MARKERS, marker_gpios, 0) & GPIO_ACTIVE_LOW) == 0 &&
		     (DT_GPIO_FLAGS_BY_IDX(MARKERS, marker_gpios, 1) & GPIO_ACTIVE_LOW) == 0,
	     "the raw port write assumes active-high marker pins");

static const struct gpio_dt_spec bit1 = GPIO_DT_SPEC_GET_BY_IDX(MARKERS, marker_gpios, 0);
static const struct gpio_dt_spec bit0 = GPIO_DT_SPEC_GET_BY_IDX(MARKERS, marker_gpios, 1);

static bool ready;

int phase_markers_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&bit1) || !gpio_is_ready_dt(&bit0)) {
		return -ENODEV;
	}
	ret = gpio_pin_configure_dt(&bit1, GPIO_OUTPUT_INACTIVE);
	if (ret == 0) {
		ret = gpio_pin_configure_dt(&bit0, GPIO_OUTPUT_INACTIVE);
	}
	ready = (ret == 0);
	return ret;
}

void phase_markers_set(enum phase_marker code)
{
	const gpio_port_pins_t mask = BIT(bit1.pin) | BIT(bit0.pin);
	gpio_port_value_t value = 0;

	if (!ready) {
		return;
	}
	if (code & 0x2) {
		value |= BIT(bit1.pin); /* GPIO18 */
	}
	if (code & 0x1) {
		value |= BIT(bit0.pin); /* GPIO19 */
	}
	/* One write to the port's output register (gpio_esp32.c, under
	 * irq_lock): no intermediate code such as 01 -> 11 -> 10. */
	(void)gpio_port_set_masked_raw(bit1.port, mask, value);
}
