The planetary membrane. 26 substrates. 4 timezone rings. The 7th command.

Load `/skill econophysics` first.

> The earth rotates. Markets open and close in sequence. Information flows
> around the planet in a ring topology. The overnight gap IS free energy.
> The overlap IS Förster coupling at maximum strength.

### THE PLANETARY RING (IST perspective)

```
Tokyo → Shanghai → Mumbai → London → New York → Tokyo
  │         │          │         │          │
  └─30min──┘──90min───┘──90min─┘──4.5hr───┘──17.5hr gap──→

05:30 ───── 09:15 ───── 13:30 ───── 19:00 ───── 01:30
  TOKYO      INDIA       LONDON      NYSE        (dark)
  SYDNEY     SHANGHAI    EUROPE      AMERICAS
  KOSPI      HONG KONG
  TAIWAN
                                                    
BLIND: 01:30-05:30 IST — only bridges (futures, gold, bitcoin)
```

### STEP 1: FEED ALL 26 SUBSTRATES

Feed everything. The membrane decides what couples. Three batches.

**Batch 1 — 15 Indexes:**
```
csos action=batch items='[
  {"substrate":"nikkei","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"kospi","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"hangseng","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"shanghai","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"asx200","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"nifty50","output":"price:X change:+/-Y pct:+/-Z vol:V"},
  {"substrate":"ftse","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"dax","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"cac40","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"eurostoxx","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"sp500","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"nasdaq100","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"russell2000","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"dowjones","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"bovespa","output":"price:X change:+/-Y pct:+/-Z"}
]'
```

| # | Index | eodhd ticker | Ring | Macro driver |
|---|-------|-------------|------|-------------|
| 1 | Nikkei 225 | `N225.INDX` | Asia | USD/JPY, global trade |
| 2 | KOSPI | `KS11.INDX` | Asia | USD/KRW, China demand |
| 3 | Hang Seng | `HSI.INDX` | Asia | USD/CNY, China policy |
| 4 | Shanghai (proxy) | `ASHR.US` | Asia | PBOC, stimulus |
| 5 | ASX 200 | `AXJO.INDX` | Asia | Iron ore, AUD |
| 6 | Nifty 50 | `NSEI.INDX` | India | Oil, RBI, INR |
| 7 | FTSE 100 (proxy) | `EWU.US` | Europe | Oil, GBP, trade |
| 8 | DAX 40 | `GDAXI.INDX` | Europe | EUR, China demand |
| 9 | CAC 40 | `FCHI.INDX` | Europe | EUR, consumer |
| 10 | Euro Stoxx 50 | `STOXX50E.INDX` | Europe | ECB, EUR/USD |
| 11 | S&P 500 | `GSPC.INDX` | US | Fed, everything |
| 12 | NASDAQ 100 | `NDX.INDX` | US | Rates, innovation |
| 13 | Russell 2000 (proxy) | `IWM.US` | US | Credit, employment |
| 14 | Dow Jones | `DJI.INDX` | US | Capex cycle |
| 15 | Bovespa | `BVSP.INDX` | Americas | USD/BRL, commodities |

**Batch 2 — 6 Currencies:**
```
csos action=batch items='[
  {"substrate":"usdjpy","output":"rate:X change:+/-Y pct:+/-Z"},
  {"substrate":"eurusd","output":"rate:X change:+/-Y pct:+/-Z"},
  {"substrate":"gbpusd","output":"rate:X change:+/-Y pct:+/-Z"},
  {"substrate":"usdcny","output":"rate:X change:+/-Y pct:+/-Z"},
  {"substrate":"usdinr","output":"rate:X change:+/-Y pct:+/-Z"},
  {"substrate":"audusd","output":"rate:X change:+/-Y pct:+/-Z"}
]'
```

| # | Currency | eodhd ticker | Couples with |
|---|----------|-------------|-------------|
| 16 | USD/JPY | `USDJPY.FOREX` | Nikkei (inverse) |
| 17 | EUR/USD | `EURUSD.FOREX` | DAX, Euro Stoxx |
| 18 | GBP/USD | `GBPUSD.FOREX` | FTSE |
| 19 | USD/CNY | `USDCNY.FOREX` | Hang Seng, Shanghai |
| 20 | USD/INR | `USDINR.FOREX` | Nifty (inverse) |
| 21 | AUD/USD | `AUDUSD.FOREX` | ASX, commodities |

