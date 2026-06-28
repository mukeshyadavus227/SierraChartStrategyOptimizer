# Back testing & optimizing the OTF State study

This document explains how the uploaded `OTFStateFilter.cpp` was made
back-testable and optimizable, what was added, and how to actually run the
optimization — both inside Sierra Chart (tick-accurate) and offline with a
portable C++ tool.

## TL;DR

* `OTFStateFilter.cpp` is a **pure visual filter**. It outputs a state
  subgraph (`+1 / 0 / -1`) and colors the chart background, but it **never
  places a trade**.
* The Strategy Optimizer in this repo measures performance from Sierra Chart's
  **trade-simulation statistics** (`GetTradeStatisticsForSymbolV2`,
  `GetTradeListEntry`). A study that places no trades produces **empty
  results**, and the filter's single input (`Source Chart Number`) is **not a
  strategy parameter**. So the filter, as deployed, is neither back-testable
  nor optimizable.
* **`OTFStateStrategy.cpp`** (new) keeps the exact OTF state machine and adds
  trade execution plus tunable inputs so it can be back tested and optimized.
* **`StrategyOptimizerConfig.OTFStateStrategy.example.json`** (new) is a
  ready-to-use optimizer configuration that sweeps the meaningful parameters.
* **`tools/otf_backtest.cpp`** (new) is a standalone back tester/optimizer that
  runs offline on exported OHLC CSV — no Sierra Chart required.

## Files added

| File | Purpose |
| --- | --- |
| `OTFStateFilter.cpp` | The original deployed filter, preserved unchanged. |
| `OTFStateStrategy.cpp` | Trade-enabled, parameterized strategy for back testing/optimization. |
| `StrategyOptimizerConfig.OTFStateStrategy.example.json` | Optimizer config targeting the strategy. |
| `tools/otf_backtest.cpp` | Portable offline back tester + parameter sweep. |
| `tools/make_sample_data.py` | Synthetic OHLC generator for demoing the tool. |

## What the strategy adds (vs. the filter)

The state machine is **identical**, except the entry run length is now a
parameter instead of being hard-coded to 2:

* **Up entry**: `N` consecutive strict Higher Lows — `L[idx-N] < ... < L[idx]`.
* **Up exit**: current Low breaks `LSHL` (running max of lows since entry).
* **Down entry**: `N` consecutive strict Lower Highs — `H[idx-N] > ... > H[idx]`.
* **Down exit**: current High breaks `LSLH` (running min of highs since entry).

With `N = 2` the strategy reproduces the original filter's signals exactly.

Trading rules:

* state → `+1` : enter / reverse to **long** (if long enabled)
* state → `-1` : enter / reverse to **short** (if short enabled)
* state → `0`  : **flatten**
* A directional state whose side is disabled also flattens, so long-only mode
  exits longs when a down-trend starts without going short.

### Inputs (index order matches the config JSON)

| Idx | Input | Default | Notes |
| --- | --- | --- | --- |
| 0 | Source Chart (0 = this chart) | 0 | Native or cross-chart data, same as the filter. |
| 1 | Consecutive HL/LH Count (`N`) | 2 | **Primary optimization target.** `2` = original filter. |
| 2 | Visualize Signals Only (No Trades) | No | Set **Yes** to behave exactly like `OTFStateFilter`. |
| 3 | Enable Long Trades | Yes | |
| 4 | Enable Short Trades | Yes | |
| 5 | Order Quantity | 1 | |
| 6 | Target in Ticks (0 = none) | 0 | Optional fixed profit target (attached order). |
| 7 | Stop in Ticks (0 = none) | 0 | Optional fixed protective stop (attached order). |
| 8 | Trade On Bar Close Only | Yes | Avoids intrabar churn; acts on completed bars. |

The new study sets the trade-simulation flags it needs in `SetDefaults`
(`MaintainTradeStatisticsAndTradesData`, `SupportReversals`,
`SupportAttachedOrdersForTrading`, etc.).

## A. Back test/optimize inside Sierra Chart (recommended, tick-accurate)

