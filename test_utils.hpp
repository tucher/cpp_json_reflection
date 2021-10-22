#ifndef TEST_UTILS_HPP
#define TEST_UTILS_HPP
#include <iostream>
#include <chrono>

template<typename F, typename... Args>
void doPerformanceTest(std::string title, std::size_t count, F func, Args&&... args){
    std::chrono::high_resolution_clock::time_point t1=std::chrono::high_resolution_clock::now();
    for(std::size_t i = 0; i < count; i++) {
        func(std::forward<Args>(args)...);
    }
    double toUs = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now()-t1).count();
    double res = toUs / double(count);
    std::cerr << title << ": " << res << " us (" <<  count << " runs took " << toUs << " us)" << std::endl << std::flush;
}


#endif // TEST_UTILS_HPP
