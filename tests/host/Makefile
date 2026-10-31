CC      ?= gcc
CFLAGS  += -std=c11 -Wall -Wextra -Werror -O0 -g
CFLAGS  += -I../../include -I../../lib/plant_json/include

TESTS := test_json

.PHONY: all clean $(TESTS)
all: $(TESTS)

test_json: test_json.c ../../lib/plant_json/json.c
	$(CC) $(CFLAGS) -o $@ $^ -lm
	./$@

clean:
	rm -f $(TESTS)
