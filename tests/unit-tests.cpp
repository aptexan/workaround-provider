// Unit tests of the provider's core: the value model and its encodings (cty), and the resource rules
// (workaround-example). No gRPC and no OpenTofu: see tools/check-unit.sh. The end-to-end checks with the real
// OpenTofu are tools/check-data.sh.
//   --skip-negative   leave out the checks that expect an error (bad input, invalid configuration)

#include <cstring>

#include "check.h"

void RunCtyTests();
void RunExampleTests();

int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--skip-negative") == 0) {
      g_skip_negative = true;
    } else {
      std::cerr << "unknown option: " << argv[i] << " (the only option is --skip-negative)\n";
      return 2;
    }
  }
  if (g_skip_negative) std::cout << "== negative checks are skipped (--skip-negative)\n";
  RunCtyTests();
  RunExampleTests();
  return Summary();
}