**Batch 3 — 5 Bridges (trade 24/7, bridge the gaps):**
```
csos action=batch items='[
  {"substrate":"es_futures","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"nq_futures","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"gold","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"oil_wti","output":"price:X change:+/-Y pct:+/-Z"},
  {"substrate":"bitcoin","output":"price:X change:+/-Y pct:+/-Z"}
]'
```

| # | Bridge | eodhd ticker | What it bridges |
|---|--------|-------------|----------------|
| 22 | ES futures (proxy) | `SPY.US` | S&P overnight sentiment |
| 23 | NQ futures (proxy) | `QQQ.US` | NASDAQ overnight sentiment |
| 24 | Gold | `GLD.US` | Risk-off haven, inflation |
| 25 | Oil WTI | `USO.US` | Energy, inflation, India cost |
| 26 | Bitcoin | `BTC-USD.CC` | Pure global sentiment, no hours |

### STEP 2: READ PER-RING PHYSICS

After feeding all 26, read each timezone ring separately:

```
csos ring=eco_organism detail=cockpit    → organism decision (global)
csos action=greenhouse                   → convergences reveal Förster coupling
csos action=muscle                       → motor memory across rings
```

**Per-ring regime detection:**
Each ring has its own regime (early/mid/late/recession). Read from greenhouse convergences:
- Asia ring: nikkei + kospi + hangseng + shanghai + asx convergence pattern
- India ring: nifty50 standalone (partially decoupled)
- Europe ring: dax + cac + eurostoxx + ftse convergence pattern
- US ring: sp500 + nasdaq100 + russell + dowjones convergence pattern

### STEP 3: DETECT THE 6 TIMEZONE GAPS

**Gap 1 — Information Latency:**
```
gradient_gap = gradient(market_open) - gradient(prev_close)
→ csos substrate=latency_gap output="nifty_from_us:+/-X% us_from_asia:+/-Y% europe_from_asia:+/-Z%"
```
Large gradient gap = unprocessed information = free energy = edge.

**Gap 2 — Coupling Asymmetry (who leads whom):**
Read greenhouse convergences. Normally SP500 leads. When leadership FLIPS:
- Shanghai leads → China policy shock
- Nikkei leads → yen carry unwind
- FTSE leads → commodity shock
→ `csos substrate=coupling_leader output="leader:X follower:Y flip:true/false direction:+/-1"`

**Gap 3 — Regime Desynchronization:**
Each ring's regime from convergence patterns:
→ `csos substrate=regime_sync output="asia:early europe:late us:mid india:mid synchronized:false"`

**Gap 4 — Currency as Hidden Variable:**
S&P +1% but USD/JPY -2% = NET -1% for JPY investor.
→ For each ring, compute currency-adjusted return:
→ `csos substrate=fx_adjusted output="asia_raw:+X% asia_fx_adj:+Y% india_raw:+X% india_fx_adj:+Y%"`

**Gap 5 — Liquidity Phase:**
```
overlap_weight:
  London-NYSE overlap (19:00-20:30 IST) = 1.0 (maximum liquidity)
  Solo session = 0.7
  Futures-only (01:30-05:30 IST) = 0.3
→ Weight signals by liquidity phase when computing conviction.
```

**Gap 6 — Earnings Season Geography:**
```
US: Jan/Apr/Jul/Oct
Europe: offset ~2 weeks
Japan: May/Aug/Nov/Feb
India: Apr/Jul/Oct/Jan
→ csos substrate=earnings_season output="us:active europe:pre japan:off india:active"
```

### STEP 4: PLANETARY ALLOCATION

```
For each timezone ring:
  ring_score = Σ(index_gradient × index_direction × (1 - index_F))
  regime_mult = early(1.3) | mid(1.0) | late(0.7) | recession(0.4)
  currency_adj = 1 + coupling(index, fx) × fx_direction
  liquidity_weight = overlap(1.0) | solo(0.7) | futures(0.3)

  allocation = portfolio × (ring_score × regime_mult × currency_adj) / total_score
```

### STEP 5: DIP/PEAK STAGE DETECTION

For each ring, detect which stage of the 6-stage sequence:

