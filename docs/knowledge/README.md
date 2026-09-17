# Math knowledge

Plain-language explanations of the non-trivial math in this codebase: what each formula computes, why it was chosen, and how it fits the architecture. Written for anyone reading the code who doesn't want to reverse-engineer the reasoning from the formula alone.

- [Adjusted close and corporate actions](adjusted-close-and-corporate-actions.md) — why raw `close` understates returns for dividend-paying stocks, how `adj_close` and the `corporate_actions` table fix that, and the per-bar fallback when Yahoo doesn't supply an adjusted value.
