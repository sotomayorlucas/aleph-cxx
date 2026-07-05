#!/usr/bin/env python3
"""Paper figures from bench/aleph_bench_scaling CSV.

Usage: plot_scaling.py [csv_path] [out_dir] [prefix]
Defaults: docs/paper/data/scaling.csv -> docs/paper/figs/ with prefix "fig"
(mesh family: plot_scaling.py data/scaling_mesh.csv figs mesh)

Design notes (dataviz method): categorical slots in fixed order; every series
direct-labeled (relief rule); one axis per chart; thin 1.8pt lines; recessive
grid; text in ink tokens. The three interior edits (add1/add2/delete) differ
by <5% in cost, so figures collapse them into one "interior" series (median)
against the corner add — collapsing is stated in the caption, not hidden.
"""
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

# Reference palette (validated: scripts/validate_palette.js, light mode).
SLOT = ["#2a78d6", "#1baf7a", "#eda100", "#008300", "#4a3aa7"]
INK, INK2, SURFACE = "#0b0b0b", "#52514e", "#fcfcfb"
MARKERS = ["o", "s", "^", "D", "v"]

plt.rcParams.update({
    "figure.facecolor": SURFACE, "axes.facecolor": SURFACE,
    "savefig.facecolor": SURFACE,
    "text.color": INK, "axes.labelcolor": INK,
    "xtick.color": INK2, "ytick.color": INK2,
    "axes.edgecolor": INK2, "axes.linewidth": 0.6,
    "grid.color": "#e4e3df", "grid.linewidth": 0.5,
    "font.size": 9, "axes.titlesize": 10, "legend.fontsize": 8,
    "lines.linewidth": 1.8, "lines.markersize": 5.5,
})

INTERIOR = ["add1", "add2", "delete"]


def style(ax, xlabel, ylabel, title):
    ax.grid(True, which="major", zorder=0)
    ax.set_axisbelow(True)
    ax.spines[["top", "right"]].set_visible(False)
    ax.set_xlabel(xlabel)
    ax.set_ylabel(ylabel)
    ax.set_title(title, loc="left", fontweight="bold", color=INK)


def label_end(ax, x, y, text, color, dy=0.0):
    ax.annotate(text, (x, y), xytext=(6, dy), textcoords="offset points",
                fontsize=8, color=color, fontweight="bold", va="center")


