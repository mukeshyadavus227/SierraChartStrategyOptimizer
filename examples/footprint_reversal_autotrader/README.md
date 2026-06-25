# Footprint Reversal AutoTrader — SCAFFOLD (sim only)

⚠️ **This is an untested starting point, not a finished or proven strategy.**
Automating a signal does **not** give it an edge. Compile it, run it **only with
Trade Simulation Mode ON**, then validate out-of-sample before believing anything.

## What it is
An automated-trading wrapper around the **FootprintReversalSystem** signal by
Patrick Senas ([FutTrader](https://github.com/FutTrader/footprint-system), MIT)
— credit also to Adam @ JumpstartTrading. The 4-condition reversal signal
(relative volume, finished auction, diagonal imbalance, POC location) is reused
unchanged. This project only **adds the trade-management layer** the original
omits.

## What was added vs the original
1. **Repaint fix** — trades only on the **last closed bar's** final footprint
   (the original alerts on the still-forming bar, which can repaint).
2. **Entry** — market `BuyEntry`/`SellEntry` with a native server-side OCO bracket.
3. **Exit (model 'a')** — stop just beyond the reversal bar's extreme, target at
   `RewardRisk × risk`, plus an end-of-day flatten. `TODO` markers in the source
   show where to switch to opposite-signal or time-based exits later.
4. **Safety interlock** — orders route only if *Enable Order Submission* = Yes
   **and** (*Trade Simulation Mode* ON **or** *Allow Live* = Yes). With sim mode
   on, it can never hit a live broker.
5. `MaintainTradeStatisticsAndTradesData = 1` so the optimizer can read results.

## Dependency
Needs a **Number Bars Calculated** study on the same chart; select its study ID
and **SG14 (VolBarDiff)** in the inputs. Designed for **range-bar** footprint charts.

## How to use
1. Compile: **Analysis → Build Custom Studies DLL → add `FootprintReversalAutoTrader.cpp` → Build.**
2. Add it to a range-bar footprint chart that also has *Number Bars Calculated*;
   wire up inputs **3** (study) and **4** (SG14).
3. Turn **Trade Simulation Mode ON**, set *Enable Order Submission* = Yes,
   *Allow Live* = No.
4. Optimize with `StrategyOptimizerConfig.json` (sweeps imbalance %, PB ticks, R:R).

## Optimizer sweep (36 combos)
| Param | idx | type | min→max/step |
| --- | --- | --- | --- |
| Buy/Sell Imbalance % Threshold | 6 | int | 300→500 / 100 |
| Ticks from High/Low for PB Reversal | 9 | int | 2→6 / 2 |
| Reward:Risk Multiple | 14 | float | 1.5→3.0 / 0.5 |

Held manually (`increment 0`, optimizer ignores): interlocks (10/11/18), order qty
(12), stop buffer (13), and the NumberBars2 study refs (3/4).

## ⚠️ Footprint backtest caveat
The study early-returns if volume-at-price data isn't present, so a replay needs
**intraday tick data** loaded and a tick-by-tick replay. If a run produces **zero
trades**, that — or the missing NumberBars2 dependency — is almost always why.
Verify the first run produces footprint data and trades before trusting the grid.

## License
Derived work under the **MIT License**, preserving the original FutTrader
copyright/attribution. See the header in `FootprintReversalAutoTrader.cpp`.
