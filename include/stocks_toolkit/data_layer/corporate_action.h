#pragma once

#include <string>

namespace stocks_toolkit {

enum class CorporateActionType { kDividend, kSplit };

// One dividend payment or stock split for a ticker, as reported by the data
// provider. `date` is ISO-8601 "YYYY-MM-DD". Only the fields relevant to
// `type` are populated — amount for a dividend, split_numerator/denominator
// for a split (e.g. a 4-for-1 split has numerator=4, denominator=1).
struct CorporateAction {
    std::string date;
    CorporateActionType type = CorporateActionType::kDividend;
    double amount = 0.0;
    double split_numerator = 0.0;
    double split_denominator = 0.0;

    bool operator==(const CorporateAction&) const = default;
};

}  // namespace stocks_toolkit
