// =============================================================================
// otf_backtest.cpp
// Portable, standalone back tester + parameter optimizer for the OTF State
// strategy. Requires NO Sierra Chart headers — it compiles with any C++17
// compiler and runs on exported OHLC bar data (CSV).
//
// It replicates the OTFStateStrategy native state machine and trade rules so
// you can quickly explore parameters offline before running the full,
// tick-accurate back test inside Sierra Chart via the Strategy Optimizer.
//
// BUILD
//   g++ -O2 -std=c++17 -o otf_backtest tools/otf_backtest.cpp
//   (or)  cl /O2 /std:c++17 /EHsc tools\otf_backtest.cpp
//
// DATA
//   A CSV with Open/High/Low/Close columns. Column positions are detected from
//   the header by name (works with Sierra Chart "Export Chart Data" files:
//   Date,Time,Open,High,Low,Close,Volume,...). If there is no header, the tool
//   assumes columns: datetime,open,high,low,close[,volume].
//
// USAGE
//   ./otf_backtest --csv data.csv
//   ./otf_backtest --csv data.csv --count-min 2 --count-max 8
//                  --targets 0,20,40 --stops 20,40,60
//                  --tick 0.25 --point-value 50 --commission 4.0
//                  --out results.csv
//
//   --count-min/--count-max  range of "Consecutive HL/LH Count" to sweep
//   --targets / --stops      comma lists of tick offsets (0 = disabled)
//   --tick                   instrument tick size (price units)
//   --point-value            account currency per 1.0 of price movement
//   --commission             commission per round-turn trade (currency)
//   --long / --short         1/0 enable each side (default both on)
//   --out                    write the ranked results table to this CSV
// =============================================================================

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

struct Bars
{
    std::vector<double> open, high, low, close;
    size_t size() const { return close.size(); }
};

struct Params
{
    int    count       = 2;
    int    targetTicks = 0;
    int    stopTicks   = 0;
    bool   enableLong  = true;
    bool   enableShort = true;
    double tick        = 0.25;
    double pointValue  = 1.0;
    double commission  = 0.0;
};

struct Metrics
{
    int    count = 0, targetTicks = 0, stopTicks = 0;
    int    totalTrades = 0, wins = 0, losses = 0;
    double netPL = 0.0, grossProfit = 0.0, grossLoss = 0.0;
    double maxDrawdown = 0.0;
    double avgWin = 0.0, avgLoss = 0.0;
    double profitFactor() const
    {
        if (grossLoss <= 0.0) return grossProfit > 0.0 ? std::numeric_limits<double>::infinity() : 0.0;
        return grossProfit / grossLoss;
    }
    double winRate() const { return totalTrades ? 100.0 * wins / totalTrades : 0.0; }
};

// ---- OTF native state machine (matches OTFStateStrategy "Source Chart = 0") --
static bool strictHigherLows(const std::vector<double>& L, int i, int n)
{
    for (int k = n; k > 0; --k)
        if (!(L[i - k] < L[i - k + 1])) return false;
    return true;
}
static bool strictLowerHighs(const std::vector<double>& H, int i, int n)
{
    for (int k = n; k > 0; --k)
        if (!(H[i - k] > H[i - k + 1])) return false;
    return true;
}

static std::vector<int> computeStates(const Bars& b, int count)
{
    const int n = (int)b.size();
    std::vector<int> st(n, 0);
    int    state = 0;
    double LSHL = 0.0, LSLH = 0.0;
    for (int i = 0; i < n; ++i)
    {
        if (i < count) { st[i] = 0; continue; }

        if (state == 1)
        {
            LSHL = std::max(LSHL, b.low[i - 1]);
            if (LSHL > b.low[i]) { state = 0; LSHL = 0.0; }
        }
        else if (state == -1)
        {
            LSLH = std::min(LSLH, b.high[i - 1]);
            if (LSLH < b.high[i]) { state = 0; LSLH = 0.0; }
        }

        // entry check runs on the same bar as an exit (native behavior)
        if (state == 0)
        {
            if      (strictHigherLows(b.low,  i, count)) { state =  1; LSHL = b.low[i]; }
            else if (strictLowerHighs(b.high, i, count)) { state = -1; LSLH = b.high[i]; }
        }
        st[i] = state;
    }
    return st;
}

