add_executable(gdc_calibrate calibrate/main.cpp)
target_link_libraries(gdc_calibrate PRIVATE gdc_core)
target_compile_options(gdc_calibrate PRIVATE -Wall -Wextra -Wpedantic)

add_executable(gdc_bench bench/main.cpp)
target_link_libraries(gdc_bench PRIVATE gdc_core)
target_compile_options(gdc_bench PRIVATE -Wall -Wextra -Wpedantic)
