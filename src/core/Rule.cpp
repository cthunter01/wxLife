#include "wxLife/core/Rule.h"

#include <string>

namespace wxLife::core
{

std::string Rule::toString() const
{
    std::string text;
    const auto  appendCounts = [&text](Mask mask) {
        for (unsigned n = 0; n <= 8; ++n)
        {
            if ((unsigned{mask} >> n) & 1U)
            {
                text += static_cast<char>('0' + n);
            }
        }
    };
    text += 'B';
    appendCounts(birth_);
    text += "/S";
    appendCounts(survival_);
    return text;
}

}  // namespace wxLife::core
