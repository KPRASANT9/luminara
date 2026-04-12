Feed signal into the membrane for $SUBSTRATE.

**Detect market first**, then route to correct MCP + ticker:

### India / NIFTY / NSE / BSE → use `eodhd` (VERIFIED tickers)

| What user says | eodhd ticker | Feed as |
|---------------|-------------|---------|
| nifty, nifty 50 | `NSEI.INDX` | `csos substrate=nifty50 output="price:X change:+/-Y pct:+/-Z vol:V"` |
| nifty 100 | `CNX100.INDX` | `csos substrate=nifty100 output="price:X change:+/-Y"` |
| sensex, bse | `BSESN.INDX` | `csos substrate=sensex output="price:X change:+/-Y"` |
| bank nifty | `NSEBANK.INDX` | `csos substrate=banknifty output="price:X change:+/-Y"` |
| nifty it | `CNXIT.INDX` | `csos substrate=niftyit output="price:X change:+/-Y"` |
| nifty pharma | `CNXPHARMA.INDX` | `csos substrate=niftypharma output="price:X change:+/-Y"` |

Do NOT use `alphavantage` for India data. Do NOT search/resolve — use exact tickers above.

### US Ticker (AAPL, SPY, MSFT) → try `eodhd` first, `alphavantage` fallback

1. `eodhd` real-time quote for `$SUBSTRATE.US` → extract price, vol, change
2. If eodhd fails: `alphavantage` GLOBAL_QUOTE symbol=$SUBSTRATE
3. `csos substrate=$SUBSTRATE output="price:X vol:Y change:+/-Z pct:+/-P"`

### Market-wide ("the market", "macro", "scan")

Batch-feed via `eodhd` (all verified):
- SPY: `SPY.US`, VIX: `VIX.INDX`, DXY: `UUP.US`
- If India context: add `NSEI.INDX` + `NSEBANK.INDX`

```
csos action=batch items='[
  {"substrate":"SPY","output":"price:X change:+/-Y"},
  {"substrate":"vix","output":"level:X change:+/-Y"},
  {"substrate":"dxy","output":"level:X change:+/-Y"}
]'
```

### Fundamentals (US only)

`financialdatasets` getIncomeStatements/getInsiderTrades/getNews → extract → absorb

### URL

`csos command="curl -sf 'URL'" substrate=auto`

### Raw

`csos substrate=X output="key1:val1 key2:val2"`

After feeding, ALWAYS check: `csos ring=$SUBSTRATE detail=cockpit` → report decision, gradient, speed, F.
