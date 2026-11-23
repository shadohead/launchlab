/-!
# WipeClock: a Lean 4 proof pilot for `uint32_t(now-then)` in `wipe_confirmation.h`

Read first: `firmware/LaunchLabMini/wipe_confirmation.h` and
`firmware/tests/test_wipe_confirmation.cpp` (LaunchLab main at 8d063e41).

Target: Lean 4.30.0, core library only. No imports, no Lake project, no mathlib.
Run: `lean -DwarningAsError=true firmware/formal/WipeClock.lean`.

What is proved here (all statements are about Lean's own `UInt32`):

1. For any earlier reading and any true gap in milliseconds, the wrapped
   unsigned difference `now - earlier` equals the gap modulo 2^32.
   For a gap below 2^32 it equals the gap itself.
2. The bound is tight: at 2^32 ms and beyond the difference never equals the
   gap, and a gap of exactly 2^32 ms reads as 0.
3. Under the same reading hypothesis the three comparisons of the header agree
   with the true gap: timeout at 30000 ms inclusive, hold complete at 3000 ms
   inclusive, poll gap too long strictly above 250 ms.
4. A hand transcription of `tick()` cancels first: B down or the deadline
   reached gives no event, also on the tick that would complete the hold.

What is NOT proved here: that this model equals the compiled C++, that
`millis()` reads true time, or anything about a sequence of ticks.
`begin()`, `confirm()` and `elapsed()` are not modelled.

Every proof below is complete; the closing `#print axioms` commands let the
compiler report what each main theorem depends on.
-/

set_option autoImplicit false

namespace LaunchLab.WipeClock

/-! ## 0. Bridge to Lean's `UInt32`

These six lemmas are the only place that depends on how the core library
defines `UInt32` (in 4.19.0: a wrapper around `BitVec 32`). Each one tries a
definitional proof first and then the core lemma of the same content, so a
library rename shows up here and nowhere else. Everything after this section
uses only these lemmas, `omega` and `simp`.

The number 4294967296 is 2^32, written out so that `omega` sees a literal.
-/

theorem two_pow_32 : (2 : Nat) ^ 32 = 4294967296 := by
  first
    | rfl
    | decide

/-- A `UInt32` read as a natural number is below 2^32. -/
theorem u32_toNat_lt (a : UInt32) : a.toNat < 4294967296 := by
  first
    | exact a.toBitVec.isLt
    | exact a.toBitVec.toFin.isLt
    | exact UInt32.toNat_lt a
    | (have h : a.toNat < 2 ^ 32 := a.toBitVec.isLt; omega)

/-- `UInt32` subtraction wraps modulo 2^32, as C++ `uint32_t` subtraction does. -/
theorem u32_toNat_sub (a b : UInt32) :
    (a - b).toNat = (4294967296 - b.toNat + a.toNat) % 4294967296 := by
  first
    | rfl
    | exact UInt32.toNat_sub a b
    | (simp <;> omega)

/-- `UInt32` addition wraps modulo 2^32. -/
theorem u32_toNat_add (a b : UInt32) :
    (a + b).toNat = (a.toNat + b.toNat) % 4294967296 := by
  first
    | rfl
    | exact UInt32.toNat_add a b
    | (simp <;> omega)

/-- Truncating a natural number to `UInt32` keeps its residue modulo 2^32. -/
theorem u32_toNat_ofNat (n : Nat) : (UInt32.ofNat n).toNat = n % 4294967296 := by
  first
    | rfl
    | exact UInt32.toNat_ofNat'
    | exact UInt32.toNat_ofNat
    | (simp <;> omega)

/-- Unsigned `<=` on `UInt32` is `<=` on the values. -/
theorem u32_le_iff (a b : UInt32) : a ≤ b ↔ a.toNat ≤ b.toNat := by
  first
    | exact Iff.rfl
    | exact UInt32.le_iff_toNat_le
    | simp [UInt32.le_iff_toNat_le]

/-- Unsigned `<` on `UInt32` is `<` on the values. -/
theorem u32_lt_iff (a b : UInt32) : a < b ↔ a.toNat < b.toNat := by
  first
    | exact Iff.rfl
    | exact UInt32.lt_iff_toNat_lt
    | simp [UInt32.lt_iff_toNat_lt]

/-! ## 1. The clock theorem

`ReadsAfter earlier now gap` is the sampling assumption, stated once: a
free-running 32-bit millisecond counter that read `earlier` reads `now` after
`gap` true milliseconds. `gap` is an unbounded natural number. The initial
value `earlier` is arbitrary, so every phase of the counter is covered.
-/

def ReadsAfter (earlier now : UInt32) (gap : Nat) : Prop :=
  now.toNat = (earlier.toNat + gap) % 4294967296

/-- The assumption holds for the host test helper `after(start,delta)`,
which is `uint32_t(start+delta)`. -/
theorem readsAfter_add (start : UInt32) (gap : Nat) :
    ReadsAfter start (start + UInt32.ofNat gap) gap := by
  have hadd := u32_toNat_add start (UInt32.ofNat gap)
  have hof := u32_toNat_ofNat gap
  show (start + UInt32.ofNat gap).toNat = (start.toNat + gap) % 4294967296
  omega

/-- The assumption holds for a counter that is true time truncated to 32 bits:
readings taken at true times `t` and `t + gap`. -/
theorem readsAfter_ofNat (t gap : Nat) :
    ReadsAfter (UInt32.ofNat t) (UInt32.ofNat (t + gap)) gap := by
  have h0 := u32_toNat_ofNat t
  have h1 := u32_toNat_ofNat (t + gap)
  show (UInt32.ofNat (t + gap)).toNat = ((UInt32.ofNat t).toNat + gap) % 4294967296
  omega

/-- Exact correspondence, no bound on the gap: `uint32_t(now-earlier)` is the
true gap modulo 2^32. -/
theorem wrapped_difference_eq_gap_mod (earlier now : UInt32) (gap : Nat)
    (h : ReadsAfter earlier now gap) :
    (now - earlier).toNat = gap % 4294967296 := by
  have hnow : now.toNat = (earlier.toNat + gap) % 4294967296 := h
  have hsub := u32_toNat_sub now earlier
  have hlt := u32_toNat_lt earlier
  omega

/-- Main result: for any initial reading and any true gap below 2^32 ms, the
wrapped unsigned difference recovers the true gap exactly. -/
theorem wrapped_difference_recovers_gap (earlier now : UInt32) (gap : Nat)
    (hgap : gap < 4294967296) (h : ReadsAfter earlier now gap) :
    (now - earlier).toNat = gap := by
  have hmod := wrapped_difference_eq_gap_mod earlier now gap h
  omega

/-- The main result in the shape of the host test: `after(start,delta) - start`. -/
theorem after_recovers_delta (start : UInt32) (delta : Nat) (hdelta : delta < 4294967296) :
    ((start + UInt32.ofNat delta) - start).toNat = delta :=
  wrapped_difference_recovers_gap start (start + UInt32.ofNat delta) delta hdelta
    (readsAfter_add start delta)

/-- The main result for true times of any size, however often the counter has
already wrapped before `t`. -/
theorem true_times_recover_gap (t gap : Nat) (hgap : gap < 4294967296) :
    (UInt32.ofNat (t + gap) - UInt32.ofNat t).toNat = gap :=
  wrapped_difference_recovers_gap (UInt32.ofNat t) (UInt32.ofNat (t + gap)) gap hgap
    (readsAfter_ofNat t gap)

/-! ### The limit, stated as theorems

No recovery is claimed for a gap of one complete wrap or more. The next three
results show that none is possible.
-/

/-- At 2^32 ms and beyond the difference is never the gap, whatever was read. -/
theorem no_recovery_at_or_past_full_wrap (earlier now : UInt32) (gap : Nat)
    (hfull : 4294967296 ≤ gap) : (now - earlier).toNat ≠ gap := by
  have hlt := u32_toNat_lt (now - earlier)
  omega

/-- A gap of exactly one wrap reads as no time at all. -/
theorem full_wrap_reads_as_zero (earlier now : UInt32)
    (h : ReadsAfter earlier now 4294967296) : (now - earlier).toNat = 0 := by
  have hmod := wrapped_difference_eq_gap_mod earlier now 4294967296 h
  omega

/-- The same for the test helper, so the hypothesis above is not empty. -/
theorem full_wrap_is_invisible (start : UInt32) :
    ((start + UInt32.ofNat 4294967296) - start).toNat = 0 :=
  full_wrap_reads_as_zero start (start + UInt32.ofNat 4294967296)
    (readsAfter_add start 4294967296)

/-! ### Two closed checks at the wrap

Evaluation of Lean's `UInt32` only. 4294966295 is 1001 ms before the wrap.
-/

example : ((4294966295 : UInt32) + 3000).toNat = 1999 := by decide
example : (((4294966295 : UInt32) + 3000) - 4294966295).toNat = 3000 := by decide

/-! ## 2. The three comparisons of the header

Scope: one comparison, one pair of readings. The constants and the three
predicates mirror these C++ expressions:

* `uint32_t(now-opened_)>=TIMEOUT_MS`          (`expired`)
* `uint32_t(now-holdStarted_)<HOLD_MS`         (`holdShort`)
* `uint32_t(now-lastPoll_)>MAX_POLL_GAP_MS`    (`pollGapTooLong`)
-/

def HOLD_MS : UInt32 := 3000
def TIMEOUT_MS : UInt32 := 30000
def MAX_POLL_GAP_MS : UInt32 := 250

theorem HOLD_MS_toNat : HOLD_MS.toNat = 3000 := by
  first
    | rfl
    | decide

theorem TIMEOUT_MS_toNat : TIMEOUT_MS.toNat = 30000 := by
  first
    | rfl
    | decide

theorem MAX_POLL_GAP_MS_toNat : MAX_POLL_GAP_MS.toNat = 250 := by
  first
    | rfl
    | decide

def expired (now opened : UInt32) : Bool := decide (now - opened ≥ TIMEOUT_MS)
def holdShort (now holdStarted : UInt32) : Bool := decide (now - holdStarted < HOLD_MS)
def pollGapTooLong (now lastPoll : UInt32) : Bool := decide (now - lastPoll > MAX_POLL_GAP_MS)

theorem expired_iff_toNat (now opened : UInt32) :
    expired now opened = true ↔ 30000 ≤ (now - opened).toNat := by
  have hc : TIMEOUT_MS.toNat = 30000 := TIMEOUT_MS_toNat
  unfold expired
  constructor
  · intro h
    have h' : TIMEOUT_MS.toNat ≤ (now - opened).toNat :=
      (u32_le_iff TIMEOUT_MS (now - opened)).mp (of_decide_eq_true h)
    omega
  · intro h
    exact decide_eq_true ((u32_le_iff TIMEOUT_MS (now - opened)).mpr (by omega))

theorem holdShort_iff_toNat (now holdStarted : UInt32) :
    holdShort now holdStarted = true ↔ (now - holdStarted).toNat < 3000 := by
  have hc : HOLD_MS.toNat = 3000 := HOLD_MS_toNat
  unfold holdShort
  constructor
  · intro h
    have h' : (now - holdStarted).toNat < HOLD_MS.toNat :=
      (u32_lt_iff (now - holdStarted) HOLD_MS).mp (of_decide_eq_true h)
    omega
  · intro h
    exact decide_eq_true ((u32_lt_iff (now - holdStarted) HOLD_MS).mpr (by omega))

theorem pollGapTooLong_iff_toNat (now lastPoll : UInt32) :
    pollGapTooLong now lastPoll = true ↔ 250 < (now - lastPoll).toNat := by
  have hc : MAX_POLL_GAP_MS.toNat = 250 := MAX_POLL_GAP_MS_toNat
  unfold pollGapTooLong
  constructor
  · intro h
    have h' : MAX_POLL_GAP_MS.toNat < (now - lastPoll).toNat :=
      (u32_lt_iff MAX_POLL_GAP_MS (now - lastPoll)).mp (of_decide_eq_true h)
    omega
  · intro h
    exact decide_eq_true ((u32_lt_iff MAX_POLL_GAP_MS (now - lastPoll)).mpr (by omega))

/-- A Boolean is false exactly when the statement it decides fails. -/
theorem bool_eq_false_iff_not {b : Bool} {p : Prop} (h : b = true ↔ p) :
    b = false ↔ ¬ p := by
  cases b
  · exact ⟨fun _ hp => absurd (h.mpr hp) (by decide), fun _ => rfl⟩
  · exact ⟨fun hb => absurd hb (by decide), fun hnp => absurd (h.mp rfl) hnp⟩

/-- Timeout, inclusive: `expired` holds exactly from 30000 true ms on. -/
theorem expired_iff_true_gap (opened now : UInt32) (gap : Nat)
    (hgap : gap < 4294967296) (h : ReadsAfter opened now gap) :
    expired now opened = true ↔ 30000 ≤ gap := by
  have hd := wrapped_difference_recovers_gap opened now gap hgap h
  have hi := expired_iff_toNat now opened
  constructor
  · intro he
    have hn := hi.mp he
    omega
  · intro hg
    exact hi.mpr (by omega)

theorem expired_eq_false_iff_true_gap (opened now : UInt32) (gap : Nat)
    (hgap : gap < 4294967296) (h : ReadsAfter opened now gap) :
    expired now opened = false ↔ gap < 30000 := by
  have hf := bool_eq_false_iff_not (expired_iff_true_gap opened now gap hgap h)
  constructor
  · intro he
    have hn := hf.mp he
    omega
  · intro hg
    exact hf.mpr (by omega)

/-- Hold, inclusive: the hold is still short exactly below 3000 true ms. -/
theorem holdShort_iff_true_gap (holdStarted now : UInt32) (gap : Nat)
    (hgap : gap < 4294967296) (h : ReadsAfter holdStarted now gap) :
    holdShort now holdStarted = true ↔ gap < 3000 := by
  have hd := wrapped_difference_recovers_gap holdStarted now gap hgap h
  have hi := holdShort_iff_toNat now holdStarted
  constructor
  · intro hs
    have hn := hi.mp hs
    omega
  · intro hg
    exact hi.mpr (by omega)

theorem holdShort_eq_false_iff_true_gap (holdStarted now : UInt32) (gap : Nat)
    (hgap : gap < 4294967296) (h : ReadsAfter holdStarted now gap) :
    holdShort now holdStarted = false ↔ 3000 ≤ gap := by
  have hf := bool_eq_false_iff_not (holdShort_iff_true_gap holdStarted now gap hgap h)
  constructor
  · intro hs
    have hn := hf.mp hs
    omega
  · intro hg
    exact hf.mpr (by omega)

/-- Poll gap, strict: too long exactly above 250 true ms. -/
theorem pollGapTooLong_iff_true_gap (lastPoll now : UInt32) (gap : Nat)
    (hgap : gap < 4294967296) (h : ReadsAfter lastPoll now gap) :
    pollGapTooLong now lastPoll = true ↔ 250 < gap := by
  have hd := wrapped_difference_recovers_gap lastPoll now gap hgap h
  have hi := pollGapTooLong_iff_toNat now lastPoll
  constructor
  · intro hp
    have hn := hi.mp hp
    omega
  · intro hg
    exact hi.mpr (by omega)

theorem pollGapTooLong_eq_false_iff_true_gap (lastPoll now : UInt32) (gap : Nat)
    (hgap : gap < 4294967296) (h : ReadsAfter lastPoll now gap) :
    pollGapTooLong now lastPoll = false ↔ gap ≤ 250 := by
  have hf := bool_eq_false_iff_not (pollGapTooLong_iff_true_gap lastPoll now gap hgap h)
  constructor
  · intro hp
    have hn := hf.mp hp
    omega
  · intro hg
    exact hf.mpr (by omega)

/-! ### The six boundary readings, with explicit numbers -/

theorem not_expired_at_29999 (opened now : UInt32) (h : ReadsAfter opened now 29999) :
    expired now opened = false :=
  (expired_eq_false_iff_true_gap opened now 29999 (by decide) h).mpr (by decide)

theorem expired_at_30000 (opened now : UInt32) (h : ReadsAfter opened now 30000) :
    expired now opened = true :=
  (expired_iff_true_gap opened now 30000 (by decide) h).mpr (by decide)

theorem hold_short_at_2999 (holdStarted now : UInt32) (h : ReadsAfter holdStarted now 2999) :
    holdShort now holdStarted = true :=
  (holdShort_iff_true_gap holdStarted now 2999 (by decide) h).mpr (by decide)

theorem hold_complete_at_3000 (holdStarted now : UInt32) (h : ReadsAfter holdStarted now 3000) :
    holdShort now holdStarted = false :=
  (holdShort_eq_false_iff_true_gap holdStarted now 3000 (by decide) h).mpr (by decide)

theorem poll_gap_allowed_at_250 (lastPoll now : UInt32) (h : ReadsAfter lastPoll now 250) :
    pollGapTooLong now lastPoll = false :=
  (pollGapTooLong_eq_false_iff_true_gap lastPoll now 250 (by decide) h).mpr (by decide)

theorem poll_gap_too_long_at_251 (lastPoll now : UInt32) (h : ReadsAfter lastPoll now 251) :
    pollGapTooLong now lastPoll = true :=
  (pollGapTooLong_iff_true_gap lastPoll now 251 (by decide) h).mpr (by decide)

/-- The limit again, for the deadline: a true gap of any size whose residue
modulo 2^32 is below 30000 does not read as expired. A window left unpolled for
a whole wrap (about 49.7 days) is outside what the timeout comparison can see. -/
theorem deadline_hidden_after_full_wrap (opened now : UInt32) (gap : Nat)
    (h : ReadsAfter opened now gap) (halias : gap % 4294967296 < 30000) :
    expired now opened = false := by
  have hd := wrapped_difference_eq_gap_mod opened now gap h
  have hf := bool_eq_false_iff_not (expired_iff_toNat now opened)
  exact hf.mpr (by omega)

/-! ## 3. Cancel first, on a hand transcription of `tick()`

Scope: one call of `tick()`. `Wipe` holds the private fields of
`WipeConfirmation`, `Wipe.cancelled` is the state `cancel()` leaves, and `tick`
follows the C++ statement by statement, returning the new state and the event.
The transcription was made by reading the header. Nothing here checks it
against the compiled firmware.
-/

inductive Stage where
  | warning
  | confirm
  | armed

structure Wipe where
  stage : Stage
  opened : UInt32
  holdStarted : UInt32
  lastPoll : UInt32
  active : Bool
  awaitingRelease : Bool
  holding : Bool
  pollSeen : Bool

def Wipe.cancelled : Wipe :=
  { stage := Stage.warning, opened := 0, holdStarted := 0, lastPoll := 0,
    active := false, awaitingRelease := false, holding := false, pollSeen := false }

def tick (s : Wipe) (now : UInt32) (aDown bDown fresh : Bool) : Wipe × Bool :=
  if !s.active then (s, false)
  else if bDown || expired now s.opened then (Wipe.cancelled, false)
  else
    let s1 : Wipe :=
      if !fresh || (s.holding && s.pollSeen && pollGapTooLong now s.lastPoll) then
        { s with awaitingRelease := true, holding := false, holdStarted := 0, pollSeen := false }
      else s
    if !fresh then (s1, false)
    else
      let s2 : Wipe := { s1 with lastPoll := now, pollSeen := true }
      if s2.awaitingRelease then
        ((if !aDown then { s2 with awaitingRelease := false } else s2), false)
      else
        match s2.stage with
        | Stage.armed =>
          if !aDown then ({ s2 with holding := false, holdStarted := 0 }, false)
          else if !s2.holding then ({ s2 with holding := true, holdStarted := now }, false)
          else if holdShort now s2.holdStarted then (s2, false)
          else (Wipe.cancelled, true)
        | _ => (s2, false)

/-- An inactive confirmation ignores the tick. -/
theorem tick_inactive (s : Wipe) (now : UInt32) (aDown bDown fresh : Bool)
    (hInactive : s.active = false) :
    tick s now aDown bDown fresh = (s, false) := by
  first
    | simp [tick, hInactive]
    | (unfold tick; simp [hInactive])

/-- Cancel first: with B down or the deadline reached, an active confirmation
is cancelled and reports no event. There is no hypothesis about A, freshness,
the stage or the hold, so this covers the tick that would complete the hold. -/
theorem tick_cancel_first (s : Wipe) (now : UInt32) (aDown bDown fresh : Bool)
    (hActive : s.active = true)
    (hCancel : bDown = true ∨ expired now s.opened = true) :
    tick s now aDown bDown fresh = (Wipe.cancelled, false) := by
  cases hCancel with
  | inl hB =>
    first
      | simp [tick, hActive, hB]
      | (unfold tick; simp [hActive, hB])
  | inr hE =>
    first
      | simp [tick, hActive, hE]
      | (unfold tick; simp [hActive, hE])

/-- The model does report the event when nothing stands in the way, so the
previous theorem is not true only because the model never commits. -/
theorem tick_commits (s : Wipe) (now : UInt32)
    (hActive : s.active = true)
    (hNotExpired : expired now s.opened = false)
    (hPollOk : pollGapTooLong now s.lastPoll = false)
    (hReleased : s.awaitingRelease = false)
    (hArmed : s.stage = Stage.armed)
    (hHolding : s.holding = true)
    (hHeld : holdShort now s.holdStarted = false) :
    tick s now true false true = (Wipe.cancelled, true) := by
  first
    | simp [tick, hActive, hNotExpired, hPollOk, hReleased, hArmed, hHolding, hHeld]
    | (unfold tick; simp [hActive, hNotExpired, hPollOk, hReleased, hArmed, hHolding, hHeld])
    | simp_all [tick]

/-- Same state, same instant: A alone completes, A with B cancels.
(Host test: "Both buttons at completion cancel".) -/
theorem b_wins_on_completing_tick (s : Wipe) (now : UInt32)
    (hActive : s.active = true)
    (hNotExpired : expired now s.opened = false)
    (hPollOk : pollGapTooLong now s.lastPoll = false)
    (hReleased : s.awaitingRelease = false)
    (hArmed : s.stage = Stage.armed)
    (hHolding : s.holding = true)
    (hHeld : holdShort now s.holdStarted = false) :
    tick s now true false true = (Wipe.cancelled, true) ∧
    tick s now true true true = (Wipe.cancelled, false) :=
  ⟨tick_commits s now hActive hNotExpired hPollOk hReleased hArmed hHolding hHeld,
   tick_cancel_first s now true true true hActive (Or.inl rfl)⟩

/-! ### Sections 1 to 3 together

Scope: still one call of `tick()`. Each `since...` is a true gap in
milliseconds between a stored reading and `now`.
-/

/-- From 30000 true ms after opening (and below one wrap), no tick reports an
event, whatever the buttons, freshness, stage or hold.
(Host test: "Timeout wins over simultaneous 3s completion".) -/
theorem no_event_at_or_after_true_deadline (s : Wipe) (now : UInt32)
    (aDown bDown fresh : Bool) (sinceOpen : Nat)
    (hOpenLt : sinceOpen < 4294967296)
    (hOpen : ReadsAfter s.opened now sinceOpen)
    (hDeadline : 30000 ≤ sinceOpen) :
    (tick s now aDown bDown fresh).2 = false := by
  have hExpired : expired now s.opened = true :=
    (expired_iff_true_gap s.opened now sinceOpen hOpenLt hOpen).mpr hDeadline
  cases hA : s.active
  · exact congrArg Prod.snd (tick_inactive s now aDown bDown fresh hA)
  · exact congrArg Prod.snd
      (tick_cancel_first s now aDown bDown fresh hA (Or.inr hExpired))

/-- An armed, released, held confirmation commits on a fresh A-only tick when
the true gaps are: below 30000 ms since opening, at least 3000 ms since the
hold started, at most 250 ms since the last poll. -/
theorem tick_commits_of_true_gaps (s : Wipe) (now : UInt32)
    (sinceOpen sinceHold sincePoll : Nat)
    (hActive : s.active = true)
    (hReleased : s.awaitingRelease = false)
    (hArmed : s.stage = Stage.armed)
    (hHolding : s.holding = true)
    (hOpen : ReadsAfter s.opened now sinceOpen)
    (hHold : ReadsAfter s.holdStarted now sinceHold)
    (hPoll : ReadsAfter s.lastPoll now sincePoll)
    (hBeforeDeadline : sinceOpen < 30000)
    (hHoldLt : sinceHold < 4294967296)
    (hHeld : 3000 ≤ sinceHold)
    (hPollFresh : sincePoll ≤ 250) :
    tick s now true false true = (Wipe.cancelled, true) := by
  have hE : expired now s.opened = false :=
    (expired_eq_false_iff_true_gap s.opened now sinceOpen (by omega) hOpen).mpr hBeforeDeadline
  have hP : pollGapTooLong now s.lastPoll = false :=
    (pollGapTooLong_eq_false_iff_true_gap s.lastPoll now sincePoll (by omega) hPoll).mpr hPollFresh
  have hH : holdShort now s.holdStarted = false :=
    (holdShort_eq_false_iff_true_gap s.holdStarted now sinceHold hHoldLt hHold).mpr hHeld
  exact tick_commits s now hActive hE hP hReleased hArmed hHolding hH

/-! ## 4. Dependency report

Each line should name nothing beyond `propext`, `Classical.choice` and
`Quot.sound`, the three standard ones of Lean's core library.
-/

#print axioms wrapped_difference_eq_gap_mod
#print axioms wrapped_difference_recovers_gap
#print axioms after_recovers_delta
#print axioms true_times_recover_gap
#print axioms no_recovery_at_or_past_full_wrap
#print axioms full_wrap_is_invisible
#print axioms expired_iff_true_gap
#print axioms holdShort_iff_true_gap
#print axioms pollGapTooLong_iff_true_gap
#print axioms deadline_hidden_after_full_wrap
#print axioms tick_cancel_first
#print axioms b_wins_on_completing_tick
#print axioms no_event_at_or_after_true_deadline
#print axioms tick_commits_of_true_gaps

end LaunchLab.WipeClock
