// Prints the web theme editor's test vectors (fixtures/theme_vectors.json,
// copied to beywatch tests/launchlab/fixtures/theme-vectors.json).
// Build like the native suites, then: ./theme_fixture > fixtures/theme_vectors.json
#include "theme_vectors.h"
#include <cstdio>
int main(){std::fputs(ThemeVectors::json().c_str(),stdout);}
