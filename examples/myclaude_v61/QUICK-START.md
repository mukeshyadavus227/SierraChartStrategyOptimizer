# Quick Start — Do This First

A plain-English checklist for test-driving your MyClaude strategy settings.
**No real money is involved** — this only runs pretend trades over past data.

> Do these on your trading PC, in Sierra Chart.

## 1. Turn on pretend-trading mode
Top menu: **Trade → Trade Simulation Mode On.**
This is your safety switch — it makes sure nothing can ever place a real order.

## 2. Add the tester to your chart
Open the chart that has your **MyClaude_V61** strategy on it.
Add the **Strategy Optimizer** study, and choose **MyClaude_V61** as the study to test.

## 3. Flip three switches in the strategy settings
- **Enable Order Submission** → **Yes**  (lets it place pretend trades)
- **ALLOW LIVE** → **No**  (extra safety — keeps it pretend)
- **FLATTEN NOW** → **No**

## 4. Run the first plan
Point the Optimizer's **Config File Path** at **`StrategyOptimizerConfig.PassA.json`**.
Set the **start date** back a few months (gives the strategy time to "warm up").
Press **Start**, and watch a few pretend trades appear — that confirms it's working.

## 5. Use the results, then repeat
When it finishes, a results folder opens. Find the **best-scoring** settings,
type those values into the strategy, then run the next plan the same way:
**PassA → PassB → PassC.**

---

### If you see NO trades
Two usual causes: (a) **Trade Simulation Mode** isn't on, or **Enable Order
Submission** isn't **Yes**; or (b) the start date / trading hours don't line up with
your market. Fix those and run one plan again.

### What each plan is for
- **PassA** — best entry-signal settings
- **PassB** — best stop-loss / profit-target settings
- **PassC** — checks whether each filter is actually helping

Full details are in [`README.md`](README.md). Your strategy itself is never changed —
these plans only adjust settings while testing.
