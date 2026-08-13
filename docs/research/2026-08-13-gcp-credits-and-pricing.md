# GCP credit applicability & Compute Engine pricing

Research date: 2026-08-13

Answers the questions in `.scratch/mqtt-backend/issues/07-gcp-credits-and-pricing.md`
(a wayfinder research ticket for the MQTT backend on GCP). Sourced from official
Google pages only (cloud.google.com / docs.cloud.google.com / developers.google.com /
support.google.com) — no third-party blogs or forum summaries. Each claim below is
followed by its exact source URL.

## 1. Does the Google AI Pro ~$10/month Cloud credit apply to Compute Engine billing?

**Short answer: plausibly yes, but not textually guaranteed** — the credit's stated
scope is general ("all Google Cloud products" / "any Google Cloud product"), and no
official page names Compute Engine specifically, positively or negatively.

- Google One plan-benefits page: "Google Cloud credits: This includes a $10 Google
  Cloud credits each month" (no product restriction stated on this page).
  Source: https://support.google.com/googleone/answer/14534406?hl=en

- Google Developer Program benefits FAQ (the terms page for this credit): "You can
  apply the Google Cloud credit to usage of all Google Cloud products including
  Firebase, Vertex AI, and the Google Maps API." Confirmed via direct fetch; page
  states "Last updated 2026-08-04 UTC". A related, separate $500 one-time credit
  benefit on the same page is described even more broadly: "Cloud credits may be
  spent on any Google Cloud Platform or Google Maps Platform product." Compute
  Engine is not named explicitly anywhere on the page — the broadest explicit list
  given is "Firebase, Vertex AI, and the Google Maps API" — but the general phrasing
  ("all/any Google Cloud products") does not exclude it either.
  Source: https://developers.google.com/profile/help/benefits

- Google Developer Program Plans & Pricing page: lists "$10 GenAI and Cloud monthly
  credit" under the Premium tier, and the comparison table phrase "Start building in
  AI Studio and Vertex AI **or any Google Cloud product**."
  Source: https://developers.google.com/program/plans-and-pricing

**Do not confuse this with a different, narrower credit**: the same benefits FAQ
describes a separate $50/year "GenAI developer" credit that *is* explicitly
restricted — "This credit may only be used in Google AI Studio and Google Cloud
Vertex AI." That restriction applies to the $50/year credit only, not to the
$10/month Cloud credit in question.
Source: https://developers.google.com/profile/help/benefits

**Recent change to flag**: standalone "Google Developer Program Premium"
subscriptions are being consolidated into Google AI Pro / AI Ultra subscriptions in
2026 (legacy standalone subscribers must transition; auto-renewal cutoff dates of
March 30, 2026 for annual and June 30, 2026 for monthly subscribers are stated on the
same FAQ page). This is a change to *how the benefit is delivered/bundled*, not
evidence that the $10/month credit's product scope has narrowed to exclude Compute
Engine — current live wording is general-purpose.
Source: https://developers.google.com/profile/help/benefits

**Practical caveat**: credits require manual activation in most cases and are stated
elsewhere on Google's own support pages not to roll over month to month, so unused
credit in a given month is lost — plan usage accordingly rather than assuming
banking across months.

## 2. GCE Always Free tier for e2-micro

Source (fetched directly, page states "Last updated 2026-08-05 UTC"):
https://docs.cloud.google.com/free/docs/free-cloud-features

- **Qualifying regions**: exactly three US regions — Oregon (`us-west1`), Iowa
  (`us-central1`), South Carolina (`us-east1`). The free allowance is pooled across
  all three combined, not per-region, so only one always-on free instance total is
  covered.
- **Instance-hours**: "free until you have used a number of hours equal to the total
  hours in the current month" — i.e. in a 31-day month that's 744 hours, exactly
  enough for one instance to run continuously all month with no margin for a second
  concurrent instance.
- **Persistent disk**: 30 GB-months of **standard** (HDD) persistent disk free per
  month — not SSD.
- **Network egress**: 1 GB/month of outbound data transfer from North America to all
  destinations, excluding China and Australia.

