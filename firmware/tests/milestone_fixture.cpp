// Prints the web milestone editor's test vectors (fixtures/milestone_vectors.json,
// copied to beywatch tests/launchlab/fixtures/milestone-vectors.json), or with
// `effect-set` the effect set editor's (fixtures/effect_set_vectors.json,
// copied to beywatch tests/launchlab/fixtures/effect-set-vectors.json).
// Build like the native suites, then: ./milestone_fixture > fixtures/milestone_vectors.json
// and ./milestone_fixture effect-set > fixtures/effect_set_vectors.json
#include "effect_set_vectors.h"
#include "milestone_vectors.h"
#include <cstdio>
#include <cstring>
int main(int argc,char **argv){
  const bool effectSet=argc>1 && !std::strcmp(argv[1],"effect-set");
  std::fputs((effectSet?EffectSetVectors::json():MilestoneVectors::json()).c_str(),stdout);
}
