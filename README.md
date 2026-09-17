# stocks-toolkit

A historical stock analytics toolkit. It reports facts and statistical context about a stock's
price history — performance ranking, relative strength, drawdown depth, historical analogs — never
a prediction of where the price goes next. Pure price/volume math, no fundamentals, no news, no
sentiment. Every claim is meant to be validated against real outcomes before being trusted, not
asserted from a hardcoded rule.

Not financial advice. No guarantee of accuracy or profitability.

## Status

- **M0 (data layer):** fetches daily historical price bars, dividend/split-adjusted close, and
  corporate action history (dividends, splits) from the
  [Yahoo Finance chart API](https://query1.finance.yahoo.com) for a configurable ticker universe,
  and stores them in SQLite. Tickers can be flagged as benchmarks (e.g. SPY) so later modules can
  find "the market" without a hardcoded name.

Later modules (regime/relative-strength classification, drawdown analysis, historical analog
matching, backtesting) will build on top of M0. None are implemented yet.

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
./build/stocks_toolkit_cli config/tickers.txt config/benchmarks.txt data/prices.db
```

Fetches history for every ticker in `config/tickers.txt` plus every benchmark in
`config/benchmarks.txt` (one Yahoo Finance symbol per line, `#` for comments) into the SQLite
database at the given path. Re-running only fetches bars newer than what's already stored. Both
config paths and the db path are optional and default to the paths above.

## Test

```sh
ctest --test-dir build
```
