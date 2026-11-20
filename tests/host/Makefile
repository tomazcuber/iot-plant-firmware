CC      ?= gcc
CXX     ?= g++
CFLAGS  += -std=c11 -Wall -Wextra -Werror -O0 -g
CFLAGS  += -I../../include -I../../lib/plant_json/include -I../../lib/plant_net
CFLAGS  += -I../../lib/plant_sensors/include

CXXFLAGS += -std=c++17 -Wall -Wextra -Werror -O0 -g
CXXFLAGS += -I../../include -I../../lib/plant_json/include

TESTS := test_json test_net_util test_soil_calib test_active_wifi test_main_cycle \n	 test_main_retained

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

# apps/main is C++17 (DD-SM-02); the shared encoder stays C and is linked in.
test_main_cycle: CXXFLAGS += -I../../lib/plant_net/include -I../../lib/plant_sensors/include
test_main_cycle: CXXFLAGS += -I../../apps/main/src
test_main_cycle: test_main_cycle.cpp ../../apps/main/src/cycle.cpp ../../lib/plant_json/json.c
	$(CC) $(CFLAGS) -c -o json_main_cycle.o ../../lib/plant_json/json.c
	$(CXX) $(CXXFLAGS) -o $@ test_main_cycle.cpp ../../apps/main/src/cycle.cpp json_main_cycle.o
	rm -f json_main_cycle.o
	./$@

test_main_retained: CXXFLAGS += -I../../apps/main/src
test_main_retained: test_main_retained.cpp ../../apps/main/src/retained.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^
	./$@

clean:
	rm -f $(TESTS) json_main_cycle.o
