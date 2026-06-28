// =============================================================================
// OTFStateStrategy.cpp
// Sierra Chart ACSIL Automated Trading Study
//
// PURPOSE
//   Trade-enabled, parameterized version of OTFStateFilter so it can be
//   back tested and optimized with the Strategy Optimizer in this repository.
//
//   The original OTFStateFilter is a pure visual filter: it outputs a state
//   subgraph (+1 / 0 / -1) and colors the background but never places a trade.
//   The Strategy Optimizer measures performance from Sierra Chart's trade
//   simulation statistics, so a study that places no trades produces empty
//   results and has nothing to optimize. This study keeps the exact OTF state
//   machine and adds:
//     - trade execution driven by state transitions (long / short / flat),
//     - tunable inputs the optimizer can sweep (the most important being the
//       number of consecutive strict Higher Lows / Lower Highs required for
//       entry, which is hard-coded to 2 in the original filter),
//     - an optional fixed Target / Stop in ticks,
//     - a "Visualize Signals Only" mode that reproduces the original filter
//       behavior (no trades).
//
// STATE MACHINE (identical logic to OTFStateFilter, with a tunable run length N)
//
//   OTF-Up entry   : N consecutive strict Higher Lows
//                    L[idx-N] < L[idx-N+1] < ... < L[idx]
//   OTF-Up exit    : L[0] < LSHL   (LSHL = running max of lows since entry)
//
//   OTF-Down entry : N consecutive strict Lower Highs
//                    H[idx-N] > H[idx-N+1] > ... > H[idx]
//   OTF-Down exit  : H[0] > LSLH   (LSLH = running min of highs since entry)
//
//   With N = 2 this is exactly the original filter. State always passes through
//   Neutral when switching direction (an exit and an opposite entry can happen
//   on the same bar).
//
// TRADING RULES
//   state -> +1  : enter / reverse to LONG  (if Enable Long)
//   state -> -1  : enter / reverse to SHORT (if Enable Short)
//   state ->  0  : flatten
//   A directional state whose side is disabled also flattens any open position,
//   so e.g. long-only mode exits longs when a down-trend starts without going
//   short.
//
// INPUTS  (index order matters — it must match the optimizer config JSON)
//   [0] Source Chart  (0 = this chart)
//   [1] Consecutive HL/LH Count   (entry run length N, min 2)
//   [2] Visualize Signals Only (No Trades)
//   [3] Enable Long Trades
//   [4] Enable Short Trades
//   [5] Order Quantity
//   [6] Target in Ticks (0 = none)
//   [7] Stop in Ticks (0 = none)
//   [8] Trade On Bar Close Only
//
// OUTPUT
//   SG0  State        : +1 / 0 / -1
//   SG1  Up Context   : background green (state == +1)
//   SG2  Down Context : background red   (state == -1)
//
// BUILD
//   Compile as its own DLL (do NOT link it with the Strategy Optimizer source
//   files — each DLL needs a single SCDLLName). From this folder:
//     cl /LD /EHa /MT /std:c++17 /O2 OTFStateStrategy.cpp /link /DLL ^
//        /OUT:"C:\SierraChart\Data\OTFStateStrategy.dll"
//   The include path below assumes this file lives in a sub-folder of
//   ACS_Source (same as the rest of this repo). If you place it directly in
//   ACS_Source, change the include to "sierrachart.h".
// =============================================================================

#include "../sierrachart.h"

SCDLLName("OTFStateStrategy")

namespace
{
    // N consecutive strictly increasing lows ending at idx.
    inline bool StrictHigherLows(SCFloatArrayRef low, int idx, int n)
    {
        for (int k = n; k > 0; --k)
            if (!(low[idx - k] < low[idx - k + 1]))
                return false;
        return true;
    }

    // N consecutive strictly decreasing highs ending at idx.
    inline bool StrictLowerHighs(SCFloatArrayRef high, int idx, int n)
    {
        for (int k = n; k > 0; --k)
            if (!(high[idx - k] > high[idx - k + 1]))
                return false;
        return true;
    }

