#include "stdafx.h"

namespace uih::literals::spx {

int operator"" _spx(unsigned long long px)
{
    return scale_dpi_value(gsl::narrow<int>(px));
}

} // namespace uih::literals::spx
