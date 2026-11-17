CC      ?= gcc
CFLAGS  += -std=c11 -Wall -Wextra -Werror -O0 -g
CFLAGS  += -I../../include -I../../lib/plant_json/include -I../../lib/plant_net
CFLAGS  += -I../../lib/plant_sensors/include

TESTS := test_json test_net_util test_soil_calib test_active_wifi

.PHONY: all clean $(TESTS)
all: $(TESTS)

test_json: test_json.c ../../lib/plant_json/json.c
	$(CC) $(CFLAGS) -o $@ $^
	./$@

test_soil_calib: test_soil_calib.c ../../lib/plant_sensors/soil_calib.c
	$(CC) $(CFLAGS) -o $@ $^
	./$@

test_net_util: test_net_util.c ../../lib/plant_net/net_util.c
	$(CC) $(CFLAGS) -o $@ $^
	./$@

test_active_wifi: CFLAGS += -I../../lib/plant_net/include -I../../apps/active-wifi/src
test_active_wifi: test_active_wifi.c ../../apps/active-wifi/src/link.c
	$(CC) $(CFLAGS) -o $@ $^
	./$@

clean:
	rm -f $(TESTS)
