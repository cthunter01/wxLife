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
    appendCounts(m_birth);
    text += "/S";
    appendCounts(m_survival);
    return text;
}

}  // namespace wxLife::core
