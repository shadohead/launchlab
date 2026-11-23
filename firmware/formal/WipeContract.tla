---------------------------- MODULE WipeContract ----------------------------
\* LaunchLab firmware Wipe all (factory reset) safety contract.
\* Research pilot: a small design-level model for TLC, written by reading
\*   firmware/LaunchLabMini/device_wipe.h         journal, erase order, begin and resume
\*   firmware/LaunchLabMini/device_wipe_nvs.h     NVS adapter and its allowlist guard
\*   firmware/LaunchLabMini/wipe_confirmation.h   confirmation stages
\*   firmware/LaunchLabMini/LaunchLabMini.ino     commitWipe, pollWipe, recoverWipe
\* at tree 8d063e41. It is not derived from that code mechanically and proves
\* nothing about it. model-notes.md holds the source map, the assumptions, the
\* gaps and the run instructions.
EXTENDS Naturals

CONSTANTS
  NumOwned,                  \* erase units on the allowlist, cleared in index order
  ExtraTargets,              \* names in the engine's list that are off the allowlist (0 as built)
  AdapterEnforcesAllowlist,  \* the adapter refuses a name off the allowlist (TRUE as built)
  PersistIntentFirst,        \* MUTATION SWITCH: commit and read back the marker before erasing
  EnforceBudgets,            \* FALSE: cuts and faults recur without limit (safety runs only)
  MaxPowerCuts,              \* uncontrolled resets per behaviour
  MaxFaults                  \* misbehaving store calls per behaviour

ASSUME
  /\ NumOwned \in Nat /\ NumOwned >= 1
  /\ ExtraTargets \in {0, 1}
  /\ AdapterEnforcesAllowlist \in BOOLEAN
  /\ PersistIntentFirst \in BOOLEAN
  /\ EnforceBudgets \in BOOLEAN
  /\ MaxPowerCuts \in Nat
  /\ MaxFaults \in Nat

VARIABLES
  \* volatile: lost at every reset
  mode,        \* "recover" | "normal" | "wiping" | "complete" | "blocked"
  dlg,         \* confirmation stage: 0 none, 1 Warning, 2 Confirm, 3 Armed
  kind,        \* running transaction: "none" | "begin" | "resume"
  pc,          \* its next step
  idx,         \* position in the erase list while pc = "clear", else 0
  \* durable
  marker,      \* ll-reset/pending: "absent" | "valid" | "invalid"
  unit,        \* owned stores: "user" | "partial" | "empty"
  foreign,     \* everything off the allowlist: "intact" | "erased"
  \* bookkeeping, not in the firmware
  auth,        \* ghost: a local authorization is outstanding
  cutsLeft,
  faultsLeft

vars      == <<mode, dlg, kind, pc, idx, marker, unit, foreign, auth, cutsLeft, faultsLeft>>
nonDialog == <<mode, kind, pc, idx, marker, unit, foreign, auth, cutsLeft, faultsLeft>>

Owned      == 1..NumOwned               \* as built: 1..8 the NVS namespaces, 9 the spiffs partition
NumTargets == NumOwned + ExtraTargets   \* the engine walks 1..NumTargets in order

AllUser  == \A u \in Owned : unit[u] = "user"
AllEmpty == \A u \in Owned : unit[u] = "empty"
Whole    == AllUser \/ AllEmpty         \* untouched, or erased completely

Init ==
  /\ mode = "recover" /\ kind = "resume" /\ pc = "inspect" /\ idx = 0 /\ dlg = 0
  /\ marker = "absent"
  /\ unit = [u \in Owned |-> "user"]
  /\ foreign = "intact"
  /\ auth = FALSE
  /\ cutsLeft = MaxPowerCuts /\ faultsLeft = MaxFaults

-----------------------------------------------------------------------------
\* Store behaviour.

\* inspect() reports the truth or a storage error, never a wrong answer (A2).
Truth == IF marker = "absent" THEN "clean" ELSE IF marker = "valid" THEN "pending" ELSE "invalid"

Spend(b) == IF EnforceBudgets THEN b - 1 ELSE b
Fault    == faultsLeft > 0 /\ faultsLeft' = Spend(faultsLeft)

Inspect(r) ==
  \/ r = Truth /\ UNCHANGED faultsLeft
  \/ r = "error" /\ Fault

\* What an erase that does not finish cleanly can leave in one unit.
Damaged(c) == IF c = "empty" THEN {"empty"} ELSE {c, "partial", "empty"}

-----------------------------------------------------------------------------
\* Control flow shared by begin() and resume().

Goto(p, i) == pc' = p /\ idx' = i /\ UNCHANGED <<mode, kind, auth>>

