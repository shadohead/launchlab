#include "../LaunchLabMini/dark_display.h"
#include <cassert>
#include <iostream>
int main() {
  DarkDisplay d;
  // Lit until asked, and a press then does what it always did.
  assert(!d.dark(0));assert(!d.press(0));assert(!d.dark(1));
  d.set(true);assert(d.dark(100));
  // The press that wakes it does nothing else; the next one in the window counts.
  assert(d.press(1000));assert(!d.dark(1000));
  assert(!d.press(5000));
  // A press in the window starts it again.
  assert(!d.dark(5000+DarkDisplay::WAKE_MS-1));assert(d.dark(5000+DarkDisplay::WAKE_MS));
  d.tick(5000+DarkDisplay::WAKE_MS);assert(!d.woken);
  assert(d.press(20000));
  // Ending it takes the window with it, and asking again starts dark.
  d.set(false);assert(!d.dark(20001));assert(!d.press(20002));
  d.set(true);assert(d.dark(20003));
  // Across the clock's wrap.
  assert(d.press(0xFFFFFFF0u));assert(!d.dark(5));assert(d.dark(0xFFFFFFF0u+DarkDisplay::WAKE_MS));
  std::cout<<"dark display tests passed\n";
}
