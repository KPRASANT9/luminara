# HAPOS Resilience Patterns — Agent Context

1. **Circuit Breaker**: External API 5xx×3 → open 5min → retry. (Allostatic: shed load)
2. **Graceful Degradation**: Missing signal → neutral (0.5), widen CI. Never fabricate. (Law I)
3. **Model Rollback**: I7 fails → revert to previous version. (Self-correction)
4. **Precision Reset**: I8 fails → Π to neutral weights. (Bad meta > no meta)
5. **Invariant Gate**: Any invariant fails in CI → block merge. (Laws don't break)
6. **Canary Deploy**: 5% traffic 24h → compare metrics → promote or rollback. (Temporal conservation)
7. **Self-Report Rescue**: 7+ days no reports → reduce C/A confidence, gentler prompts. (No coercion)
8. **Cold Start Bootstrap**: Day 1 → population priors, wide CIs. (Honesty > false precision)
9. **Cross-Domain Arbitration**: Agents conflict → MC simulate → Pareto → user chooses. (Agency)
10. **Federated Fail-Safe**: Bad federated round → reject, keep previous prior. (Privacy always)