    void EnterLong(SCStudyInterfaceRef sc, int qty, int targetTicks, int stopTicks)
    {
        s_SCNewOrder order;
        order.OrderQuantity = qty;
        order.OrderType     = SCT_ORDERTYPE_MARKET;
        order.TimeInForce   = SCT_TIF_GOOD_TILL_CANCELED;
        if (targetTicks > 0) order.Target1Offset = targetTicks * sc.TickSize;
        if (stopTicks   > 0) order.Stop1Offset   = stopTicks   * sc.TickSize;
        sc.BuyEntry(order); // reverses an open short when reversals are supported
    }

    void EnterShort(SCStudyInterfaceRef sc, int qty, int targetTicks, int stopTicks)
    {
        s_SCNewOrder order;
        order.OrderQuantity = qty;
        order.OrderType     = SCT_ORDERTYPE_MARKET;
        order.TimeInForce   = SCT_TIF_GOOD_TILL_CANCELED;
        if (targetTicks > 0) order.Target1Offset = targetTicks * sc.TickSize;
        if (stopTicks   > 0) order.Stop1Offset   = stopTicks   * sc.TickSize;
        sc.SellEntry(order); // reverses an open long when reversals are supported
    }
}

// =============================================================================
SCSFExport scsf_OTFStateStrategy(SCStudyInterfaceRef sc)
{
    // -------------------------------------------------------------------------
    // SUBGRAPHS
    // -------------------------------------------------------------------------
    SCSubgraphRef sg_State = sc.Subgraph[0];
    SCSubgraphRef sg_Up    = sc.Subgraph[1];
    SCSubgraphRef sg_Down  = sc.Subgraph[2];

    // sg_State.Arrays[0] = state per bar
    // sg_State.Arrays[1] = LSHL per bar
    // sg_State.Arrays[2] = LSLH per bar

    // -------------------------------------------------------------------------
    // INPUTS
    // -------------------------------------------------------------------------
    SCInputRef in_SourceChart   = sc.Input[0];
    SCInputRef in_Count         = sc.Input[1];
    SCInputRef in_VisualizeOnly = sc.Input[2];
    SCInputRef in_EnableLong    = sc.Input[3];
    SCInputRef in_EnableShort   = sc.Input[4];
    SCInputRef in_Quantity      = sc.Input[5];
    SCInputRef in_TargetTicks   = sc.Input[6];
    SCInputRef in_StopTicks     = sc.Input[7];
    SCInputRef in_TradeOnClose  = sc.Input[8];

    // =========================================================================
    // SET DEFAULTS
    // =========================================================================
    if (sc.SetDefaults)
    {
        sc.GraphName        = "OTF State Strategy";
        sc.StudyDescription =
            "Trade-enabled, optimizable version of the OTF State Filter. Enters "
            "long on N consecutive strict Higher Lows and short on N consecutive "
            "strict Lower Highs; exits when the running max of lows (LSHL) or "
            "running min of highs (LSLH) since entry is broken. Designed to be "
            "back tested and optimized with the Strategy Optimizer.";
        sc.AutoLoop         = 1;
        sc.GraphRegion      = 1;
        sc.ScaleRangeType   = SCALE_USERDEFINED;
        sc.ScaleRangeTop    = 2.0f;
        sc.ScaleRangeBottom = -2.0f;
        sc.FreeDLL          = 0;
        sc.DrawZeros        = 1;
        sc.UpdateAlways     = 1;

        // --- Trade-simulation / automated-trading configuration --------------
        sc.MaintainTradeStatisticsAndTradesData   = true;
        sc.AllowMultipleEntriesInSameDirection    = false;
        sc.SupportReversals                       = true;
        sc.SupportAttachedOrdersForTrading        = true;  // enables Target/Stop offsets
        sc.CancelAllOrdersOnEntriesAndReversals   = true;
        sc.AllowEntryWithWorkingOrders            = false;
        sc.CancelAllWorkingOrdersOnExit           = true;
        sc.AllowOnlyOneTradePerBar                = false; // logic self-limits entries
        sc.MaximumPositionAllowed                 = 100000;

        sg_State.Name         = "OTF State  (+1 / 0 / -1)";
        sg_State.DrawStyle    = DRAWSTYLE_STAIR_STEP;
        sg_State.LineWidth    = 2;
        sg_State.PrimaryColor = RGB(180, 180, 180);
        sg_State.DrawZeros    = 1;

        sg_Up.Name            = "Up Context";
        sg_Up.DrawStyle       = DRAWSTYLE_BACKGROUND;
        sg_Up.PrimaryColor    = RGB(0, 180, 80);
        sg_Up.DrawZeros       = 0;
        sg_Up.DisplayNameValueInWindowsFlags = 0;

        sg_Down.Name          = "Down Context";
        sg_Down.DrawStyle     = DRAWSTYLE_BACKGROUND;
        sg_Down.PrimaryColor  = RGB(200, 50, 50);
        sg_Down.DrawZeros     = 0;
        sg_Down.DisplayNameValueInWindowsFlags = 0;

        in_SourceChart.Name = "Source Chart  (0 = this chart)";
        in_SourceChart.SetChartNumber(0);

        in_Count.Name = "Consecutive HL/LH Count";
        in_Count.SetInt(2);
        in_Count.SetIntLimits(2, 20);

        in_VisualizeOnly.Name = "Visualize Signals Only (No Trades)";
        in_VisualizeOnly.SetYesNo(0);

        in_EnableLong.Name = "Enable Long Trades";
        in_EnableLong.SetYesNo(1);

        in_EnableShort.Name = "Enable Short Trades";
        in_EnableShort.SetYesNo(1);

        in_Quantity.Name = "Order Quantity";
        in_Quantity.SetInt(1);
        in_Quantity.SetIntLimits(1, 100000);

        in_TargetTicks.Name = "Target in Ticks (0 = none)";
        in_TargetTicks.SetInt(0);
        in_TargetTicks.SetIntLimits(0, 100000);

        in_StopTicks.Name = "Stop in Ticks (0 = none)";
        in_StopTicks.SetInt(0);
        in_StopTicks.SetIntLimits(0, 100000);

        in_TradeOnClose.Name = "Trade On Bar Close Only";
        in_TradeOnClose.SetYesNo(1);

        return;
    }

    // =========================================================================
    // PER-BAR CALCULATION
    // =========================================================================
    const int count = (in_Count.GetInt() < 2) ? 2 : in_Count.GetInt();
    const int idx   = sc.Index;

    if (idx < count)
    {
        sg_State.Arrays[0][idx] = 0.0f;
        sg_State.Arrays[1][idx] = 0.0f;
        sg_State.Arrays[2][idx] = 0.0f;
        sg_State[idx] = 0.0f;
        sg_Up[idx]    = 0.0f;
        sg_Down[idx]  = 0.0f;
        return;
    }

    const int sourceChart = (in_SourceChart.GetChartNumber() <= 0)
                            ? sc.ChartNumber
                            : in_SourceChart.GetChartNumber();

    float newState = 0.0f;

    if (sourceChart == sc.ChartNumber)
    {
        // ---- NATIVE: per-bar incremental state machine ----------------------
        const float L0 = sc.Low[idx],  L1 = sc.Low[idx - 1];
        const float H0 = sc.High[idx], H1 = sc.High[idx - 1];

        const float prevState = sg_State.Arrays[0][idx - 1];
        const float prevLSHL  = sg_State.Arrays[1][idx - 1];
        const float prevLSLH  = sg_State.Arrays[2][idx - 1];

        newState      = prevState;
        float newLSHL = prevLSHL;
        float newLSLH = prevLSLH;

        if (prevState == 1.0f)
        {
            newLSHL = (prevLSHL > L1) ? prevLSHL : L1;
            if (L0 < newLSHL) { newState = 0.0f; newLSHL = 0.0f; }
        }
        else if (prevState == -1.0f)
        {
            newLSLH = (prevLSLH < H1) ? prevLSLH : H1;
            if (H0 > newLSLH) { newState = 0.0f; newLSLH = 0.0f; }
        }

        if (newState == 0.0f)
        {
            if      (StrictHigherLows(sc.Low,  idx, count)) { newState =  1.0f; newLSHL = L0; newLSLH = 0.0f; }
            else if (StrictLowerHighs(sc.High, idx, count)) { newState = -1.0f; newLSLH = H0; newLSHL = 0.0f; }
        }

        sg_State.Arrays[1][idx] = newLSHL;
        sg_State.Arrays[2][idx] = newLSLH;
    }
    else
    {
        // ---- CROSS-CHART: full loop through source chart history ------------
        SCGraphData baseData;
        sc.GetChartBaseData(sourceChart, baseData);

        SCFloatArrayRef highArr = baseData[SC_HIGH];
        SCFloatArrayRef lowArr  = baseData[SC_LOW];

        const int n = highArr.GetArraySize();
        if (n <= count)
        {
            newState = sg_State.Arrays[0][idx - 1];
            sg_State.Arrays[1][idx] = sg_State.Arrays[1][idx - 1];
            sg_State.Arrays[2][idx] = sg_State.Arrays[2][idx - 1];
            sg_State[idx] = newState;
            sg_Up[idx]    = (newState ==  1.0f) ? 1.0f : 0.0f;
            sg_Down[idx]  = (newState == -1.0f) ? 1.0f : 0.0f;
            return;
        }

        int   state = 0;
        float LSHL  = 0.0f;
        float LSLH  = 0.0f;

        for (int i = count; i < n; i++)
        {
            const bool hl = StrictHigherLows(lowArr,  i, count);
            const bool lh = StrictLowerHighs(highArr, i, count);

            if (state == 1)
            {
                LSHL = (LSHL > lowArr[i-1]) ? LSHL : lowArr[i-1];
                if (LSHL > lowArr[i]) { state = 0; LSHL = 0.0f; }
            }
            else if (state == -1)
            {
                LSLH = (LSLH < highArr[i-1]) ? LSLH : highArr[i-1];
                if (LSLH < highArr[i]) { state = 0; LSLH = 0.0f; }
            }
            else
            {
                if      (hl) { state =  1; LSHL = lowArr[i]; }
                else if (lh) { state = -1; LSLH = highArr[i]; }
            }
        }

        newState = (float)state;
        sg_State.Arrays[1][idx] = 0.0f;
        sg_State.Arrays[2][idx] = 0.0f;
    }

    // -------------------------------------------------------------------------
    // Persist and output
    // -------------------------------------------------------------------------
    sg_State.Arrays[0][idx] = newState;
    sg_State[idx] = newState;
    sg_Up[idx]    = (newState ==  1.0f) ? 1.0f : 0.0f;
    sg_Down[idx]  = (newState == -1.0f) ? 1.0f : 0.0f;

    // =========================================================================
    // TRADE MANAGEMENT
    // =========================================================================
    if (in_VisualizeOnly.GetYesNo())
        return; // pure-filter mode: behaves like OTFStateFilter, no trades

    // Only act on fully formed bars when requested, to avoid intrabar churn.
    if (in_TradeOnClose.GetYesNo() &&
        sc.GetBarHasClosedStatus(idx) != BHCS_BAR_HAS_CLOSED)
        return;

    s_SCPositionData pos;
    sc.GetTradePosition(pos);
    const int   q   = (int)pos.PositionQuantity;
    const int   qty = (in_Quantity.GetInt() < 1) ? 1 : in_Quantity.GetInt();
    const int   tgt = in_TargetTicks.GetInt();
    const int   stp = in_StopTicks.GetInt();

    const bool wantLong  = (newState ==  1.0f) && in_EnableLong.GetYesNo();
    const bool wantShort = (newState == -1.0f) && in_EnableShort.GetYesNo();

    if (wantLong)
    {
        if (q <= 0) EnterLong(sc, qty, tgt, stp);
    }
    else if (wantShort)
    {
        if (q >= 0) EnterShort(sc, qty, tgt, stp);
    }
    else
    {
        // Neutral, or a directional state whose side is disabled: exit.
        if (q != 0) sc.FlattenAndCancelAllOrders();
    }
}