1. **Compile `OTFStateStrategy.cpp` as its own DLL** (separately from the
   optimizer — each DLL needs a single `SCDLLName`). From this folder:

   ```bat
   cl /LD /EHa /MT /std:c++17 /O2 OTFStateStrategy.cpp /link /DLL ^
      /OUT:"C:\SierraChart\Data\OTFStateStrategy.dll"
   ```

   The file includes `"../sierrachart.h"` (it lives in a sub-folder of
   `ACS_Source`, like the rest of this repo). If you place it directly in
   `ACS_Source`, change the include to `"sierrachart.h"`.

2. **Add the study to your trading chart** and confirm its settings — in
   particular **Visualize Signals Only = No**, and long/short enabled as
   desired. (The optimizer does not change inputs whose `increment` is `0`, so
   these fixed inputs must already be correct on the chart.)

3. **Turn on Trade Simulation Mode** in Sierra Chart (the optimizer refuses to
   start otherwise).

4. **Copy `StrategyOptimizerConfig.OTFStateStrategy.example.json`** to your
   `Data` folder (or wherever your `Config File Path` points), and **edit
   `replayConfig.startDate`** to a date your chart actually has data for.

5. **Run the optimizer** per the main `README.md`: select `OTFStateStrategy`
   as the **Target Study**, point **Config File Path** at the JSON, click
   **Verify Config** (CS7) to sanity-check, then **Start** (CS8).

6. **Read the results**: a timestamped folder under `results/` with a
   `...-summary.csv` ranked by Total P/L, plus per-combination JSON/CSV.

The example config sweeps **5 × 3 × 2 = 30** combinations:
`Consecutive HL/LH Count` 2→6, `Target` {0,20,40} ticks, `Stop` {20,40} ticks.
Adjust the `min/max/increment` fields to taste. Set a parameter's `increment`
to `0` to hold it fixed.

## B. Offline back test/optimize (no Sierra Chart needed)

Use this for fast parameter exploration before the full replay. It replicates
the strategy's **native** state machine and a next-bar-open fill model with
optional intrabar target/stop.

```bash
# build
g++ -O2 -std=c++17 -o otf_backtest tools/otf_backtest.cpp

# (optional) make synthetic demo data — see warning below
python3 tools/make_sample_data.py --bars 6000 --out tools/sample_data.csv

# sweep the entry run length and target/stop grid
./otf_backtest --csv tools/sample_data.csv \
    --count-min 2 --count-max 8 \
    --targets 0,20,40 --stops 20,40,60 \
    --tick 0.25 --point-value 50 --commission 4.0 \
    --out tools/results.csv
```

Provide real data by exporting it from Sierra Chart
(*Chart → Export Chart Data* → CSV). Columns are detected from the header by
name (`Open/High/Low/Close`), so Sierra Chart's
`Date,Time,Open,High,Low,Close,Volume,...` export works directly. Set `--tick`
and `--point-value` to your instrument (e.g. ES: tick `0.25`, point value `50`).

Output columns: `NetP/L, Trades, Win%, PF (profit factor), MaxDD, AvgWin,
AvgLoss`, ranked by net P/L.

> ⚠️ **`make_sample_data.py` produces random synthetic data.** It only exists to
> exercise the tool. Numbers from it say nothing about real performance —
> always evaluate on real exported market data.

### Why optimization matters here

On a 6,000-bar synthetic demo run (ES-like, tick `0.25`, point value `50`,
`$4` commission), the original hard-coded `count = 2` was the **worst**
setting — it over-traded (1,165 trades) — while `count = 4` ranked best. The
point isn't the specific numbers (the data is synthetic); it's that the
filter's fixed `2` is just one point in a parameter space worth searching.

## Caveats

* The actual Sierra Chart back test runs via chart replay **inside Sierra
  Chart on Windows** — it can't be executed from this repo/environment. The
  offline tool is an approximation (bar-based fills, simplified stop/target
  precedence) and will not match Sierra Chart's tick-by-tick simulation
  exactly. Use it to narrow the search, then confirm with method A.
* Back-test results are not predictive of live performance. Optimizing over a
  single period risks curve-fitting; validate on out-of-sample data.
