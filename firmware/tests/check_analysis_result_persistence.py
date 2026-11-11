"""Execute the production optional-result save helper with NVS faults."""
from pathlib import Path
import os,subprocess,tempfile
root=Path(__file__).resolve().parents[1]
source=(root/'LaunchLabMini/LaunchLabMini.ino').read_text()
start=source.index('static bool saveAnalysisResult()');end=source.index('{',start)+1;depth=1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}');end+=1
prefix=r"""
#include "decay_record.h"
#include "nvs_budget.h"
#include <cassert>
#include <cstring>
#include <iostream>
struct {bool pending=true;Decay::Record::Image bytes{};bool recordPending(){return pending;}
 const Decay::Record::Image &record(){return bytes;}void recordHandled(){pending=false;}}analysisResults;
struct {bool exists=false,fail=false,corrupt=false;unsigned writes=0;Decay::Record::Image stored{};
 bool isKey(const char*){return exists;}
 size_t putBytes(const char*,const void *p,size_t n){++writes;if(fail)return 0;std::memcpy(stored.data(),p,n);exists=true;return n;}
 size_t getBytes(const char*,void *p,size_t n){std::memcpy(p,stored.data(),n);if(corrupt)static_cast<uint8_t*>(p)[0]^=1;return n;}
}prefs;
struct PracticeHistory{static constexpr unsigned IMAGE_BYTES=2600;};
static unsigned available=0;static bool nvsAvailable(unsigned &n){n=available;return true;}
struct {bool ok=false;void printf(const char*,bool saved,unsigned,unsigned,unsigned){ok=saved;}}Serial;
"""
tests=r"""
int main(){
 Decay::Results results;Decay::Record::encode(results,2,analysisResults.bytes);
 assert(saveAnalysisResult() && !analysisResults.pending && !prefs.writes && !Serial.ok);
 available=1000;analysisResults.pending=true;assert(saveAnalysisResult() && prefs.writes==1 && Serial.ok && !analysisResults.pending);
 prefs.fail=true;analysisResults.pending=true;assert(saveAnalysisResult() && !Serial.ok && !analysisResults.pending);
 prefs.fail=false;prefs.corrupt=true;analysisResults.pending=true;assert(saveAnalysisResult() && !Serial.ok && !analysisResults.pending);
 unsigned writes=prefs.writes;assert(saveAnalysisResult() && prefs.writes==writes);
 std::cout<<"PASS: production result save, history reserve, read-back verification and optional failure handling\n";
}
"""
with tempfile.TemporaryDirectory(prefix='analysis-save-') as directory:
    cpp=Path(directory)/'test.cpp';exe=Path(directory)/'test';cpp.write_text(prefix+source[start:end]+tests)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-I'+str(root/'LaunchLabMini'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
