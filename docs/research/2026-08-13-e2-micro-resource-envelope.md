# GCE e2-micro resource envelope for the MQTT backend

> Research note. Answers the planning question of whether a GCE `e2-micro` Always Free
> instance (Docker Compose deployment) can host the plant-monitoring MQTT backend at
> increasing levels of ambition: broker + logger only, broker + TSDB, broker + TSDB + Grafana.
> Workload assumption throughout: at most two devices, one small JSON publish pair roughly
> every 5 minutes per device — negligible message throughput, RAM is the binding constraint
> (see CPU note at the end).
>
> Convention note: this repository has no pre-existing `docs/research/` folder or research-note
> template (checked `docs/adr/`, `docs/superpowers/specs/`, `docs/superpowers/plans/`,
> `docs/thesis/` — none match a "dated research memo" shape), so this file uses a plain dated
> filename per the task's fallback instruction. The `.scratch/mqtt-backend/issues/08-e2-micro-resource-envelope.md`
> context file named in the task brief does not exist anywhere in this repository's history
> (checked working tree, `migration-wip`, and `git log --all`) — this note was written directly
> from the research brief's own enumerated questions.
>
> Every numeric claim below is tagged **[official]** (vendor's own docs/pricing/GitHub/Docker
> Hub page), **[measured, third-party]** (a reputable benchmark or tracker, not the vendor), or
> **[estimate]** (no citable source found; reasoned approximation, flagged as such per the brief).

## 1. e2-micro platform facts

- **e2-micro**: 2 vCPUs shared-core, sustaining a combined **0.25 vCPU** (12.5% of each of 2
  cores) baseline with burst capacity, and **1 GB memory**. **[official]**
  Google Cloud, "General-purpose machine family for Compute Engine" — E2 shared-core machine
  types table and burst description:
  https://cloud.google.com/compute/docs/general-purpose-machines#e2-shared-core
  (Note: this page renders as a very large single doc; automated fetch of the live page
  truncated before the E2 table in this session, so the 1 GB / 0.25 vCPU figures above were
  cross-checked against Google's own Compute Engine FAQ / machine-family description language
  surfaced via search snippets of the same URL, and against two independent third-party specs
  mirrors that quote the same official table: https://gcloud-compute.com/e2-micro.html and
  https://sparecores.com/server/gcp/e2-micro. These are widely-known, stable, non-controversial
  figures for this machine type.)

- **Always Free eligibility**: exactly **one non-preemptible `e2-micro` instance per month**,
  restricted to three US regions — `us-west1` (Oregon), `us-central1` (Iowa), `us-east1` (South
  Carolina) — plus 30 GB-months of standard persistent disk and 1 GB/month of North-America
  egress. **[official]**
  Google Cloud, "Free cloud features and trials":
  https://cloud.google.com/free/docs/free-cloud-features

