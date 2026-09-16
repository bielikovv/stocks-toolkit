# Math knowledge

Plain-language explanations of the non-trivial math in this codebase: what each formula computes, why it was chosen, and how it fits the architecture. Written for anyone reading the code who doesn't want to reverse-engineer the reasoning from the formula alone.

- [Range vs. trend classification](regime-classification.md) — Kaufman's Efficiency Ratio (trend strength) cross-checked against a volatility-scaled containment ratio (has price moved further than its own noise predicts) to label an asset's window as range, trending, or uncertain.
- [Volatility-sized grid range and level spacing](atr-grid-sizing.md) — Average True Range (simple mean, not Wilder-smoothed) sets the width of a grid's price band, and geometric spacing places the levels so every adjacent pair is the same percentage apart.
