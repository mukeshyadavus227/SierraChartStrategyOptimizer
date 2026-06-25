# Quick Start — Mechanics QA (Do This First)

A plain-English checklist for **QA-testing the mechanics** of your MyClaude strategy
in the Strategy Optimizer — confirming it *fires entries, manages exits, and applies
filters correctly*. This is **not** a procedure for finding profitable settings to trade.

> ⚠️ **Read before you start.**
> - MyClaude V6.1 has **no validated edge** (walk-forward OOS *t* = −3.69,
>   gross-negative on every timeframe). The optimizer *will* hand you a "best-scoring"
>   config — that is **selection bias over a short window, not an edge.** Searching
>   ~285 configs and keeping the max makes a true-zero strategy look ~3σ profitable by
>   chance alone.
> - **Optimizer winners are for mechanics QA only. Never type them into the live-armed
>   MyClaude study (real account 23234705).** That study keeps its frozen parameters;
>   nothing in this folder produces a tradeable configuration.
> - "No real money is involved" is true **only inside this Trade-Simulation workflow.**
>   The separately deployed MyClaude DLL is armed live on a real account — these steps
>   do not touch that.

> Do these on your trading PC, in Sierra Chart.

## 1. Turn on pretend-trading mode
Top menu: **Trade → Trade Simulation Mode On.**
This is your safety switch for *this workflow* — it makes sure the optimizer run can't
place a real order. (It does **not** disarm the separately deployed live study.)

## 2. Add the tester to your chart
Open the chart that has your **MyClaude_V61** strategy on it.
Add the **Strategy Optimizer** study, and choose **MyClaude_V61** as the study to test.

## 3. Flip three switches in the strategy settings
- **Enable Order Submission** → **Yes**  (lets it place pretend trades)
- **ALLOW LIVE** → **No**  (extra safety — keeps it pretend)
- **FLATTEN NOW** → **No**

## 4. Set the SEARCH window, then run the first plan
The optimizer only has a **start** date — the replay always runs to the **last bar
loaded on the chart**, so the *end* of your window is a **chart setting, not a config
field.**
- In the strategy settings, set the **chart's data range to END on 2026-06-01** (this
  becomes your search/holdout boundary — see Step 6).
- Point the Optimizer's **Config File Path** at **`StrategyOptimizerConfig.PassA.json`**
  (its `startDate` is **2026-05-01**, a few hundred bars of warm-up before trading).
- Press **Start**, and confirm a few pretend trades appear — that's the QA pass:
  entries fire.

## 5. Run the plans in order — to check behavior, not to harvest winners
**PassA → PassB → PassC**, each on the **search window only**:
- **PassA** — confirm entry signals fire across the Range-Filter / RQK grid
- **PassB** — confirm stops, TP1/TP2, and the trail actually trigger
- **PassC** — confirm each filter toggle changes behavior (trade count moves)

You are verifying mechanics. You are **not** selecting numbers to trade.

## 6. If you read the scores as performance — the one-look holdout rule
The scores are noise over a few weeks, but if you want a robustness sanity-check, use a
strict out-of-sample holdout — **never judge a config on the data you searched:**

1. **Search** on **2026-05-01 → 2026-06-01** (Steps 4–5, chart END = 2026-06-01). Pick
   **at most one** candidate per pass, on **risk-adjusted** metrics (Sharpe / PF /
   MaxDD), and only if results show a **broad plateau, not a lone peak.**
2. **Lock** that one final config in the study, then **extend the chart's data range
   END to today** and run **`StrategyOptimizerConfig.Holdout.json`** once (it replays
   **2026-06-01 → now**).
3. **One look. No peeking, no re-tuning to the holdout.** If the holdout P/L flips sign
   or the Sharpe/PF collapses, **discard the config** — that's overfit.

> Honesty caveat: even passing this holdout does **not** certify an edge. The holdout
> is only ~3 weeks and a handful of trades — it catches gross overfit, it does not
> validate a strategy. Certification needs years of OOS, not weeks.

---

### If you see NO trades
Two usual causes: (a) **Trade Simulation Mode** isn't on, or **Enable Order
Submission** isn't **Yes**; or (b) the start date / trading hours don't line up with
your market. Fix those and run one plan again.

### What each plan checks
- **PassA** — entry-signal mechanics across the Range-Filter / RQK grid (125 combos)
- **PassB** — stop-loss / TP / trail mechanics (144 combos)
- **PassC** — that each filter toggle measurably changes behavior (16 combos)

Full details — and the *why* behind the no-edge / no-harvest rules — are in
[`README.md`](README.md). Your strategy code is never changed; these plans only adjust
settings while testing.