- **Swap on GCE VMs**: **not officially documented** for standard Compute Engine VM images.
  The Compute Engine FAQ (https://cloud.google.com/compute/docs/faq) contains no mention of
  swap at all, and there is no equivalent-to-GKE "configure swap" doc for plain GCE VMs (GKE
  has one, for node pools, which is a different product surface:
  https://cloud.google.com/kubernetes-engine/docs/how-to/node-memory-swap — not applicable
  here). The common operational claim that stock Debian/Ubuntu GCE images boot with **no swap
  partition/file configured by default** is **not backed by an official GCP statement** found
  in this research pass — treat it as operational folklore, correct in practice but unverified
  against a primary source. Implication: don't rely on swap as a safety margin; size the stack
  to fit in physical RAM.

## 2. Minimal stack: Docker Engine + Mosquitto + Python logger

| Component | Figure | Quality | Source |
|---|---|---|---|
| Docker Engine daemon overhead (dockerd + containerd, idle) | **~137 MB** (dockerd 72.9 MB + containerd 63.8 MB) | **[measured, third-party]** | ComputingForGeeks Docker vs CRI-O vs containerd benchmark: https://computingforgeeks.com/docker-vs-cri-o-vs-containerd/ |
| Docker install requirements | No RAM figure stated at all — only OS version/architecture | **[official, negative result]** | Docker Engine install docs (Ubuntu): https://docs.docker.com/engine/install/ubuntu/ |
| Eclipse Mosquitto memory footprint | No published MB figure. Broker exposes a `memory_limit` config knob (heap byte cap) rather than documenting a typical footprint; official Docker Hub image is Alpine-based, **9.5 MB image size** (disk, not RAM) | **[official, negative result on RAM; official on image basis]** | `mosquitto.conf` docs, `memory_limit`: https://mosquitto.org/man/mosquitto-conf-5.html ; Docker Hub page: https://hub.docker.com/_/eclipse-mosquitto |
| Mosquitto runtime RAM at this workload (2 clients, 1 msg pair/5 min) | Realistically low tens of MB at most — it is a C broker with a few connections and near-zero queued messages | **[estimate]**, no canonical source; the `memory_limit` mechanism plus the tiny Alpine image strongly imply a small footprint by design, but neither is a direct RAM measurement | — |
| Minimal Python process (paho-mqtt + sqlite3, one small subscriber/logger) | Roughly **20–40 MB** RSS (CPython interpreter baseline is commonly cited around 10 MB+ before imports; paho-mqtt and sqlite3 add modest overhead) | **[estimate]** — no single canonical source for CPython idle RSS; figure is a reasoned range from common knowledge of interpreter+small-library memory, not a citation | — |
| Ubuntu/Debian minimal OS footprint | Debian's own installer guide states install is possible with **350 MB (with swap)** or a **780 MB** floor for the graphical installer — these are install-time minimums, not a running idle-RSS figure | **[official, but wrong metric]** | Debian Installation Guide §3.4, "Meeting Minimum Hardware Requirements": https://www.debian.org/releases/stable/amd64/ch03s04.en.html |
| Ubuntu/Debian minimal *idle running* RAM (headless server, post-boot) | Roughly **50–150 MB** | **[estimate]** — no official post-boot idle-RSS figure was found; this is a commonly-observed range for a minimal headless Debian/Ubuntu cloud image, not a cited measurement | — |

**Reasoned math for (a):**

```
Docker Engine daemon (dockerd+containerd)   ~137 MB   [measured, third-party]
OS idle (headless minimal image)            ~100 MB   [estimate]
Mosquitto container                          ~20 MB   [estimate, informed by official design intent]
Python logger container                      ~30 MB   [estimate]
-----------------------------------------------------
Subtotal                                    ~287 MB
Headroom on 1024 MB e2-micro                ~737 MB
```

**(a) The minimal stack — Mosquitto + Python logger under Docker Compose — fits e2-micro
comfortably**, with roughly 700 MB of headroom for kernel page cache, container image layers,
and burst activity. This conclusion is high-confidence even though several line items are
estimates, because the estimated items are individually small and the subtotal is well under
half the available RAM.

## 3. Adding a time-series database

- **InfluxDB OSS**: the only InfluxData doc with a published RAM figure is the **v1** hardware
  sizing guide, whose smallest documented tier is **2–4 vCPU cores / 2–4 GB RAM / 500 IOPS**,
  for workloads under 5,000 writes/sec, under 5 queries/sec, under 100,000 unique series —
  already far beyond this project's ~2 writes/10 min. **[official]**
  InfluxDB OSS v1, "Hardware sizing guidelines": https://docs.influxdata.com/influxdb/v1/guides/hardware_sizing/
  InfluxDB OSS **v2**'s own install docs state **no RAM/CPU/disk minimum at all** for any
  platform except Raspberry Pi (which needs a Pi 4+ and 64-bit OS) — a documented gap, not a
  documented "it's fine on 1 GB" claim. **[official, negative result]**
  InfluxDB OSS v2 install docs: https://docs.influxdata.com/influxdb/v2/install/
  Since InfluxData's *only* published RAM number for any OSS version (2–4 GB, v1) already
  exceeds the entire e2-micro machine, and v2 adds more components (Flux engine, HTTP API,
  UI) on the same storage engine lineage, there is no official basis to expect InfluxDB OSS to
  fit in the ~730 MB of headroom left after the minimal stack, let alone the full 1 GB.

- **PostgreSQL / TimescaleDB**: PostgreSQL's own docs discuss sub-1 GB systems explicitly and
  give a small, tunable default. `shared_buffers` **defaults to 128 MB**, with the docs'
  own guidance being "on systems with less than 1 GB of RAM, a smaller percentage of RAM is
  appropriate, so as to leave adequate space for the operating system" — i.e., Postgres is
  officially documented as usable, with reduced settings, below 1 GB total system RAM.
  **[official]** PostgreSQL docs, §19.4.1 Resource Consumption / Memory:
  https://www.postgresql.org/docs/current/runtime-config-resource.html
  TimescaleDB ships as a Postgres extension; its own tuning tool (`timescaledb-tune`) does not
  impose a minimum either — it reads whatever RAM is available and proposes settings
  accordingly, rather than requiring a floor. **[official, negative result — no stated
  minimum]** TimescaleDB docs, "TimescaleDB tuning tool": https://docs.timescale.com/self-hosted/latest/configuration/timescaledb-tune/

**Reasoned math for (b), using Postgres/TimescaleDB (not InfluxDB, which is ruled out above):**

```
Minimal-stack subtotal (from §2)            ~287 MB
Postgres/TimescaleDB process
  (shared_buffers ~128–192 MB tuned small
  + backend/connection overhead)            ~220–280 MB   [estimate, anchored to official
                                                            shared_buffers default/guidance]
-----------------------------------------------------
Subtotal                                    ~510–570 MB
Headroom on 1024 MB e2-micro                ~450–510 MB
```

**(b) TSDB alongside Mosquitto: fits only if the TSDB is PostgreSQL/TimescaleDB, tuned for a
small-memory host per Postgres's own documented guidance — not InfluxDB**, whose only
official sizing figures (2–4 GB, from the v1 docs) already exceed the whole machine. The
Postgres/TimescaleDB path leaves ~450–500 MB headroom, which is workable but not generous —
recommend keeping `shared_buffers` and `work_mem` conservative and monitoring RSS in practice,
since several inputs above are estimates rather than citations.

## 4. Adding Grafana on top

- **Grafana OSS minimum**: **512 MB RAM, 1 core**, stated as a floor for evaluation, with the
  same page noting actual needs scale with concurrent users, alert rule count/frequency, data
  source count, and dashboard refresh rate — and citing **2–4 GB** for even a small production
  setup. **[official]**
  Grafana Labs, "Install Grafana": https://grafana.com/docs/grafana/latest/setup-grafana/installation/

**Reasoned math for (c):**

```
Mosquitto + Python logger + Docker + OS      ~287 MB
Postgres/TimescaleDB (tuned small)        ~220–280 MB
Grafana OSS (official documented minimum)    512 MB   [official]
-----------------------------------------------------
Subtotal                                ~1019–1079 MB
1024 MB e2-micro total                      1024 MB
```

**(c) Mosquitto + TSDB + Grafana does not fit e2-micro.** Grafana's own documented minimum
(512 MB) alone consumes roughly half the machine; stacked on top of the already-tight
Mosquitto+Postgres subtotal from §3, the total meets or exceeds the entire 1 GB before
accounting for kernel/page-cache/container overhead margin or any burst — and with no
officially-confirmed swap available as a safety net (§1). This is a hard "no" grounded in
Grafana's own stated floor, not just the estimated line items.

### Recommended next-size instance if Grafana is required

Move Mosquitto + TSDB + Grafana off e2-micro onto a bigger, non-free-tier machine in the same
E2 shared-core family:

- **e2-small** (2 vCPU shared-core, **2 GB RAM**) — roughly doubles available headroom versus
  e2-micro; the ~1.0–1.1 GB stack above would then leave ~0.9–1.0 GB free. Reasonable minimum
  choice.
- **e2-medium** (2 vCPU shared-core, **4 GB RAM**) — more comfortable margin for Grafana under
  real usage (alerting, multiple dashboards) rather than just its documented floor.

Pricing, on-demand, `us-central1` (Iowa) — the same region the Always Free e2-micro would run
in:
- **e2-small: ≈ $0.0167/hr ≈ $12.23/month** — **[measured, third-party, cross-checking the
  official pricing page]**. The official Google Cloud Compute Engine pricing page
  (https://cloud.google.com/compute/all-pricing and https://cloud.google.com/compute/vm-instance-pricing)
  is the authoritative source, but its pricing tables are rendered by JavaScript and this
  session's automated fetch of that page returned a truncated/empty body rather than table
  rows, so the number above is corroborated instead via two independent third-party GCP price
  trackers that source from Google's published rate card: https://www.economize.cloud/resources/gcp/pricing/compute-engine/e2-small/
  and https://cloudprice.net/gcp/compute/instances/e2-small — **treat as accurate but not
  independently confirmed from the primary page in this session; re-check
  https://cloud.google.com/compute/vm-instance-pricing directly before quoting a firm number
  in the thesis.**
- **e2-medium: ≈ $0.0335/hr ≈ $24.46/month** — same sourcing caveat.
  https://www.economize.cloud/resources/gcp/pricing/compute-engine/e2-medium/ and
  https://gcloud-compute.com/e2-medium.html (both agree to the cent).

**Important cost implication**: only `e2-micro` is Always-Free-eligible (§1). Moving to
`e2-small` or `e2-medium` means leaving the $0/month tier entirely — for a Bachelor's thesis
budget this is a real decision point (~$12–25/month), not just a technical one.

## 5. Summary answers

| Question | Answer | Confidence |
|---|---|---|
| (a) Mosquitto + Python logger fits in 1 GB | **Yes, comfortably** (~287 MB used, ~737 MB free) | High — subtotal is well under half of RAM even treating every estimated line generously |
| (b) + TSDB still fits | **Yes, but only with Postgres/TimescaleDB tuned small, not InfluxDB** (~510–570 MB used, ~450–510 MB free). InfluxDB OSS is ruled out by InfluxData's own only published sizing figure (2–4 GB, v1 docs), which alone exceeds the whole machine. | Medium — Postgres path is officially documented as viable under 1 GB; the exact process RSS is an estimate |
| (c) + Grafana on top | **No — requires a bigger instance.** Grafana's own documented minimum (512 MB) alone plus the existing stack meets/exceeds all 1024 MB, with no confirmed swap safety net. | High — driven by Grafana's own official minimum, not just estimates |
| Recommended step-up | **e2-small** (2 GB RAM, ≈$12.23/mo) as the minimum comfortable choice; **e2-medium** (4 GB RAM, ≈$24.46/mo) for more headroom under real (non-floor) Grafana usage | Pricing sourced via third-party trackers cross-checking Google's rate card; official pricing page fetch was inconclusive this session and should be re-verified directly |

### CPU note (not the binding constraint, but flagged)

e2-micro's 0.25 vCPU sustained baseline (§1) is a real constraint for Grafana dashboard
rendering/query execution under concurrent viewers, independent of RAM. At this project's
message rate (RAM is clearly the binding constraint per the math above), CPU was not explored
further — if Grafana is added on a bigger instance, watch CPU burst credits/sustained-use
behavior for the E2 family separately, since e2-small/e2-medium share the same "shared-core...
burst for short periods" model as e2-micro (https://cloud.google.com/compute/docs/general-purpose-machines#e2-shared-core).
