#ifndef ANTON_ASSERT_LIB_DEBUGGING_HPP_INCLUDE
#define ANTON_ASSERT_LIB_DEBUGGING_HPP_INCLUDE

#include <cstdint>
#include <string>

[[nodiscard]] std::string get_type_as_string(void* process_handle, uint64_t module_base, uint64_t index);

#endif // !ANTON_ASSERT_LIB_DEBUGGING_HPP_INCLUDE