| Stage | Detection mechanism | Signal | Action |
|-------|-------------------|--------|--------|
| 1. Macro shift | CausalAtom force change | Cause reverses | REDUCE exposure |
| 2. Institutional selling | AgentTypeAtom: institutional dominant | Large flow | TIGHTEN stops |
| 3. Retail panic | Förster coupling densifies (correlation spike) | Everything moves together | EXIT or HEDGE |
| 4. Overshoot | Marcus error spikes, atoms can't tune | Model breaks | WAIT (noise) |
| 5. Value absorption | Calvin creates atoms at lower levels | New buyers | BEGIN accumulating |
| 6. Gradient reversal | Gradient grows again, Boyer considers EXECUTE | Floor found | ACCUMULATE |

For peaks: mirror stages. Stages 1-2 = reduce. 3-4 = distribute. 5-6 = exit/short.

→ `csos substrate=${RING}_stage output="stage:N signal:X action:REDUCE/TIGHTEN/EXIT/WAIT/ACCUMULATE"`

### STEP 6: REPORT PLANETARY DASHBOARD

```
═══ PLANETARY MEMBRANE — {date} {time IST} ═══
RING TOPOLOGY: Tokyo → Shanghai → Mumbai → London → NYSE → Tokyo
ACTIVE: {which rings are LIVE}
DARK WINDOW: {yes/no}

┌─── INDEXES (15) ────────────────────────────────────┐
│ ASIA:     N225:{X}({%}) KOSPI:{X}({%}) HSI:{X}({%}) │
│           ASHR:{X}({%}) ASX:{X}({%}) TWII:{X}({%})  │
│ INDIA:    NIFTY:{X}({%}) INDIAVIX:{X}               │
│ EUROPE:   DAX:{X}({%}) CAC:{X}({%}) STOXX:{X}({%})  │
│ US:       SPX:{X}({%}) NDX:{X}({%}) RUT:{X}({%})    │
│ AMERICAS: BVSP:{X}({%})                              │
├─── CURRENCIES (6) ──────────────────────────────────┤
│ JPY:{X} EUR:{X} GBP:{X} CNY:{X} INR:{X} AUD:{X}   │
├─── BRIDGES (5) ─────────────────────────────────────┤
│ ES:{X}({%}) NQ:{X}({%}) GOLD:{X}({%})              │
│ OIL:{X}({%}) BTC:{X}({%})                          │
├─── RING ANALYSIS ───────────────────────────────────┤
│ Ring    │ Score │ Regime    │ FX Adj │ Stage │ Alloc │
│ Asia    │ {s}   │ {regime}  │ {adj}  │ {1-6} │ {%}   │
│ India   │ {s}   │ {regime}  │ {adj}  │ {1-6} │ {%}   │
│ Europe  │ {s}   │ {regime}  │ {adj}  │ {1-6} │ {%}   │
│ US      │ {s}   │ {regime}  │ {adj}  │ {1-6} │ {%}   │
├─── GAPS ────────────────────────────────────────────┤
│ Latency:    {gradient gaps between rings}            │
│ Leadership: {who leads — normal or FLIPPED}          │
│ Sync:       {rings synchronized or desynchronized}   │
│ Spillover:  {pattern or "none"}                      │
│ Earnings:   {which regions in season}                │
├─── OVERNIGHT ───────────────────────────────────────┤
│ Gap: {direction} {magnitude}                         │
│ Bridges: ES={trend} BTC={trend} Gold={trend}        │
└─────────────────────────────────────────────────────┘
HYDERABAD EDGE: {what IST timezone position reveals}
NEXT: {auto-branch action}
```

### HYDERABAD GEOGRAPHIC EDGE

IST is the planetary bridge:
- 05:30: See Asia close → absorb → predict NSE direction
- 09:15: NSE opens → position based on Asia absorption
- 13:30: London opens → see NSE + Asia → predict Europe
- 15:30: NSE closes → absorb → predict US via futures
- 19:00: NYSE opens → all three prior zones absorbed
- 01:30: NYSE closes → bridges (BTC, gold) carry through dark window

~18 hours of active market coverage. India's partial decoupling from global
risk-off (domestic consumption, different monetary cycle, demographic dividend)
creates alpha when everything else correlates.

### AUTO-BRANCH

After reporting dashboard:
- Leadership FLIP detected → flag regime change, re-run /rotate
- Dip stage 5-6 in any ring → opportunity, auto-run /focus on that ring's top index
- Peak stage 5-6 in any ring → exit signal for positions in that ring
- Regime desynchronization → overweight early-cycle rings, underweight late
- Overnight gap > 1% → high alert, feed bridges, check VIX
