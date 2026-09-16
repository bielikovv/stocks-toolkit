# stocks-toolkit

Analyzes historical price data for stocks and crypto to detect volatility regimes, range vs. trend
behavior, and correlation to BTC crashes — helping decide if and how to run range/grid strategies.
Pure price-history math, no news, no sentiment. Validated through backtesting.

Not financial advice. No guarantee of accuracy or profitability.

## Status

- **M0 (data layer):** fetches daily historical price bars from the
  [Yahoo Finance chart API](https://query1.finance.yahoo.com) for a configurable list of tickers
  and stores them in SQLite.
- **M1 (regime detector):** classifies an asset's trailing price window as range-bound, trending,
  or uncertain, using Kaufman's Efficiency Ratio (trend strength) combined with a volatility-scaled
  containment check (has price wandered further than its own recent volatility would predict).
  Thresholds are unvalidated defaults — M3 (backtester) is what's expected to tune them against
  real outcomes.

Later modules (backtesting, correlation analysis, reporting) will build on top of these.

## Build

Requires: CMake 3.21+, Ninja, a C++20 compiler.

```sh
git submodule update --init  # first time only, fetches vcpkg
./vcpkg/bootstrap-vcpkg.sh
cmake --preset default
cmake --build --preset default
```

## Run

```sh
./build/stocks_toolkit_cli config/tickers.txt data/prices.db
```

Fetches history for every ticker listed in `config/tickers.txt` (one Yahoo Finance symbol per
line, e.g. `NVDA`) into the SQLite database at the given path. Re-running only fetches bars newer
than what's already stored.

## Test

```sh
ctest --test-dir build
```
