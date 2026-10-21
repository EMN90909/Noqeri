#include <cstdint>
extern "C" std::int64_t noqeri_program_entry(void);
int main() { return static_cast<int>(noqeri_program_entry()); }
