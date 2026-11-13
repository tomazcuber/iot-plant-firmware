#!/usr/bin/env python3
"""Thesis figures for the ADR-0006 DTIM gate (issue #15): one figure per build.

Inputs: the six 2026-11-10 net-smoke Trace runs (2 kHz, 120 s) under --data. The
run list and the reference means come from <data>/dtim-gate/summary.json (its
"file" fields), so the 2026-11-09 supplementary runs with similar names are never
picked up. Each run is a CSV of raw INA219 shunt_raw samples plus a JSON sidecar.
Conversion (ADR-0008) reuses tools/ina219-reader/host/record.py:
current_mA = (shunt_raw - offset_lsb) x 0.01 mV / R_eff, with each run's own
sidecar offset (the session zero, -2.551 LSB) and R_eff (0.10101 ohm). The script
checks the recomputed run means against summary.json (exits non-zero if any is
off by more than 0.005 mA), prints the off-scale peaks for the thesis text, and
writes dtim-gate-min-modem.{pdf,png} and dtim-gate-none.{pdf,png} to --out:
three rows (runs 1-3), left the full 120 s as 100 ms means with the run mean
dashed, right every sample from 60 to 61 s (MIN_MODEM with dotted lines every
102.4 ms anchored on the first rising crossing of 60 mA). Portuguese labels,
decimal comma, no title (the caption sits above the figure in the thesis), sized
for a 16 cm text width. Needs matplotlib and numpy (pip install matplotlib numpy).

    python tools/plots/plot_dtim_gate.py --data C:\\zw\\bench-data --out C:\\zw\\bench-data\\dtim-gate\\thesis
"""
import argparse
import json
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.patheffects as pe  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
from matplotlib.ticker import FuncFormatter, MultipleLocator  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "ina219-reader" / "host"))
from record import shunt_raw_to_ma  # noqa: E402

BUILDS = {
    # key in summary.json: (output stem, trace colour, draw DTIM grid)
    "MIN_MODEM": ("dtim-gate-min-modem", "#2a7ad5", True),
    "NONE": ("dtim-gate-none", "#e8740c", False),
}
BIN_S = 0.1            # left panel: 100 ms means
ZOOM_S = (60.0, 61.0)  # right panel window
DTIM_S = 0.1024        # 1 x 100 TU beacon interval
SPIKE_MA = 60.0        # rising-crossing threshold for the grid anchor
Y_MAX = 130.0
MEAN_TOL_MA = 0.005
FIG_W_IN = 16 / 2.54
FIG_H_IN = 13 / 2.54
MINUS = "\u2212"


def comma(value, decimals):
    return f"{value:.{decimals}f}".replace(".", ",").replace("-", MINUS)


def comma_formatter(decimals):
    return FuncFormatter(lambda x, _pos: comma(x, decimals))


def load_run(data_dir, stem):
    side = json.loads((data_dir / f"{stem}.json").read_text(encoding="utf8"))
    raw = np.loadtxt(data_dir / f"{stem}.csv", delimiter=",", skiprows=1,
                     usecols=(1, 2), dtype=np.int64)
    t_s = raw[:, 0] / 1e6
    ma = shunt_raw_to_ma(raw[:, 1].astype(float), side["r_eff_ohm"], side["offset_lsb"])
    return side, t_s, ma


def bin_means(t_s, ma):
    idx = np.floor(t_s / BIN_S).astype(np.int64)
    counts = np.bincount(idx)
    sums = np.bincount(idx, weights=ma)
    keep = counts > 0
    centres = (np.nonzero(keep)[0] + 0.5) * BIN_S
    return centres, sums[keep] / counts[keep]


def dtim_anchor(t_rel, ma):
    """Time of the first rising crossing of SPIKE_MA in the window, else None."""
    above = ma > SPIKE_MA
    rising = np.nonzero(above[1:] & ~above[:-1])[0]
    return t_rel[rising[0] + 1] if rising.size else None


def style_axes(ax):
    ax.set_ylim(0, Y_MAX)
    ax.yaxis.set_major_locator(MultipleLocator(50))
    ax.yaxis.set_major_formatter(comma_formatter(0))
    ax.grid(axis="y", color="0.88", linewidth=0.5)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)