\* The transaction returns; m is the marker it leaves. A failure always lands
\* on the blocked page. The authorization outlives the call only as a valid marker.
EndTx(ok, m) ==
  /\ pc' = "idle" /\ idx' = 0 /\ kind' = "none"
  /\ mode' = IF ~ok THEN "blocked" ELSE IF kind = "begin" THEN "complete" ELSE "normal"
  /\ auth' = (auth /\ m = "valid")

AfterClear == IF idx < NumTargets THEN Goto("clear", idx + 1) ELSE Goto("clear_intent", 0)

\* Any reset: RAM is gone and setup() runs recoverWipe() before any load.
Reboot(m) ==
  /\ mode' = "recover" /\ kind' = "resume" /\ pc' = "inspect" /\ idx' = 0 /\ dlg' = 0
  /\ auth' = (auth /\ m = "valid")

-----------------------------------------------------------------------------
\* The person and the app. The gesture and its timing are not modelled: these
\* four actions are the input contract (A1 in model-notes.md).

\* A on Settings > Wipe all (button, or a USB tap), or A on the blocked page.
OpenDialog ==
  /\ mode \in {"normal", "blocked"} /\ dlg = 0
  /\ dlg' = 1
  /\ UNCHANGED nonDialog

\* confirm(): a physical A press after a fresh release. Warning, Confirm, Armed.
Advance ==
  /\ mode \in {"normal", "blocked"} /\ dlg \in {1, 2}
  /\ dlg' = dlg + 1
  /\ UNCHANGED nonDialog

\* cancel(): B, the 30 s deadline, leaving the page, the side key, USB b.
Cancel ==
  /\ mode \in {"normal", "blocked"} /\ dlg > 0
  /\ dlg' = 0
  /\ UNCHANGED nonDialog

\* tick() returns true once: the fresh continuous physical hold on the Armed stage.
HoldComplete ==
  /\ mode \in {"normal", "blocked"} /\ dlg = 3
  /\ \/ /\ dlg' = 0                     \* commitWipe() sets wipeBlocked, calls begin()
        /\ mode' = "wiping" /\ kind' = "begin" /\ pc' = "inspect"
        /\ auth' = TRUE
     \/ /\ mode = "normal"              \* WIPE_DEFERRED: capture busy, nothing written
        /\ dlg' = 1
        /\ UNCHANGED <<mode, kind, pc, auth>>
  /\ UNCHANGED <<idx, marker, unit, foreign, cutsLeft, faultsLeft>>

\* Every ordinary save of RAM records, lumped into one write of all units (A4).
AppWrite ==
  /\ mode = "normal" /\ ~AllUser
  /\ unit' = [u \in Owned |-> "user"]
  /\ UNCHANGED <<mode, dlg, kind, pc, idx, marker, foreign, auth, cutsLeft, faultsLeft>>

-----------------------------------------------------------------------------
\* DeviceWipe::begin(), reached only from HoldComplete.

BeginInspect ==
  /\ kind = "begin" /\ pc = "inspect"
  /\ \E r \in {Truth, "error"} :
       /\ Inspect(r)
       /\ IF r = "error" THEN EndTx(FALSE, marker)
          ELSE IF r = "pending" \/ ~PersistIntentFirst THEN Goto("clear", 1)
          ELSE Goto("set_intent", 0)
  /\ UNCHANGED <<dlg, marker, unit, foreign, cutsLeft>>

\* setIntent(): any return value over any landing state; all but the clean one cost a fault.
SetIntent ==
  /\ pc = "set_intent"
  /\ \E ret \in BOOLEAN, m \in {marker, "valid", "invalid"} :
       /\ IF ret /\ m = "valid" THEN UNCHANGED faultsLeft ELSE Fault
       /\ marker' = m
       /\ IF ret THEN Goto("verify_intent", 0) ELSE EndTx(FALSE, m)
  /\ UNCHANGED <<dlg, unit, foreign, cutsLeft>>

\* The read-back: only an exact marker lets the erase start.
VerifyIntent ==
  /\ pc = "verify_intent"
  /\ \E r \in {Truth, "error"} :
       /\ Inspect(r)
       /\ IF r = "pending" THEN Goto("clear", 1) ELSE EndTx(FALSE, marker)
  /\ UNCHANGED <<dlg, marker, unit, foreign, cutsLeft>>

-----------------------------------------------------------------------------
\* Detail::finish(), shared by begin() and resume().

