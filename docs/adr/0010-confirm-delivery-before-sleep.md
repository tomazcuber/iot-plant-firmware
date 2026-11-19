---
status: accepted
---

# `main` confirms delivery with one PINGREQ round trip before the radio goes down

QoS 0 publishes (FR-MA-11) rely on TCP for delivery: MQTT requires an ordered, lossless transport, and TCP retransmits until the bytes arrive. Baseline A keeps its connection, so TCP finishes that work. `main` used to tear the link down right after its last publish: `plant_mqtt_publish()` returns once the bytes are in the TCP send buffer, and `plant_mqtt_disconnect()`, `plant_wifi_disconnect()` and `sys_poweroff()` followed with nothing waiting for the broker. Data still queued or lost in flight died with the interface.

On the bench (2026-11-19, issue #22) the broker logged `main`'s CONNECT every cycle, but no PUBLISH reached a `mosquitto_sub` over two cycles, and every session ended by `session taken over` or `exceeded timeout`, never by a DISCONNECT. One `sensors/raw` row in about an hour of cycles reached the logger. The CONNECT got through only because `plant_mqtt_connect()` waits for the CONNACK.

After publishing `status`, `main` now calls `plant_mqtt_flush()`: it sends a PINGREQ and waits until every outstanding PINGREQ is answered. The transport is ordered and the broker reads a client's packets in order, so the PINGRESP proves that both publishes reached the broker. That is the same guarantee baseline A gets from its open connection. A confirmation that doesn't arrive within its timeout (5 s) fails the cycle like a failed publish: no retry (DD-MA-02), and the link is still torn down.

## Considered options

- **Fixed delay before the teardown** (rejected). `k_msleep(N)` with the link up. The delivery stays probabilistic, N has no principled value (it must cover a round trip and a TCP retransmission timeout), and every cycle pays N even when the data arrived long before. Delivery rate would become a variable to correct for in the energy comparison.
- **PINGREQ/PINGRESP barrier** (chosen). One round trip, scaled to the real network instead of a guess. Uses a packet the protocol already defines, with a mandatory reply. Publishes stay QoS 0, so FR-MA-11 holds and both firmwares send identical publications. The new `plant_net` function is not called by baseline A, whose behaviour and energy don't change.
- **QoS 1 in `plant_mqtt_publish()`, both apps** (rejected). Same round trip, but a change to the shared stack (ADR-0006): baseline A would have to be measured again, and it would pay a PUBACK per publication for a guarantee its live TCP connection already gives. QoS 1 promises at-least-once delivery, which rests on re-sending unacknowledged publishes when a client reconnects with CleanSession 0. Both apps use clean sessions and `main` discards a failed cycle, so neither could keep that promise.
- **QoS 1 in `main` only** (rejected). Same at-least-once mismatch, and the two stacks would differ in a protocol parameter, which ADR-0006 exists to avoid.
- **At-least-once delivery** (out of scope). QoS 1 with a persistent session, unacknowledged messages kept across deep sleep (retained RAM or flash) and re-sent on the next cycle. It answers a different requirement ("no gaps"), contradicts DD-MA-02's domain argument and gives cycles with and without re-sends different energy profiles. Future work.

## Consequences

- The confirmation round trip comes after `status` is encoded, so it is in no `t_*` term (as the rest of the teardown, see `CycleResult` and ADR-0001). It appears in the logged "ms to sleep" and in the INA219 trace, which is where the reduced energy model measures energy. It is a cost of the deep-sleep architecture: a device that powers its radio off must confirm delivery first; one that stays connected doesn't.
- DD-MA-02 still holds: no per-message acknowledgement and no retry. The cycle now has one round trip for all its publishes.
- The DISCONNECT and the TCP close can still be lost in the teardown, so the broker may keep logging `session taken over`. That is cosmetic: sessions are clean and the data already arrived.
- The measurement campaign records the delivery rate of both firmwares (rows in the logger DB over cycles in the trace). With the barrier it should be close to 100 % in both, which is the evidence that they deliver the same service.