def plot_build(key, runs, colour, dtim_grid, out_dir, stem):
    fig, axes = plt.subplots(len(runs), 2, figsize=(FIG_W_IN, FIG_H_IN), sharey=True,
                             gridspec_kw={"width_ratios": [2.3, 1]}, layout="constrained")
    fig.get_layout_engine().set(wspace=0.06)
    # A thin light edge keeps the black dashes readable over the trace (also in
    # grayscale) without blanking the trace around the mean.
    halo = [pe.withStroke(linewidth=1.6, foreground="white", alpha=0.8)]
    for row, run in enumerate(runs):
        ax_l, ax_r = axes[row]
        t_s, ma, mean = run["t_s"], run["ma"], run["mean"]

        centres, means = bin_means(t_s, ma)
        ax_l.plot(centres, means, color=colour, linewidth=0.7)
        ax_l.axhline(mean, color="black", linewidth=0.9, linestyle=(0, (5, 2.5)), path_effects=halo)
        ax_l.set_xlim(0, 120)
        ax_l.xaxis.set_major_locator(MultipleLocator(20))
        ax_l.xaxis.set_major_formatter(comma_formatter(0))
        ax_l.set_ylabel("Corrente (mA)")
        ax_l.set_title(f"Corrida {row + 1} · média {comma(mean, 2)} mA · "
                       f"RSSI {comma(run['rssi'], 0)} dBm", loc="left", fontsize=9)

        win = (t_s >= ZOOM_S[0]) & (t_s < ZOOM_S[1])
        t_rel, ma_win = t_s[win] - ZOOM_S[0], ma[win]
        if dtim_grid:
            anchor = dtim_anchor(t_rel, ma_win)
            if anchor is not None:
                k = np.arange(np.ceil(-anchor / DTIM_S), np.floor((1 - anchor) / DTIM_S) + 1)
                for x in anchor + k * DTIM_S:
                    ax_r.axvline(x, color="0.45", linewidth=0.6, linestyle=(0, (1, 1.5)))
        ax_r.plot(t_rel, ma_win, color=colour, linewidth=0.6)
        ax_r.set_xlim(0, 1)
        ax_r.xaxis.set_major_locator(MultipleLocator(0.2))
        ax_r.xaxis.set_major_formatter(comma_formatter(1))

        for ax in (ax_l, ax_r):
            style_axes(ax)
            if row < len(runs) - 1:
                ax.tick_params(labelbottom=False)
        ax_r.tick_params(labelleft=False)

    axes[-1, 0].set_xlabel("Tempo (s), médias de 100 ms")
    axes[-1, 1].set_xlabel("Tempo a partir de 60 s (s),\namostras de 0,5 ms")
    out_dir.mkdir(parents=True, exist_ok=True)
    for ext in ("pdf", "png"):
        fig.savefig(out_dir / f"{stem}.{ext}", dpi=300)
    plt.close(fig)
    return [out_dir / f"{stem}.{ext}" for ext in ("pdf", "png")]


def report_peaks(key, runs):
    print(f"\n{key}: off-scale check (y axis capped at {Y_MAX:.0f} mA)")
    print("  run  max sample (mA) @ t (s)   max 100 ms mean (mA)   max in 60-61 s (mA)   samples > cap")
    for i, run in enumerate(runs, 1):
        t_s, ma = run["t_s"], run["ma"]
        j = int(np.argmax(ma))
        _, means = bin_means(t_s, ma)
        win = (t_s >= ZOOM_S[0]) & (t_s < ZOOM_S[1])
        print(f"  r{i}   {ma[j]:15.1f} @ {t_s[j]:6.2f}   {means.max():20.1f}   "
              f"{ma[win].max():19.1f}   {int((ma > Y_MAX).sum()):13d}")
    peak = max(float(run["ma"].max()) for run in runs)
    print(f"  build peak: {comma(peak, 1)} mA")


def main(argv=None):
    p = argparse.ArgumentParser(description="Regenerate the DTIM-gate thesis figures (#15).")
    p.add_argument("--data", type=Path, default=Path(r"C:\zw\bench-data"))
    p.add_argument("--out", type=Path, default=Path(r"C:\zw\bench-data\dtim-gate\thesis"))
    args = p.parse_args(argv)

    plt.rcParams.update({
        "font.family": "serif", "font.serif": ["Times New Roman", "Times", "DejaVu Serif"],
        "font.size": 9, "axes.titlesize": 9, "axes.labelsize": 9,
        "xtick.labelsize": 8.5, "ytick.labelsize": 8.5,
        "pdf.fonttype": 42, "axes.unicode_minus": True,
    })
    summary = json.loads((args.data / "dtim-gate" / "summary.json").read_text(encoding="utf8"))

    failed = False
    print("run                                      reference (mA)  recomputed (mA)  diff")
    outputs = []
    for key, (stem, colour, dtim_grid) in BUILDS.items():
        runs = []
        for ref in summary[key]:
            side, t_s, ma = load_run(args.data, ref["file"])
            mean = float(ma.mean())
            diff = mean - ref["mean"]
            failed |= abs(diff) > MEAN_TOL_MA
            print(f"{ref['file']:40s} {ref['mean']:14.2f}  {mean:15.2f}  {diff:+.4f}")
            runs.append({"t_s": t_s, "ma": ma, "mean": mean, "rssi": side["rssi"]})
        report_peaks(key, runs)
        outputs += plot_build(key, runs, colour, dtim_grid, args.out, stem)
        print()

    for path in outputs:
        print(f"wrote {path}")
    if failed:
        print(f"ERROR: a recomputed mean differs from summary.json by more than {MEAN_TOL_MA} mA",
              file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
