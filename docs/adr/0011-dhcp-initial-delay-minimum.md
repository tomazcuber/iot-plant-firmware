---
status: accepted
---

# The shared network snippet sets the DHCP initial delay to its minimum

Zephyr's DHCPv4 client waits a random whole number of seconds before its first DISCOVER, as RFC 2131 §4.4.1 recommends. With the default `CONFIG_NET_DHCPV4_INITIAL_DELAY_MAX=10` the wait is 1 to 9 s. `main` loses its RAM in deep sleep, so it runs DHCP from scratch and draws a new wait on every wake. `plant_wifi_connect()` counts association and DHCP against one 10 s timeout (DD-MA-03).

The `plant-net` snippet now sets `CONFIG_NET_DHCPV4_INITIAL_DELAY_MAX=2`, the minimum the Kconfig range allows, which makes the wait a fixed 1 s.

## Evidence (2026-11-19, issue #22)

- A diagnostic build of `main` with WiFi driver logging (packet sizes identify DISCOVER and REQUEST) split a typical timer-wake cycle of `t_wifi` ≈ 4.4 s into: scan and association about 1.7 s; association to DISCOVER exactly 1.00 s in all 11 cycles (the configured wait); DISCOVER to REQUEST about 1.1 s (the access point's DHCP server is slow to offer); REQUEST to address about 0.02 s; then ARP, DNS, SNTP, MQTT connect and the first publish about 0.8 s. Every cycle sent exactly one DISCOVER and one REQUEST.
- With the default, association (about 1.7 s) plus a draw of 8 or 9 s plus the offer (about 1.1 s) exceeds the 10 s timeout on a clean network. About 2 in 9 cycles would fail for that reason alone, and the others would carry 4 s of idle, radio-on wait on average.
- With the minimum, the production build ran 11 cycles with `t_wifi` 4.36 to 4.46 s and none failed; all 11 were in the logger DB, and the 4 cycles after the capture too. The one longer cycle (7.4 s) was an SNTP timeout (DD-MA-11, `timestamp_ms` null), not DHCP.
- Earlier the same day, with the minimum, 2 of 10 cycles still timed out during a stretch of poor radio conditions. The minimum removes the self-inflicted wait; it can't protect against a lost DHCP packet (the client retransmits after 4 s) or a slow association. Those cycles fail as DD-MA-02 allows, and the measurement campaign counts them through the delivery rate (ADR-0010).

## Considered options

- **Minimum initial delay** (chosen). One standard Kconfig symbol, DHCP unchanged otherwise. The delay exists so that many clients booting together (after a power cut) don't hit the server at once. A node waking on its own RTC timer is not synchronised with others, and 1 s still spreads simultaneous boots.
- **Static IP** (future work). Removes DHCP, about 2.1 s per cycle on this network, from every wake. It is a change to the shared stack that ADR-0006 names as needing its own measurement, and it makes deployment depend on manual address assignment.
- **Longer WiFi timeout** (rejected). Hides the failures but keeps the random wait, which biases the energy comparison against `main`.
- **Reuse the DHCP lease across deep sleep** (future work). Keep the lease in retained RAM and resume with INIT-REBOOT. It saves most of the DHCP time without a static address, but needs changes in the network stack integration.

## Consequences

- The setting is in the shared snippet (SM-05: apps can't set network symbols, and the snippet is merged after `prj.conf`), so baseline A gets it too. Baseline A runs DHCP only at boot and after a dropped connection; neither is in its per-publication energy window (the first cycle of each run is discarded), so its measured energy is unaffected.
- DHCP is still about half of `main`'s `t_wifi` on this network (1 s wait plus the server's offer delay). The thesis can cite this as the motivation for the two future-work options above.
