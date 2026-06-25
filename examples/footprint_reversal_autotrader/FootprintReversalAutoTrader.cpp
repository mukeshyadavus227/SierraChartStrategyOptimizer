#include "sierrachart.h"

SCDLLName("Footprint Reversal AutoTrader (SCAFFOLD)")

/*=============================================================================
  Footprint Reversal AutoTrader  —  UNTESTED SCAFFOLD, SIMULATION ONLY

  WHAT THIS IS
    An automated-trading wrapper around the FootprintReversalSystem signal
    engine by Patrick Senas (FutTrader), MIT-licensed:
        https://github.com/FutTrader/footprint-system
    The 4-condition reversal signal (relative volume, finished auction,
    diagonal imbalance, POC location) is reused essentially verbatim and is
    credited to that project and to Adam @ JumpstartTrading. This file only
    ADDS the trade-management layer the original deliberately omits.

  WHAT WAS ADDED vs THE ORIGINAL
    1. REPAINT FIX — the original alerts on the still-forming bar
       (BarIndex == ArraySize-1). Footprint data mutates intrabar, so that
       signal can appear then vanish. This version TRADES ONLY on the last
       CLOSED bar's final footprint.
    2. ENTRY — on a confirmed closed-bar signal while flat, submit a market
       BuyEntry/SellEntry with a native server-side OCO bracket.
    3. EXITS (choice 'a': fixed R:R bracket + EOD flatten) —
         stop  = reversal bar's extreme +/- StopBufferTicks
         target= entry +/- RewardRisk * risk
       plus an end-of-session flatten. See the TODO markers to switch to
       opposite-signal or time-based exits later.
    4. SAFETY INTERLOCK — orders route only if "Enable Order Submission"=Yes
       AND (Trade Simulation Mode ON, or "Allow Live"=Yes). With Sim Mode on,
       this can NEVER reach a live broker.
    5. MaintainTradeStatisticsAndTradesData = 1 so the Strategy Optimizer can
       read results and sweep parameters.

  *** DO NOT TRADE THIS LIVE. ***  It is an untested starting point. Compile
  it (Analysis >> Build Custom Studies DLL), run it ONLY with Trade Simulation
  Mode ON, confirm it behaves, then validate out-of-sample before believing
  anything. Feasibility of automation is NOT evidence of edge.

  DEPENDENCY (unchanged from the original): a "Number Bars Calculated" study
  on the same chart, with its SG14 (VolBarDiff) selected via the inputs below.
  Designed for range-bar footprint charts.
=============================================================================*/

static inline int HHMM(const SCDateTime& dt) { return dt.GetHour() * 100 + dt.GetMinute(); }