\* clearNamespace() or clearFlash(): true only when the unit is empty.
ClearOwned ==
  /\ pc = "clear" /\ idx \in Owned
  /\ \/ /\ unit' = [unit EXCEPT ![idx] = "empty"]
        /\ UNCHANGED faultsLeft
        /\ AfterClear
     \/ /\ Fault
        /\ \E c \in Damaged(unit[idx]) : unit' = [unit EXCEPT ![idx] = c]
        /\ EndTx(FALSE, marker)
  /\ UNCHANGED <<dlg, marker, foreign, cutsLeft>>

\* A name off the allowlist. Unreachable as built (ExtraTargets = 0).
ClearForeign ==
  /\ pc = "clear" /\ idx \notin Owned
  /\ IF AdapterEnforcesAllowlist
       THEN /\ UNCHANGED foreign
            /\ EndTx(FALSE, marker)
       ELSE /\ foreign' = "erased"
            /\ AfterClear
  /\ UNCHANGED <<dlg, marker, unit, cutsLeft, faultsLeft>>

\* clearIntent(): erasing a key that is not there is an error (NotFound).
ClearIntent ==
  /\ pc = "clear_intent"
  /\ IF marker = "absent"
       THEN /\ UNCHANGED <<marker, faultsLeft>>
            /\ EndTx(FALSE, marker)
       ELSE \E ret \in BOOLEAN, m \in {marker, "absent", "invalid"} :
              /\ IF ret /\ m = "absent" THEN UNCHANGED faultsLeft ELSE Fault
              /\ marker' = m
              /\ IF ret THEN Goto("verify_clean", 0) ELSE EndTx(FALSE, m)
  /\ UNCHANGED <<dlg, unit, foreign, cutsLeft>>

\* Complete only when the journal reads Clean afterwards.
VerifyClean ==
  /\ pc = "verify_clean"
  /\ \E r \in {Truth, "error"} :
       /\ Inspect(r)
       /\ EndTx(r = "clean", marker)
  /\ UNCHANGED <<dlg, marker, unit, foreign, cutsLeft>>

-----------------------------------------------------------------------------
\* recoverWipe() at boot: Clean loads normally, Pending finishes the erase
\* first, anything else blocks. It never writes a marker.
ResumeInspect ==
  /\ kind = "resume" /\ pc = "inspect"
  /\ \E r \in {Truth, "error"} :
       /\ Inspect(r)
       /\ IF r = "clean" THEN EndTx(TRUE, marker)
          ELSE IF r = "pending" THEN Goto("clear", 1)
          ELSE EndTx(FALSE, marker)
  /\ UNCHANGED <<dlg, marker, unit, foreign, cutsLeft>>

-----------------------------------------------------------------------------
\* Resets.

\* The offered restart: buttons released after Complete; B, the side key or the
\* idle power-off (and the next power-on) on the blocked page. It writes nothing.
Restart ==
  /\ mode \in {"complete", "blocked"}
  /\ Reboot(marker)
  /\ UNCHANGED <<marker, unit, foreign, cutsLeft, faultsLeft>>

\* What a store call in flight may have done when power is lost.
MarkerAfterCut ==
  IF pc = "set_intent" THEN {marker, "valid", "invalid"}
  ELSE IF pc = "clear_intent" /\ marker # "absent" THEN {marker, "absent", "invalid"}
  ELSE {marker}

UnitsAfterCut ==
  IF pc = "clear" /\ idx \in Owned
    THEN {[unit EXCEPT ![idx] = c] : c \in Damaged(unit[idx])}
    ELSE {unit}

ForeignAfterCut ==
  IF pc = "clear" /\ idx \notin Owned /\ ~AdapterEnforcesAllowlist
    THEN {foreign, "erased"}
    ELSE {foreign}

\* Power loss, crash or watchdog at any point. A unit that has not yet started
\* booting is skipped: a reset there changes nothing.
PowerCut ==
  /\ cutsLeft > 0
  /\ ~(mode = "recover" /\ pc = "inspect")
  /\ cutsLeft' = Spend(cutsLeft)
  /\ \E m \in MarkerAfterCut, u \in UnitsAfterCut, f \in ForeignAfterCut :
       /\ marker' = m /\ unit' = u /\ foreign' = f
       /\ Reboot(m)
  /\ UNCHANGED faultsLeft

-----------------------------------------------------------------------------
\* No stuttering disjunct and no terminal state: every action below changes a
\* variable other than a budget, and some action is enabled in every state, so
\* TLC's deadlock check stays meaningful (do not pass -deadlock).

TxStep ==
  \/ BeginInspect
  \/ SetIntent
  \/ VerifyIntent
  \/ ClearOwned
  \/ ClearForeign
  \/ ClearIntent
  \/ VerifyClean
  \/ ResumeInspect

