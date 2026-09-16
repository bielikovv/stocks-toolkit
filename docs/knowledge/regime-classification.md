# Range vs. trend classification

**Where:** `src/regime_detector/regime_detector.cpp:11` (`classify_regime`)
**Feeds:** `RegimeResult.regime` — the range/trending/uncertain/insufficient-data label meant to decide whether a range/grid strategy is appropriate for an asset right now. No caller outside the tests exists yet; per `README.md`, the planned M3 backtester is the intended consumer.

## What it computes

Two independent signals over a trailing window of daily closes (`window_bars` closes, so `window_bars - 1` day-over-day changes), each answering a different question, then combined:

- **Kaufman's Efficiency Ratio (trend strength)** — `ER = |last_close - first_close| / sum(|close_i - close_i-1|)`. The numerator is how far price actually ended up net; the denominator is the total distance it travelled getting there. A straight, one-direction move gives `ER = 1`. A move that zig-zags back and forth but ends near where it started gives `ER` close to `0`. It measures *directness*, not speed.
- **Volatility-scaled containment ratio (range check)** — `containment = actual_range / expected_range`, where `actual_range = max(close) - min(close)` over the window (closes only — the bars' high/low fields are not used) and `expected_range = stdev(daily_changes) * sqrt(n_changes)`. The `stdev * sqrt(n)` term is the standard random-walk scaling: a walk of `n` independent steps of size `stdev` spreads out by roughly `stdev * sqrt(n)`, so it estimates how far price should have wandered on its own noise alone. `stdev` here is the population form (sum of squared deviations divided by `n`, not `n - 1`). `containment ≈ 1` means price moved about as far as its own day-to-day noise predicts — normal chop. Much greater than `1` means it travelled further than volatility alone explains — a real directional move, not noise.

The two are cross-checked, not averaged, and both must agree: choppy-by-ER **and** contained → Range. Trending-by-ER **and** a containment breakout → Trending. Anything else, including either signal landing in the dead zone between its two thresholds → Uncertain.

## Why this one

`Regime::kUncertain` exists as a deliberate third outcome, not a fallback: per the header comment at `include/stocks_toolkit/regime_detector/regime_detector.h:11`, "a false 'confident' read is worse than an honest 'don't know'" — so disagreement between the two signals is surfaced rather than forced into Range or Trending. Three tests in `tests/regime_detector_test.cpp` exist purely to pin this down, each asserting that a disagreement case must not be "silently resolved to Trending" or forced into Range.

Why Kaufman's Efficiency Ratio and this particular volatility-scaled containment check were picked over alternatives: not documented in code or history — confirm with the author before relying on this as intentional. (The code is untracked/uncommitted, so there are no commit messages for it.)

## Inputs → output

In: a window of `PriceBar`s (needs at least `config.window_bars`, oldest-to-newest; only the trailing `window_bars` are used, anything older is ignored) and thresholds. Out: `RegimeResult{regime, efficiency_ratio, containment_ratio}` — `efficiency_ratio` in `[0, 1]`, `containment_ratio` in `[0, +inf]`.

Degenerate cases handled explicitly in code:
- Fewer bars than the window → `Regime::kInsufficientData`, ratios left at 0.
- `window_bars < 2` → throws `std::invalid_argument` (can't form a single day-over-day change).
- Zero total price movement (`sum_abs_changes == 0`) → `efficiency_ratio = 0` instead of `0/0`.
- Zero variance in daily changes (`expected_range == 0`, e.g. perfectly uniform steps) → `containment_ratio` is `+infinity` if price moved at all, `0` if it didn't — avoids a `NaN` from dividing by zero.

## Tuning knobs

All in `RegimeConfig`:

- `window_bars` (20) — how many trailing closes the whole analysis sees. Shorter reacts faster and is noisier; longer smooths but lags.
- `efficiency_ratio_range_threshold` (0.3) / `efficiency_ratio_trend_threshold` (0.5) — the "choppy" ceiling and the "trending" floor. Widening the gap between them sends more windows to Uncertain; narrowing it forces more into Range/Trending.
- `containment_range_threshold` (2.0) / `containment_trend_threshold` (3.0) — the same pair for the containment signal: at or below 2.0 counts as contained, at or above 3.0 as breakout, in between is neither.

Per `include/stocks_toolkit/regime_detector/regime_detector.h:22-24`, all five are explicitly marked as unvalidated starting points, not backtested-optimal values — the planned M3 backtester module is what's expected to tune them against real outcomes, not this code.