// ---- Trade simulation -------------------------------------------------------
// Signals act on the next bar's open (no look-ahead). Optional fixed Target/Stop
// in ticks rest as protective orders and are checked intrabar (stop assumed to
// fill before target when a bar spans both).
static Metrics backtest(const Bars& b, const Params& p)
{
    Metrics m;
    m.count = p.count; m.targetTicks = p.targetTicks; m.stopTicks = p.stopTicks;

    const std::vector<int> state = computeStates(b, p.count);
    const int n = (int)b.size();

    int    pos = 0;            // -1 / 0 / +1
    double entry = 0.0;
    int    pendingDesired = 0; // position requested at previous bar's close
    bool   havePending = false;

    double equity = 0.0, peak = 0.0;
    std::vector<double> tradePL;

    auto closeTrade = [&](double exitPrice)
    {
        double pts = (pos > 0) ? (exitPrice - entry) : (entry - exitPrice);
        double pl  = pts * p.pointValue - p.commission;
        tradePL.push_back(pl);
        equity += pl;
        peak = std::max(peak, equity);
        m.maxDrawdown = std::max(m.maxDrawdown, peak - equity);
        pos = 0;
    };

    for (int i = 0; i < n; ++i)
    {
        // (a) fill the order decided on the previous bar, at this bar's open
        if (havePending && pendingDesired != pos)
        {
            double fill = b.open[i];
            if (pos != 0) closeTrade(fill);
            if (pendingDesired != 0) { pos = pendingDesired; entry = fill; }
        }
        havePending = false;

        // (b) protective target/stop intrabar
        if (pos != 0)
        {
            if (pos > 0)
            {
                double stopPx = entry - p.stopTicks   * p.tick;
                double tgtPx  = entry + p.targetTicks * p.tick;
                if (p.stopTicks > 0 && b.low[i] <= stopPx)        closeTrade(stopPx);
                else if (p.targetTicks > 0 && b.high[i] >= tgtPx) closeTrade(tgtPx);
            }
            else
            {
                double stopPx = entry + p.stopTicks   * p.tick;
                double tgtPx  = entry - p.targetTicks * p.tick;
                if (p.stopTicks > 0 && b.high[i] >= stopPx)       closeTrade(stopPx);
                else if (p.targetTicks > 0 && b.low[i] <= tgtPx)  closeTrade(tgtPx);
            }
        }

        // (c) decide desired position from this bar's state, fill next open
        int desired = 0;
        if      (state[i] ==  1 && p.enableLong)  desired =  1;
        else if (state[i] == -1 && p.enableShort) desired = -1;
        else                                      desired =  0;

        if (i + 1 < n) { pendingDesired = desired; havePending = true; }
    }

    if (pos != 0) closeTrade(b.close[n - 1]); // mark out at end of data

    for (double pl : tradePL)
    {
        m.totalTrades++;
        m.netPL += pl;
        if (pl >= 0.0) { m.wins++;   m.grossProfit += pl; }
        else           { m.losses++; m.grossLoss   += -pl; }
    }
    m.avgWin  = m.wins   ? m.grossProfit / m.wins   : 0.0;
    m.avgLoss = m.losses ? m.grossLoss   / m.losses : 0.0;
    return m;
}

// ---- CSV loading ------------------------------------------------------------
static std::vector<std::string> split(const std::string& s, char d)
{
    std::vector<std::string> out;
    std::string cur;
    std::stringstream ss(s);
    while (std::getline(ss, cur, d)) out.push_back(cur);
    return out;
}
static std::string lower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static bool isNumber(const std::string& s)
{
    if (s.empty()) return false;
    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

static bool loadCsv(const std::string& path, Bars& b)
{
    std::ifstream f(path);
    if (!f.is_open()) { std::cerr << "ERROR: cannot open " << path << "\n"; return false; }

    std::string line;
    int oi = 1, hi = 2, li = 3, ci = 4; // defaults: datetime,o,h,l,c
    bool haveHeader = false;

    if (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto fields = split(line, ',');
        bool anyAlpha = false;
        for (auto& fld : fields)
            for (char c : fld)
                if (std::isalpha((unsigned char)c)) { anyAlpha = true; break; }

        if (anyAlpha)
        {
            haveHeader = true;
            for (size_t k = 0; k < fields.size(); ++k)
            {
                std::string nm = lower(fields[k]);
                if      (nm.find("open")  != std::string::npos) oi = (int)k;
                else if (nm.find("high")  != std::string::npos) hi = (int)k;
                else if (nm.find("low")   != std::string::npos) li = (int)k;
                else if (nm.find("close") != std::string::npos) ci = (int)k;
            }
        }
    }

    if (!haveHeader)
    {
        // first line was data — rewind and process it too
        f.clear();
        f.seekg(0);
    }

    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto fld = split(line, ',');
        int need = std::max(std::max(oi, hi), std::max(li, ci));
        if ((int)fld.size() <= need) continue;
        if (!isNumber(fld[hi]) || !isNumber(fld[li]) || !isNumber(fld[ci])) continue;

        double o = isNumber(fld[oi]) ? std::strtod(fld[oi].c_str(), nullptr)
                                     : std::strtod(fld[ci].c_str(), nullptr);
        b.open.push_back(o);
        b.high.push_back(std::strtod(fld[hi].c_str(), nullptr));
        b.low.push_back(std::strtod(fld[li].c_str(), nullptr));
        b.close.push_back(std::strtod(fld[ci].c_str(), nullptr));
    }
    return b.size() > 0;
}

