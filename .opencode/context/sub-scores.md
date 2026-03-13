# HAPOS Sub-Scores — Agent Context

## Energy (E) — Thermodynamic Capacity
- Physics: Negentropy production rate. Prigogine + Kuramoto.
- Equation: E = w_e1·φ_hrv + w_e2·φ_rhr + w_e3·φ_spo2 + w_e4·φ_temp + w_e5·φ_circ + w_e6·φ_gluc
- Φ Partition: Φ_E[~20 dims]
- Models: Kronauer (circadian), Banister (fitness/fatigue), Bergman (glucose)
- Primary Signals: IBI, SpO2, Skin temp, Glucose, Ambient light
- Agent: Sleep/Circadian Agent + Metabolic Agent

## Recovery (R) — Control-Theoretic Stability
- Physics: PID control gain + McEwen allostatic load (inverse).
- Equation: R = w_r1·φ_hrv_recovery + w_r2·φ_sleep + w_r3·φ_load + w_r4·φ_temp_rebound + w_r5·φ_report
- Φ Partition: Φ_R[~15 dims]
- Models: McEwen (allostatic), PID controller, Banister, Kronauer (sleep)
- Primary Signals: IBI (post-stress), Sleep staging, HRV/HR/Glucose trends, Skin temp, Energy self-report
- Agent: Recovery Agent + Sleep/Circadian Agent

## Cognitive (C) — Predictive Precision (Reformulated)
- Physics: Neurovisceral integration. vmHRV = peripheral readout of prefrontal predictive precision.
- Equation: C = w_c1·π_cog + w_c2·η_cog − w_c3·ε_cog
  - π_cog: Prediction precision = vmHRV (RMSSD, HF) relative to personal optimum
  - η_cog: Model update efficiency = REM density + HAPOS model improvement rate
  - ε_cog: Prediction error load = daytime HRV deviation below 30-day baseline
- Φ Partition: Φ_C[~8 dims]
- Primary Signals: IBI (frequency-domain), Sleep staging (REM), HAPOS prediction log, Clarity self-report
- Agent: Cognitive Agent

## Agency (A) — Policy Effectiveness (Reformulated)
- Physics: Active inference. Expected free energy decomposition.
- Equation: A = w_a1·γ_eff + w_a2·γ_epi + w_a3·(1 − γ_H)
  - γ_eff: Policy effectiveness = prediction-outcome alignment from HAPOS prediction log
  - γ_epi: Epistemic drive = information gain rate from agent's choices
  - γ_H: Behavioral entropy = Shannon entropy of daily timing patterns (inverted)
- Φ Partition: Φ_A[~8 dims]
- Primary Signals: HAPOS prediction log, Accel + Location, Energy self-report
- Agent: Agency Agent

## HAP Synthesis
HAP = (w_e·E + w_r·R + w_c·C + w_a·A) × coupling_factor + trajectory_adj
- Weights: w_e=0.30, w_r=0.30, w_c=0.20, w_a=0.20 (Loop 3 adjustable)
- Coupling: coupling_factor = Σ[π_i·Κ_i] / Σ[π_i]
- Gradient: ∇HAP = [∂HAP/∂Φ_E, ∂HAP/∂Φ_R, ∂HAP/∂Φ_C, ∂HAP/∂Φ_A]
