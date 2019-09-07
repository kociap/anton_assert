#ifndef ANTON_ASSERT_LIB_ANTON_ASSERT_HPP_INCLUDE
#define ANTON_ASSERT_LIB_ANTON_ASSERT_HPP_INCLUDE

void anton_assert(char const* message, char const* file, unsigned long long line);

#ifndef NDEBUG
#    define ANTONIO_ASSERT(condition, msg) (static_cast<bool>(condition) ? (void)0 : anton_assert(msg, __FILE__, __LINE__))
#else
#    define ANTONIO_ASSERT(condition, msg) ((void)0)
#endif

#endif // !ANTON_ASSERT_LIB_ANTON_ASSERT_HPP_INCLUDE
