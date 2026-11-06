#include <iostream>
#if __has_include(<httplib.h>)
#include <httplib.h>
#endif
int main() {
  std::cout << "gt_api_test: hello from VS2022/MSVC" << std::endl;
#if __has_include(<httplib.h>)
  std::cout << "cpp-httplib linked: " << CPPHTTPLIB_VERSION << std::endl;
#endif
  return 0;
}
