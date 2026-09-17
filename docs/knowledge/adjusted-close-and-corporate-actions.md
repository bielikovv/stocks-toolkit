# Adjusted close and corporate actions

## Why raw close isn't enough

`close` is the price the stock actually traded at. It's what you'd see on a chart, but it
understates long-run returns for any stock that pays dividends, because a $1 dividend payout
drops the close by roughly $1 on the ex-dividend date without the shareholder losing $1 of value —
they received it as cash instead. Sum enough of those drops over a decade and a dividend payer's
raw-close return looks meaningfully worse than what an investor who reinvested (or even just kept)
the dividends actually earned. Splits cause a similar distortion in the other direction: a 10-for-1
split drops the close to a tenth of its prior value overnight with no change in the position's
worth at all.

`adj_close` is Yahoo's back-adjusted series that corrects for both, and it's what any return or
performance comparison in this codebase must be computed from. `close` is kept alongside it purely
so charts and the corporate-action audit trail below show the price that actually traded.

## Fallback behavior

Yahoo doesn't always return an adjusted value for a given bar (and never does for penny/OTC-style
tickers in practice). When that happens, `PriceBar::adj_close` falls back to the raw `close` for
that bar rather than dropping it — a small, explicit inaccuracy is better than losing the whole
day's data. This fallback happens per-bar in `yahoo_parser.cpp`, not per-response, so a single
missing value doesn't force a fallback for the ticker's entire history.

## The corporate_actions table

Rather than trusting the adjustment silently, every dividend and split Yahoo reports for a ticker
is stored verbatim in `corporate_actions` (`ticker`, `date`, `type`, `amount` for dividends,
`split_numerator`/`split_denominator` for splits — e.g. a 10-for-1 split has numerator=10,
denominator=1). This exists so a sudden jump in `adj_close` relative to `close` is traceable to a
specific, inspectable event instead of being an unexplained discrepancy.
