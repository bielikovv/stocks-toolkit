# Volatility-sized grid range and level spacing

**Where:** `src/grid_calculator/grid_calculator.cpp:45` (`calculate_grid_params`, with ATR in `average_true_range` at `src/grid_calculator/grid_calculator.cpp:30`)
**Feeds:** `GridParams` — the suggested price band, the per-level buy/sell prices, and the capital/quantity assigned to each. No caller outside the tests yet; per `README.md`, the planned M3 backtester is the intended consumer.

## What it computes

Three steps, one decision: how wide should the grid be, and where do the lines go.

- **Average True Range (ATR) — how much this asset moves in a typical day.** For each adjacent pair of bars, True Range is `max(high - low, |high - prev_close|, |low - prev_close|)`: the day's own candle spread, unless the price gapped overnight, in which case the gap wins. ATR is the plain mean of those True Range values over the window, `sum(TR_i) / (window.size() - 1)`. A large ATR means wide daily swings; a small ATR means the price barely travels in a day. It is in price units, not a percentage.
- **Range = `current_price ± range_atr_multiplier * ATR`.** The band is sized in units of the asset's own daily movement, so a volatile asset gets a proportionally wider grid than a quiet one at the same price. `current_price` is the most recent close in the window.
- **Geometric (percentage-constant) spacing.** The levels are a geometric sequence from `lower_bound` to `upper_bound`: `ratio = (upper_bound / lower_bound) ^ (1 / (num_levels - 1))`, `level_i = lower_bound * ratio^i`, and `spacing_pct = ratio - 1`. Every adjacent pair of levels is the same *percentage* apart rather than the same number of dollars apart, so a round trip between any two neighbouring lines earns the same percentage return anywhere in the band, instead of a larger one near the bottom and a smaller one near the top. Level 0 lands exactly on `lower_bound` and level `num_levels - 1` exactly on `upper_bound`.

Note this is the *simple-mean* ATR, not the Wilder-smoothed (exponentially recursive) ATR that most charting tools publish under the name "ATR" — same True Range definition, different averaging, so values will not match a chart package exactly.

Capital is a flat split, not part of the math decision: `total_capital / num_levels` per level, and `quantity = capital_per_level / level.price`, so lower levels buy more shares for the same money.

## Why this one

The 14-bar window is the only choice with a stated reason: per `include/stocks_toolkit/grid_calculator/grid_calculator.h:27`, "14 is the traditional ATR lookback used across most trading literature/tools."

The refusal to clamp a non-positive lower bound *is* documented, at `include/stocks_toolkit/grid_calculator/grid_calculator.h:15-19`: a level at or below a zero price is meaningless, so `kInvalidRange` is surfaced "deliberately distinct from a made-up 'safe' answer." The same reasoning extends to `kInvalidVolatility` (a zero/NaN ATR gives no meaningful "typical daily move" to size a range from) and to the 1%-of-price floor on `lower_bound` (`kMinLowerBoundFraction` in `grid_calculator.cpp`) — the exact 1% figure itself is a code review fix, not a backtested value, so treat it the same as the other unvalidated defaults below.

## Inputs → output

In: `PriceBar`s oldest-to-newest (needs at least `atr_window_bars + 1` — the extra bar supplies the "previous close" for the first True Range; only that trailing window is used, older bars ignored) plus a `GridConfig`. Out: `GridParams{status, current_price, atr, lower_bound, upper_bound, spacing_pct, levels}`, levels ordered low to high and populated only when `status == kOk`.

Boundary cases handled explicitly:
- Too few bars → `kInsufficientData`, everything else left at 0.
- ATR is zero, negative, or NaN (a perfectly flat series — e.g. a halted ticker, or a provider forward-filling a holiday with the previous close — or corrupt input data) → `kInvalidVolatility`, `levels` left empty. The check is written as `atr > 0.0` rather than `atr <= 0.0` specifically so a NaN ATR (which compares false to everything) is also caught.
- `lower_bound` is at, below, or within 1% of current_price of zero (a large `range_atr_multiplier * ATR` relative to price) → `kInvalidRange`, `levels` left empty, no clamping. Same NaN-safe comparison form as the ATR check.
- Invalid config (`atr_window_bars` outside `[1, 100000]`, `num_levels < 2`, negative `total_capital`, `range_atr_multiplier <= 0`) → throws `std::invalid_argument`, both from `calculate_grid_params` directly and eagerly from `GridParameterCalculator`'s constructor.

## Tuning knobs

All in `GridConfig`:

- `atr_window_bars` (14, must be in `[1, 100000]`) — how many trailing bars the volatility estimate sees. Shorter tracks a change in volatility faster and is jumpier; longer is steadier but lags. The upper cap is a safety limit, not a strategy choice — it exists purely to keep the internal `+ 1` bar-count arithmetic far from integer overflow.
- `range_atr_multiplier` (2.0) — half-width of the band in ATRs. Larger covers more price action but spreads the levels further apart; smaller concentrates them and raises the chance price exits the band. Large values are what push `lower_bound` toward (or below) `kInvalidRange`'s 1%-of-price floor.
- `num_levels` (10, must be >= 2) — lines across the band. More levels means smaller `spacing_pct` (more frequent, smaller round trips) and less capital per level.
- `total_capital` (0.0) — split evenly across levels; affects allocation only, not level prices.

Per `include/stocks_toolkit/grid_calculator/grid_calculator.h:22-24`, these defaults are explicitly "reasonable starting points, not backtested-optimal values" — the planned M3 backtester is what's expected to validate and tune them, not this module.