SCSFExport scsf_FootprintReversalAutoTrader(SCStudyInterfaceRef sc)
{
    // ---- Subgraphs (signal visualization, from the original) ----
    SCSubgraphRef Buy  = sc.Subgraph[0];
    SCSubgraphRef Sell = sc.Subgraph[1];

    // ---- Inputs: signal (indices 0-9, same meaning as the original) ----
    SCInputRef Input_LongSignal           = sc.Input[0];
    SCInputRef Input_ShortSignal          = sc.Input[1];
    SCInputRef Input_AlertNumber          = sc.Input[2];
    SCInputRef Input_Study1               = sc.Input[3];   // NumberBars2 study ID
    SCInputRef Input_Study1Subgraph       = sc.Input[4];   // SG14 VolBarDiff
    SCInputRef Input_FillToTopBottomBar   = sc.Input[5];
    SCInputRef Input_PercentageThreshold  = sc.Input[6];   // diagonal imbalance % (default 400)
    SCInputRef Input_AllowZeroValueCompares = sc.Input[7];
    SCInputRef Input_DivideByZeroAction   = sc.Input[8];
    SCInputRef Input_PBBarLimit           = sc.Input[9];   // ticks from H/L for POC test (default 4)

    // ---- Inputs: trading (indices 10+, NEW) ----
    SCInputRef Input_EnableOrders   = sc.Input[10];
    SCInputRef Input_AllowLive      = sc.Input[11];
    SCInputRef Input_OrderQty       = sc.Input[12];
    SCInputRef Input_StopBufferTicks= sc.Input[13];
    SCInputRef Input_RewardRisk     = sc.Input[14];
    SCInputRef Input_UseEODFlatten  = sc.Input[15];
    SCInputRef Input_EODFlattenHHMM = sc.Input[16];
    SCInputRef Input_MaxTradesPerDay= sc.Input[17];
    SCInputRef Input_FlattenNow     = sc.Input[18];

    if (sc.SetDefaults)
    {
        sc.GraphName = "Footprint Reversal AutoTrader (SCAFFOLD)";
        sc.StudyDescription = "Automated wrapper around FutTrader's footprint reversal signal. "
            "Signal-only unless Enable Order Submission=Yes. SIM-ONLY scaffold; not proven.";
        sc.GraphRegion = 0;
        sc.AutoLoop = 0;                       // manual loop (original needs full-bar iteration)
        sc.ValueFormat = VALUEFORMAT_INHERITED;
        sc.FreeDLL = 0;
        sc.MaintainVolumeAtPriceData = 1;
        sc.MaintainTradeStatisticsAndTradesData = 1;   // REQUIRED for the optimizer to read results
        sc.CalculationPrecedence = LOW_PREC_LEVEL;

        // trading flags
        sc.AllowMultipleEntriesInSameDirection = 0;
        sc.MaximumPositionAllowed = 1000;
        sc.SupportReversals = 0;
        sc.AllowOppositeEntryWithOpposingPositionOrOrders = 0;
        sc.SupportAttachedOrdersForTrading = 1;        // native server-side bracket
        sc.CancelAllOrdersOnEntriesAndReversals = 1;
        sc.AllowOnlyOneTradePerBar = 1;
        sc.SendOrdersToTradeService = 0;               // flipped by the interlock at runtime

        Buy.Name = "Buy";  Buy.PrimaryColor = RGB(0,255,0);
        Buy.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_RECTANGLE_BOTTOM; Buy.DrawZeros = 0;
        Sell.Name = "Sell"; Sell.PrimaryColor = RGB(255,0,0);
        Sell.DrawStyle = DRAWSTYLE_TRANSPARENT_FILL_RECTANGLE_BOTTOM; Sell.DrawZeros = 0;

        Input_LongSignal.Name = "Allow Long (B) Reversals"; Input_LongSignal.SetYesNo(true);
        Input_ShortSignal.Name = "Allow Short (P) Reversals"; Input_ShortSignal.SetYesNo(true);
        Input_AlertNumber.Name = "Reversal Alert Number (0=off)"; Input_AlertNumber.SetAlertSoundNumber(0);
        Input_Study1.Name = "Input NumberBars2 (study)"; Input_Study1.SetStudyID(0);
        Input_Study1Subgraph.Name = "Relative Volume Study (SG14)"; Input_Study1Subgraph.SetSubgraphIndex(0);
        Input_FillToTopBottomBar.Name = "Draw Fill Top/Bottom"; Input_FillToTopBottomBar.SetCustomInputStrings("Top;Bottom"); Input_FillToTopBottomBar.SetCustomInputIndex(0);
        Input_PercentageThreshold.Name = "Buy/Sell Imbalance % Threshold"; Input_PercentageThreshold.SetInt(400);
        Input_AllowZeroValueCompares.Name = "Enable Zero Bid/Ask Compares"; Input_AllowZeroValueCompares.SetYesNo(0);
        Input_DivideByZeroAction.Name = "Zero Value Compare Action"; Input_DivideByZeroAction.SetCustomInputStrings("Set 0 to 1;Set Percentage to +/- 1000%"); Input_DivideByZeroAction.SetCustomInputIndex(0);
        Input_PBBarLimit.Name = "Ticks from High/Low for PB Reversal"; Input_PBBarLimit.SetInt(4);

        Input_EnableOrders.Name = "Enable Order Submission (sim/live)"; Input_EnableOrders.SetYesNo(false);
        Input_AllowLive.Name = "ALLOW LIVE (No = simulation interlock)"; Input_AllowLive.SetYesNo(false);
        Input_OrderQty.Name = "Order Quantity (Contracts)"; Input_OrderQty.SetInt(1); Input_OrderQty.SetIntLimits(1, 1000);
        Input_StopBufferTicks.Name = "Stop Buffer Beyond Bar Extreme (ticks)"; Input_StopBufferTicks.SetInt(2); Input_StopBufferTicks.SetIntLimits(0, 1000);
        Input_RewardRisk.Name = "Reward:Risk Multiple (target)"; Input_RewardRisk.SetFloat(2.0f); Input_RewardRisk.SetFloatLimits(0.1f, 50.0f);
        Input_UseEODFlatten.Name = "Flatten At End Of Day"; Input_UseEODFlatten.SetYesNo(true);
        Input_EODFlattenHHMM.Name = "EOD Flatten Time (HHMM, chart time)"; Input_EODFlattenHHMM.SetInt(1555); Input_EODFlattenHHMM.SetIntLimits(0, 2359);
        Input_MaxTradesPerDay.Name = "Max Trades Per Day (0=off)"; Input_MaxTradesPerDay.SetInt(0); Input_MaxTradesPerDay.SetIntLimits(0, 1000);
        Input_FlattenNow.Name = "FLATTEN NOW (close + cancel all)"; Input_FlattenNow.SetYesNo(false);
        return;
    }

    // VAP data must exist or we cannot evaluate footprint.
    if ((int)sc.VolumeAtPriceForBars->GetNumberOfBars() < sc.ArraySize)
        return;

    const bool LongSignal  = Input_LongSignal.GetYesNo();
    const bool ShortSignal = Input_ShortSignal.GetYesNo();
    const bool EnableAlerts = sc.IsFullRecalculation == 0 && !sc.DownloadingHistoricalData;

    SCFloatArray RelativeVolumeArray;
    sc.GetStudyArrayUsingID(Input_Study1.GetStudyID(), Input_Study1Subgraph.GetSubgraphIndex(), RelativeVolumeArray);

    //==========================================================================
    // SIGNAL ENGINE — reused from FutTrader/footprint-system (MIT), unchanged
    // logic. Writes Buy[]/Sell[] (nonzero = signal) for every updated bar.
    //==========================================================================
    for (int BarIndex = sc.UpdateStartIndex; BarIndex < sc.ArraySize; BarIndex++)
    {
        float Offset = (sc.High[BarIndex] - sc.Low[BarIndex]) * (15 * 0.01f);
        Buy[BarIndex] = 0; Sell[BarIndex] = 0;

        int NumberOfPricesAtBarIndex = sc.VolumeAtPriceForBars->GetSizeAtBarIndex(BarIndex);

        bool RelVol = false, NoUnfinishedBusiness = false;
        bool BuyImbalance = false, SellImbalance = false;
        bool BReversal = false, PReversal = false;

        float Bar1 = RelativeVolumeArray[BarIndex];
        if (Bar1 > 0) RelVol = true;

        for (int PriceIndex = 0; PriceIndex < NumberOfPricesAtBarIndex; ++PriceIndex)
        {
            const s_VolumeAtPriceV2* p_VolumeAtPrice = NULL;
            if (!sc.VolumeAtPriceForBars->GetVAPElementAtIndex(BarIndex, PriceIndex, &p_VolumeAtPrice))
                continue;

            const s_VolumeAtPriceV2* p_NextVolumeAtPrice = NULL;
            if (PriceIndex < NumberOfPricesAtBarIndex - 1)
                sc.VolumeAtPriceForBars->GetVAPElementAtIndex(BarIndex, PriceIndex + 1, &p_NextVolumeAtPrice);

            // Unfinished-auction check at the bar extreme
            if (LongSignal && (PriceIndex == 0))
            { if (p_VolumeAtPrice->AskVolume == 0) NoUnfinishedBusiness = true; }
            else if (ShortSignal && (PriceIndex == NumberOfPricesAtBarIndex - 1))
            { if (p_VolumeAtPrice->BidVolume == 0) NoUnfinishedBusiness = true; }

            // Diagonal ask/bid imbalance vs threshold
            bool AllowZero = Input_AllowZeroValueCompares.GetYesNo();
            unsigned int DivZeroAction = Input_DivideByZeroAction.GetIndex();
            int RatioPct = 0;
            if (p_NextVolumeAtPrice != NULL)
            {
                int Thresh = Input_PercentageThreshold.GetInt();
                if (LongSignal && (p_NextVolumeAtPrice->AskVolume > p_VolumeAtPrice->BidVolume) && (p_VolumeAtPrice->BidVolume > 0 || AllowZero))
                {
                    if (p_VolumeAtPrice->BidVolume == 0 && DivZeroAction == 0) RatioPct = (p_NextVolumeAtPrice->AskVolume / 1) * 100;
                    else if (p_VolumeAtPrice->BidVolume == 0 && DivZeroAction == 1) RatioPct = 1000;
                    else RatioPct = sc.Round(((float)p_NextVolumeAtPrice->AskVolume / p_VolumeAtPrice->BidVolume) * 100);
                    if (Thresh > 0 && RatioPct >= Thresh) BuyImbalance = true;
                }
                if (ShortSignal && (p_VolumeAtPrice->BidVolume > p_NextVolumeAtPrice->AskVolume) && (p_NextVolumeAtPrice->AskVolume > 0 || AllowZero))
                {
                    if (p_NextVolumeAtPrice->AskVolume == 0 && DivZeroAction == 0) RatioPct = (p_VolumeAtPrice->BidVolume / 1) * 100;
                    else if (p_NextVolumeAtPrice->AskVolume == 0 && DivZeroAction == 1) RatioPct = 1000;
                    else RatioPct = sc.Round(((float)p_VolumeAtPrice->BidVolume / p_NextVolumeAtPrice->AskVolume) * 100);
                    if (Thresh > 0 && RatioPct >= Thresh) SellImbalance = true;
                }
            }
        }

        // POC-location reversal test
        s_VolumeAtPriceV2 TickPOC;
        sc.GetPointOfControlPriceVolumeForBar(BarIndex, TickPOC);
        float PricePOC = TickPOC.PriceInTicks * sc.TickSize;
        unsigned int BarLimit = Input_PBBarLimit.GetInt();
        if (BarLimit >= (unsigned)NumberOfPricesAtBarIndex) BarLimit = 1;
        if (PricePOC <= (sc.High[BarIndex] - (sc.TickSize * BarLimit))) BReversal = true;
        else if (PricePOC >= (sc.Low[BarIndex] + (sc.TickSize * BarLimit))) PReversal = true;

        unsigned int FillIdx = Input_FillToTopBottomBar.GetIndex();
        if (RelVol && NoUnfinishedBusiness && BuyImbalance && BReversal)        // long
            Buy[BarIndex]  = (FillIdx == 0) ? sc.High[BarIndex] + Offset : sc.Low[BarIndex] - Offset;
        else if (RelVol && NoUnfinishedBusiness && SellImbalance && PReversal)  // short
            Sell[BarIndex] = (FillIdx == 0) ? sc.High[BarIndex] + Offset : sc.Low[BarIndex] - Offset;
    }

    //==========================================================================
    // TRADING LAYER (NEW) — acts ONLY on the last CLOSED bar (repaint fix)
    //==========================================================================
    if (sc.IsFullRecalculation || sc.DownloadingHistoricalData) return;

    int lastClosed = sc.ArraySize - 1;
    if (sc.GetBarHasClosedStatus(lastClosed) != BHCS_BAR_HAS_CLOSED) lastClosed = sc.ArraySize - 2;
    if (lastClosed < 1) return;

    // persistent state
    int&   r_LastProcessed = sc.GetPersistentInt(1);
    int&   r_CurDate       = sc.GetPersistentInt(2);
    int&   r_DailyTrades   = sc.GetPersistentInt(3);
    int&   r_EODSent       = sc.GetPersistentInt(4);

    // ---- safety interlock: sim-or-explicit-live, else signal-only ----
    bool ordersOn = Input_EnableOrders.GetYesNo();
    bool simOn    = (sc.GlobalTradeSimulationIsOn != 0);
    bool mayRoute = ordersOn && (simOn || Input_AllowLive.GetYesNo());
    sc.SendOrdersToTradeService = mayRoute ? 1 : 0;

    s_SCPositionData pos; sc.GetTradePosition(pos);
    const bool isFlat = (pos.PositionQuantity == 0 && pos.WorkingOrdersExist == 0);

    // ---- manual flatten ----
    if (Input_FlattenNow.GetYesNo())
    {
        if (mayRoute) sc.FlattenAndCancelAllOrders();
        return;
    }

    SCDateTime bdt = sc.BaseDateTimeIn[lastClosed];
    int today = sc.GetTradingDayDate(bdt);
    if (today != r_CurDate) { r_CurDate = today; r_DailyTrades = 0; r_EODSent = 0; }

    // ---- EOD flatten ----
    const int eod = Input_EODFlattenHHMM.GetInt();
    bool pastEOD = Input_UseEODFlatten.GetYesNo() && (HHMM(sc.BaseDateTimeIn[sc.ArraySize - 1]) >= eod);
    if (pastEOD && !isFlat && !r_EODSent)
    {
        if (mayRoute) { sc.FlattenAndCancelAllOrders(); r_EODSent = 1; }
    }

    // ---- entry on a fresh closed bar ----
    bool newBar = (lastClosed > r_LastProcessed);
    if (newBar && isFlat && !pastEOD)
    {
        bool longSig  = (Buy[lastClosed]  != 0);
        bool shortSig = (Sell[lastClosed] != 0);
        bool dailyOK  = (Input_MaxTradesPerDay.GetInt() == 0) || (r_DailyTrades < Input_MaxTradesPerDay.GetInt());

        if ((longSig || shortSig) && dailyOK && mayRoute)
        {
            const int   qty    = Input_OrderQty.GetInt();
            const float buf    = Input_StopBufferTicks.GetInt() * (float)sc.TickSize;
            const float rr     = Input_RewardRisk.GetFloat();
            const float entryRef = sc.Close[lastClosed];

            // EXIT MODEL 'a': fixed R:R bracket off the reversal bar's extreme.
            // TODO(exit-b): instead of a fixed target, close on the opposite
            //   signal (longSig while short / shortSig while long) in a later pass.
            // TODO(exit-c): replace the target with a max-bars-in-trade / session exit.
            float stopPrice, tpPrice;
            if (longSig)
            {
                stopPrice = sc.Low[lastClosed] - buf;
                float risk = entryRef - stopPrice;
                if (!(risk > 0)) { r_LastProcessed = lastClosed; return; }
                tpPrice = entryRef + rr * risk;
            }
            else
            {
                stopPrice = sc.High[lastClosed] + buf;
                float risk = stopPrice - entryRef;
                if (!(risk > 0)) { r_LastProcessed = lastClosed; return; }
                tpPrice = entryRef - rr * risk;
            }

            s_SCNewOrder o;
            o.OrderType        = SCT_ORDERTYPE_MARKET;
            o.OrderQuantity    = qty;
            o.TextTag          = "FP_Reversal_AutoTrader";
            o.Target1Price     = tpPrice;
            o.Stop1Price       = stopPrice;
            o.OCOGroup1Quantity= qty;
            o.TimeInForce      = SCT_TIF_GOOD_TILL_CANCELED;

            double result = longSig ? sc.BuyEntry(o) : sc.SellEntry(o);
            if (result > 0)
            {
                r_DailyTrades += 1;
                SCString m; m.Format("FP AutoTrader %s entry: qty %d @~%.4f stop %.4f tp %.4f (R:R %.1f)",
                    longSig ? "LONG" : "SHORT", qty, entryRef, stopPrice, tpPrice, rr);
                sc.AddMessageToLog(m, 1);
                if (EnableAlerts && Input_AlertNumber.GetAlertSoundNumber() > 0)
                    sc.SetAlert(Input_AlertNumber.GetAlertSoundNumber() - 1, "FP reversal entry");
            }
            else
            {
                SCString m; m.Format("FP AutoTrader: order rejected (code %.0f).", result);
                sc.AddMessageToLog(m, 1);
            }
        }
    }

    if (lastClosed > r_LastProcessed) r_LastProcessed = lastClosed;
}
