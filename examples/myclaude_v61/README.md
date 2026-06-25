# MyClaude_V61 — Optimizer Configs

Ready-to-edit configs for running the Strategy Optimizer against the
`MyClaude_V61` study (Range Filter + RQK + Choppiness + risk/exit engine).

> These files do **not** modify the strategy. The optimizer sets the study's
> inputs *at runtime* by index and runs chart replays — your `.cpp`/DLL is
> never touched.

## How the optimizer treats parameters (read this first)

| `increment` | Behavior |
| --- | --- |
| `> 0` | Parameter is **swept** (`min` → `max` step `increment`). |
| `== 0` | Parameter is **ignored** — the optimizer does **not** set it. You must set it **manually** in the study settings beforehand. |

So `increment: 0` entries are a *checklist for you*, not something the tool applies.

## One-time manual setup (in the MyClaude_V61 study settings)

The optimizer will **not** set these — do it yourself before running:

| Input | idx | Set to | Why |
| --- | --- | --- | --- |
| Enable Order Submission | 35 | **Yes** | Without this, every replay produces 0 trades. |
| ALLOW LIVE | 36 | **No** | Keeps the simulation interlock. |
| FLATTEN NOW | 39 | **No** | At Yes it flattens + resets every bar. |
| *(global)* Trade Simulation Mode | — | **ON** | With this on, orders can **never** reach a live broker. |
| Stop Type / Stop ATR x | 0 / 1 | 0 (ATR) / 2.0 | Held fixed during Pass A. |
| TP1 / TP2 / Trail | 4 / 6 / 8 | 1.0 / 1.5 / 1.5 | Held fixed during Pass A. |
| Order Quantity (Contracts) | 18 | 1 | Fixed sizing → P/L reflects *edge*, not size. |
| Trend Filter MA / Long Only | 40 / 41 | 200 / Yes | Or your preference. |
| Session / Block times | 13,14,20,21 | match instrument | In **chart timezone** — misalignment ⇒ 0 trades. |

## Run steps

1. In Sierra Chart, add **Strategy Optimizer** to the chart that has `MyClaude_V61`
   loaded; select `MyClaude_V61` as the **Target Study**.
2. Turn **Trade Simulation Mode ON** and do the manual setup above.
3. Click **CS6 (Generate)** to confirm the live input **indices** match this file
   (regenerate if your deployed build differs), then point the optimizer's
   **Config File Path** at `StrategyOptimizerConfig.PassA.json`.
4. **Edit `startDate`** to a few hundred bars before your evaluation window
   (warm-up for the 200-bar MA / Range Filter / RQK / ATR).
5. Click **CS7 (Verify Config)** — confirm the three swept inputs read back correctly.
6. Click **CS8 (Start)**. **Confirm the first run produces fills** in
   Trade ▸ Trade Activity Log before trusting the rest.

> **Replay mode:** keep `replayMode: 1` (Standard). The study early-returns on
> `sc.IsFullRecalculation`, so mode 2 (Accurate Back Test) may yield **zero trades**.

## Pass A — signal-edge sweep (this file, 125 combos)

| Param | idx | type | min → max / step |
| --- | --- | --- | --- |
| Range Filter Period | 27 | int | 50 → 150 / 25 |
| Range Filter Multiplier | 28 | float | 2.0 → 4.0 / 0.5 |
| RQK Lookback Window | 29 | float | 4 → 12 / 2 |

## Pass B — risk/exit sweep (run after fixing Pass A's winner, ~144 combos)

Set Pass A's best Range Filter / RQK values **manually** (increment 0), then sweep:

| Param | idx | type | min → max / step |
| --- | --- | --- | --- |
| Stop ATR x | 1 | float | 1.5 → 3.0 / 0.5 |
| TP1 Risk Multiple | 4 | float | 1.0 → 2.0 / 0.5 |
| TP2 Risk Multiple | 6 | float | 1.5 → 3.0 / 0.5 |
| Trail ATR x | 8 | float | 1.0 → 2.0 / 0.5 |

## Reading results

Open the generated results folder in the `visualizer/` Streamlit app. Judge on
**risk-adjusted** metrics (Sharpe, Profit Factor, Max Drawdown) — not Total P/L
alone — and trust **broad plateaus**, not lone peaks. Validate the winner
**out-of-sample** before any live use. 45 inputs = high overfitting risk; stage
the sweeps and keep degrees of freedom low.