def main():
    csv = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parents[1] / "data/scaling.csv"
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else Path(__file__).parents[1] / "figs"
    pre = sys.argv[3] if len(sys.argv) > 3 else "fig"
    out.mkdir(parents=True, exist_ok=True)
    df = pd.read_csv(csv)
    init = df[df.edit == "initial"]
    interior = (df[df.edit.isin(INTERIOR)]
                .groupby("grid", as_index=False)
                .agg(edges=("edges", "median"), t_full_ms=("t_full_ms", "median"),
                     t_local_ms=("t_local_ms", "median"),
                     dirty_frac=("dirty_frac", "median")))
    corner = df[df.edit == "add_corner"]

    # Fig A — per-edit operator update cost vs |E| (log-log).
    fig, ax = plt.subplots(figsize=(4.6, 3.2), layout="constrained")
    ax.loglog(interior.edges, interior.t_full_ms, color=SLOT[0],
              marker=MARKERS[0], zorder=3)
    label_end(ax, interior.edges.iloc[-1], interior.t_full_ms.iloc[-1],
              "full rebuild", SLOT[0])
    ax.loglog(interior.edges, interior.t_local_ms, color=SLOT[1],
              marker=MARKERS[1], zorder=3)
    label_end(ax, interior.edges.iloc[-1], interior.t_local_ms.iloc[-1],
              "local · interior\n(dirty 37–51)", SLOT[1], dy=8)
    ax.loglog(corner.edges, corner.t_local_ms, color=SLOT[2],
              marker=MARKERS[2], zorder=3)
    label_end(ax, corner.edges.iloc[-1], corner.t_local_ms.iloc[-1],
              "local · corner\n(dirty 13)", SLOT[2], dy=-12)
    ax.set_xlim(right=ax.get_xlim()[1] * 6)
    style(ax, "|E| (skeleton edges)", "median ms per edit",
          "Per-edit rebuild cost (log–log)")
    fig.savefig(out / f"{pre}_a_local_vs_full.pdf")
    fig.savefig(out / f"{pre}_a_local_vs_full.png", dpi=200)

    # Fig B — dirty/|E| gate predictor decay (log-x).
    fig, ax = plt.subplots(figsize=(4.6, 3.0), layout="constrained")
    ax.semilogx(interior.edges, interior.dirty_frac, color=SLOT[0],
                marker=MARKERS[0], zorder=3)
    label_end(ax, interior.edges.iloc[-1], interior.dirty_frac.iloc[-1],
              "interior edits", SLOT[0], dy=8)
    ax.semilogx(corner.edges, corner.dirty_frac, color=SLOT[1],
                marker=MARKERS[1], zorder=3)
    label_end(ax, corner.edges.iloc[-1], corner.dirty_frac.iloc[-1],
              "corner add", SLOT[1], dy=-4)
    ax.axhline(0.5, color=INK2, linestyle="--", linewidth=0.8)
    ax.annotate("fallback gate (0.5)", (ax.get_xlim()[0] * 1.1, 0.5),
                xytext=(4, 4), textcoords="offset points", fontsize=8, color=INK2)
    ax.set_ylim(0, 0.55)
    ax.set_xlim(right=ax.get_xlim()[1] * 5)
    style(ax, "|E| (skeleton edges)", "dirty / |E|",
          "Dirty-cover fraction per edit")
    fig.savefig(out / f"{pre}_b_dirty_fraction.pdf")
    fig.savefig(out / f"{pre}_b_dirty_fraction.png", dpi=200)

    # Fig C — global-support formulation blow-up vs bounded build (log-log).
    fig, ax = plt.subplots(figsize=(4.6, 3.0), layout="constrained")
    g = init.dropna(subset=["t_global_ms"])
    ax.loglog(init.nodes, init.t_full_ms, color=SLOT[0], marker=MARKERS[0], zorder=3)
    label_end(ax, init.nodes.iloc[-1], init.t_full_ms.iloc[-1], "bounded κ_R", SLOT[0])
    if len(g) >= 2:
        ax.loglog(g.nodes, g.t_global_ms, color=SLOT[1], marker=MARKERS[1], zorder=3)
        label_end(ax, g.nodes.iloc[-1], g.t_global_ms.iloc[-1],
                  "global support\n(stops: >5 min)", SLOT[1])
    ax.set_xlim(right=ax.get_xlim()[1] * 3)
    style(ax, "|V| (nodes)", "full build, ms (log)",
          "Global-support W₁ blow-up")
    fig.savefig(out / f"{pre}_c_global_blowup.pdf")
    fig.savefig(out / f"{pre}_c_global_blowup.png", dpi=200)

    # Fig D — MV certificate cost vs nodes (single series; no legend needed).
    cert = df.dropna(subset=["t_cert_ms"])
    if len(cert):
        fig, ax = plt.subplots(figsize=(4.6, 3.0), layout="constrained")
        c = cert.groupby("nodes", as_index=False).t_cert_ms.median()
        ax.plot(c.nodes, c.t_cert_ms, color=SLOT[0], marker=MARKERS[0], zorder=3)
        style(ax, "|V| (nodes)", "certificate, ms",
              "Mayer–Vietoris certificate cost")
        fig.savefig(out / f"{pre}_d_cert_cost.pdf")
        fig.savefig(out / f"{pre}_d_cert_cost.png", dpi=200)

    print(f"figures -> {out}")


if __name__ == "__main__":
    main()