**Feasibility for the stated workload** (≤2 devices, one small JSON MQTT publish each
roughly every 5 minutes):
- Instance-hours: a single always-on e2-micro in one of the three qualifying regions
  uses ~744 hrs against the ~744-hr free allowance — fits at the boundary for one
  instance.
- Disk: a minimal Linux + MQTT broker (e.g. Mosquitto) install with a 10-20 GB boot
  disk comfortably fits the 30 GB-month allowance.
- Egress: two devices publishing small JSON payloads roughly every 5 minutes is on
  the order of tens of MB/month at most for the MQTT traffic itself — well under the
  1 GB/month free allowance. The exact number depends on implementation details not
  specified in the ticket (TLS overhead, keepalive frequency, QoS/retained messages,
  any dashboard/consumer also pulling data out), so egress is the allowance worth
  monitoring if more consumers are added later, but for the workload as described it
  should stay comfortably inside the limit.

**Conclusion**: a single always-on e2-micro in `us-west1`, `us-central1`, or
`us-east1` should plausibly stay within Always Free limits for this workload,
provided disk stays ≤30 GB standard PD and total egress (MQTT + overhead) stays under
1 GB/month.

## 3. Static external IPv4 address pricing

- **Attached / in-use rate (Standard VM)**: **$0.005/hour.** Confirmed via direct
  fetch of Google's official pricing-change announcement: "Starting February 1, 2024,
  we'll increase the price of in-use external IPv4 addresses that are assigned to
  virtual machine instances (VMs)" — Standard VM rate: previous $0.004/hr → new
  $0.005/hr (Spot/Preemptible VM: previous $0.002/hr → new $0.0025/hr). The page also
  notes existing fixed-price contracts are unaffected until renewal. Effective date
  explicitly stated: **February 1, 2024**, and no newer change was found searching
  from 2026.
  Source: https://cloud.google.com/vpc/pricing-announce-external-ips

- **Reserved-but-unattached rate**: **$0.01/hour (~$7.30/month)**, i.e. roughly double
  the in-use rate, and unaffected by the Feb 2024 change (which explicitly only
  touched the in-use rate). **Caveat**: `cloud.google.com/vpc/network-pricing` and
  `cloud.google.com/compute/pricing` (the pages that would carry this figure directly)
  are JavaScript-rendered and repeatedly returned only page chrome/truncated content
  through the fetch tooling available in this research session — the $0.01/hr figure
  could not be independently re-confirmed against the live rendered table this
  session, though it is the long-standing, widely corroborated published GCP rate and
  is consistent with the "not-in-use addresses are charged at a higher rate" language
  on Google's own IP-addresses documentation. **Recommend a manual spot-check of
  https://cloud.google.com/vpc/network-pricing (External IP address pricing section)
  before finalizing a cost plan that depends on this specific number.**
  Related official doc (confirms the in-use/not-in-use distinction exists, but
  carries no pricing table itself):
  https://docs.cloud.google.com/vpc/docs/ip-addresses

- **Rough monthly cost, attached case**: 744 hrs (31-day month) × $0.005/hr ≈
  **$3.72/month**; 720 hrs (30-day month) × $0.005/hr = $3.60/month. Order of
  magnitude: **~$3.60-$3.75/month** for one static IP attached to an always-on
  standard VM.

## Bottom line for cost planning

- If the AI Pro $10/month credit does cover Compute Engine (plausible given the
  general "all/any Google Cloud products" wording, but not explicitly confirmed for
  Compute Engine by name), it comfortably covers both the e2-micro instance (free
  under Always Free, if usage stays in-quota) and the ~$3.60-3.75/month static IP,
  with headroom to spare.
- Even if the credit turns out **not** to apply to Compute Engine, the e2-micro
  instance itself should still be free under Always Free for this workload, leaving
  only the ~$3.60-3.75/month static IP as an out-of-pocket cost (plus the
  reserved-but-unattached rate of ~$7.30/month applies only if the IP is provisioned
  before/without a running instance attached — keep the IP attached to the running VM
  to get the lower rate, and confirm that $0.01/hr figure directly before relying on
  it for a firm budget).
