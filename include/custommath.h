#pragma once

#include <stdexcept>

namespace custmath {

    constexpr unsigned long long factorial(unsigned int n) {
        if (n > 20) { 
            throw std::overflow_error("Factorial value too large for unsigned long long");
        }
    
        unsigned long long result = 1;
        for (unsigned int i = 2; i <= n; ++i) {
            result *= i;
        }
        return result;
    }
}