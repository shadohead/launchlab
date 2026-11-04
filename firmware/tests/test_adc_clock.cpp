// ADC timing from the APB clock the ADC runs at (adc_clock.h): 5 MHz
// controller and 50 interval cycles at 80 and 40 MHz, nothing invalid.
#include "../LaunchLabMini/adc_clock.h"
#include <cassert>
#include <iostream>

int main() {
  constexpr auto at80=AdcClock::timing(80000000,50000),at40=AdcClock::timing(40000000,50000);
  static_assert(at80.valid && at80.divider==15 && at80.interval==50,"IDF's own 80 MHz timing");
  static_assert(at40.valid && at40.divider==7 && at40.interval==50,"40 MHz keeps the 5 MHz controller");
  // IDF's formula at a 40 MHz configure gives interval 25: at 80 MHz that is 100 kS/s.
  static_assert(80000000/(15+1)/2/25==100000,"the double rate a stale configure gives");
  for(uint32_t apb:{80000000u,40000000u,20000000u,10000000u}) {
    const auto t=AdcClock::timing(apb,50000);
    assert(t.valid && apb/(t.divider+1)/2/t.interval==50000);
  }
  assert(!AdcClock::timing(0,50000).valid && !AdcClock::timing(4000000,50000).valid);
  assert(!AdcClock::timing(80000000,0).valid && !AdcClock::timing(80000000,30000).valid);
  assert(!AdcClock::timing(42000000,50000).valid && !AdcClock::timing(2000000000,50000).valid);
  assert(!AdcClock::timing(80000000,600).valid);  // interval over 12 bits
  std::cout<<"PASS: ADC timing keeps 5 MHz and 50 kS/s at 80 and 40 MHz APB\n";
}