// ---- CLI helpers ------------------------------------------------------------
static std::vector<int> parseIntList(const std::string& s)
{
    std::vector<int> v;
    for (auto& t : split(s, ',')) if (!t.empty()) v.push_back(std::atoi(t.c_str()));
    if (v.empty()) v.push_back(0);
    return v;
}

int main(int argc, char** argv)
{
    std::string csv, out;
    int countMin = 2, countMax = 6;
    std::vector<int> targets = {0}, stops = {0};
    Params base;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : std::string(); };
        if      (a == "--csv")          csv = next();
        else if (a == "--out")          out = next();
        else if (a == "--count-min")    countMin = std::atoi(next().c_str());
        else if (a == "--count-max")    countMax = std::atoi(next().c_str());
        else if (a == "--targets")      targets = parseIntList(next());
        else if (a == "--stops")        stops = parseIntList(next());
        else if (a == "--tick")         base.tick = std::atof(next().c_str());
        else if (a == "--point-value")  base.pointValue = std::atof(next().c_str());
        else if (a == "--commission")   base.commission = std::atof(next().c_str());
        else if (a == "--long")         base.enableLong = std::atoi(next().c_str()) != 0;
        else if (a == "--short")        base.enableShort = std::atoi(next().c_str()) != 0;
        else { std::cerr << "Unknown arg: " << a << "\n"; return 1; }
    }

    if (csv.empty()) { std::cerr << "Usage: otf_backtest --csv FILE [options]\n"; return 1; }

    Bars bars;
    if (!loadCsv(csv, bars)) { std::cerr << "ERROR: no usable rows in " << csv << "\n"; return 1; }
    std::cerr << "Loaded " << bars.size() << " bars from " << csv << "\n";

    std::vector<Metrics> results;
    for (int c = countMin; c <= countMax; ++c)
        for (int t : targets)
            for (int s : stops)
            {
                Params p = base;
                p.count = c; p.targetTicks = t; p.stopTicks = s;
                results.push_back(backtest(bars, p));
            }

    std::sort(results.begin(), results.end(),
              [](const Metrics& a, const Metrics& b) { return a.netPL > b.netPL; });

    auto pf = [](const Metrics& m) {
        double v = m.profitFactor();
        char buf[32];
        if (std::isinf(v)) std::snprintf(buf, sizeof(buf), "inf");
        else               std::snprintf(buf, sizeof(buf), "%.2f", v);
        return std::string(buf);
    };

    std::printf("\n%-6s %-7s %-6s %8s %8s %8s %7s %9s %9s %9s\n",
                "Count", "Target", "Stop", "NetP/L", "Trades", "Win%", "PF",
                "MaxDD", "AvgWin", "AvgLoss");
    std::printf("%s\n", std::string(86, '-').c_str());
    for (const auto& m : results)
        std::printf("%-6d %-7d %-6d %8.2f %8d %7.1f %7s %9.2f %9.2f %9.2f\n",
                    m.count, m.targetTicks, m.stopTicks, m.netPL, m.totalTrades,
                    m.winRate(), pf(m).c_str(), m.maxDrawdown, m.avgWin, m.avgLoss);

    if (!out.empty())
    {
        std::ofstream o(out);
        o << "Count,TargetTicks,StopTicks,NetPL,TotalTrades,WinRatePct,ProfitFactor,MaxDrawdown,AvgWin,AvgLoss\n";
        for (const auto& m : results)
            o << m.count << "," << m.targetTicks << "," << m.stopTicks << ","
              << m.netPL << "," << m.totalTrades << "," << m.winRate() << ","
              << pf(m) << "," << m.maxDrawdown << "," << m.avgWin << "," << m.avgLoss << "\n";
        std::cerr << "Wrote ranked results to " << out << "\n";
    }
    return 0;
}