Next ==
  \/ OpenDialog
  \/ Advance
  \/ Cancel
  \/ HoldComplete
  \/ AppWrite
  \/ TxStep
  \/ Restart
  \/ PowerCut

\* Liveness assumptions only: the firmware takes its next step, and a blocked
\* unit that holds a valid marker is restarted some day. Nothing is assumed of
\* the person otherwise; cuts and faults are bounded by the budgets instead.
Fairness ==
  /\ WF_vars(TxStep)
  /\ WF_vars(Restart /\ marker = "valid")

Spec == Init /\ [][Next]_vars /\ Fairness

-----------------------------------------------------------------------------
\* Shape.

TypeOK ==
  /\ mode \in {"recover", "normal", "wiping", "complete", "blocked"}
  /\ dlg \in 0..3
  /\ kind \in {"none", "begin", "resume"}
  /\ pc \in {"idle", "inspect", "set_intent", "verify_intent", "clear", "clear_intent",
             "verify_clean"}
  /\ idx \in 0..NumTargets
  /\ marker \in {"absent", "valid", "invalid"}
  /\ unit \in [Owned -> {"user", "partial", "empty"}]
  /\ foreign \in {"intact", "erased"}
  /\ auth \in BOOLEAN
  /\ cutsLeft \in 0..MaxPowerCuts
  /\ faultsLeft \in 0..MaxFaults

ControlOK ==
  /\ (pc = "idle") <=> (kind = "none")
  /\ (pc = "idle") <=> (mode \in {"normal", "complete", "blocked"})
  /\ (mode = "recover") <=> (kind = "resume")
  /\ (mode = "wiping") <=> (kind = "begin")
  /\ (pc = "clear") <=> (idx \in 1..NumTargets)
  /\ (dlg > 0) => (mode \in {"normal", "blocked"})
  \* Automatic recovery never creates or replaces a marker.
  /\ (kind = "resume") => (pc \notin {"set_intent", "verify_intent"})

-----------------------------------------------------------------------------
\* Safety.

InErasePhase == pc \in {"clear", "clear_intent"}

\* Persist before erase: the engine is in its erase phase only over a durable,
\* exact marker.
IntentDurableBeforeErase == InErasePhase => (marker = "valid")

\* Crash consistency: a store that is neither untouched nor fully erased exists
\* only under that marker, so a reset at any point finds it.
TornOnlyUnderIntent == (~Whole) => (marker = "valid")

\* Boot order: normal loads and saves only over a clean journal and a whole store.
NormalRunsOnCleanStore == (mode = "normal") => (marker = "absent" /\ Whole)

\* Complete is reported only when everything owned is gone and the journal is clean.
CompleteMeansWiped ==
  /\ (pc = "verify_clean") => AllEmpty
  /\ (mode = "complete") => (marker = "absent" /\ AllEmpty)

\* Allowlist scope.
ScopeRespected == foreign = "intact"

\* Authorization composed with persistence: every erase phase and every valid
\* marker traces back to a HoldComplete that has not been discharged, across
\* any number of resets.
AuthorityBacksErase ==
  /\ InErasePhase => auth
  /\ (marker = "valid") => auth

\* Only the armed on-device dialog starts a begin() or raises the authorization.
LocalInitiationOnly ==
  [][ ((auth' /\ ~auth) \/ (kind' = "begin" /\ kind # "begin"))
        => (dlg = 3 /\ mode \in {"normal", "blocked"}) ]_vars

\* Fail closed: after an erase attempt returns, nothing durable changes until a
\* new local authorization or a reset.
FailClosed ==
  [][ ((mode \in {"blocked", "complete"}) =>
        (<<marker', unit', foreign'>> = <<marker, unit, foreign>>)) ]_vars

-----------------------------------------------------------------------------
\* Liveness (with EnforceBudgets = TRUE only).

\* Every begin() and every boot recovery returns: normal, complete or blocked.
TransactionsSettle == (pc # "idle") ~> (pc = "idle")

\* A journalled wipe is finished, given the restart assumed in Fairness.
IntentIsHonoured == (marker = "valid") ~> (marker # "valid" /\ AllEmpty)

-----------------------------------------------------------------------------
\* Reachability witnesses. Each is EXPECTED TO BE VIOLATED when checked as an
\* invariant: the counterexample is the witness that the state exists.

W_NeverValidMarker    == marker # "valid"
W_NeverTorn           == Whole
W_NeverComplete       == mode # "complete"
W_NeverResumeErases   == ~(kind = "resume" /\ pc = "clear")
W_NeverBlockedInvalid == ~(mode = "blocked" /\ marker = "invalid")
W_NeverFreshNormal    == ~(mode = "normal" /\ AllEmpty)

=============================================================================
