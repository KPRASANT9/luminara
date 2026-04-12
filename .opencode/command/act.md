Execute on $SUBSTRATE. Boyer MUST say EXECUTE. Direction from /focus synthesis.

1. **Gate check**: `csos ring=$SUBSTRATE detail=cockpit`
   decision MUST = EXECUTE. If EXPLORE → STOP. Say what's missing.

2. **Direction**: from /focus synthesis or regime+causal+momentum.
   Must be LONG or SHORT with at least MODERATE consensus.
   If direction unclear → STOP. "Evidence supports action but direction is ambiguous."

3. **Sizing** (from F):
   `conviction = 1.0 - F_free_energy` from cockpit
   `base_size = buying_power × 0.02` (2% risk default)
   `position_size = base_size × conviction`

4. **Stop** (from resonance width):
   Read atom spread from cockpit.
   LONG: stop = entry - spread. SHORT: stop = entry + spread.

5. **Present thesis for human confirmation**:
   ```
   DIRECTION: LONG/SHORT $SUBSTRATE
   EVIDENCE: gradient=G speed=S F=F regime=R consensus=C
   SIZE: N shares at $P = $TOTAL (conviction: X%)
   STOP: $STOP (resonance width: rw)
   ```

6. **Execute** (on human confirmation):
   `alpaca submit_order symbol=$SUBSTRATE qty=N side=buy/sell type=limit limit_price=P`
   `alpaca submit_order symbol=$SUBSTRATE qty=N side=sell/buy type=stop stop_price=STOP`

7. **MANDATORY feedback** (non-negotiable):
   `csos substrate=${SUBSTRATE}_exec output="side:BUY/SELL fill:P slip:S impact:I"`
   Track: `csos substrate=${SUBSTRATE}_pnl output="entry:P current:C pnl:+/-X%"`

8. **Monitor**: periodic /feed ${SUBSTRATE} → if Boyer flips to EXPLORE → close.
   On close: `csos substrate=${SUBSTRATE}_pnl output="entry:P exit:E pnl:+/-X% duration:Nd"`
   Auto-tune: CausalAtom counterfactual updates. Profitable chains strengthen.
