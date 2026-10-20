// Prints the web milestone editor's test vectors (fixtures/milestone_vectors.json,
// copied to beywatch tests/launchlab/fixtures/milestone-vectors.json).
// Build like the native suites, then: ./milestone_fixture > fixtures/milestone_vectors.json
#include "milestone_vectors.h"
#include <cstdio>
int main(){std::fputs(MilestoneVectors::json().c_str(),stdout);}
