Test Scenarios – Interactive Deadlock Simulation System

Notation: `P0..Pn` are processes, `R0..Rm` are resources. Vectors are written `[R0,R1,R2]`.
Formulas: `Available = Total − Σ Allocation`, `Need = Maximum − Allocation`.
---
Scenario 1 – Safe state (textbook example)
Load: use the built-in sample (`state_load_sample`) or enter it manually.
Total: `[10,5,7]`
Process	Allocation	Maximum	Need
P0	0 1 0	7 5 3	7 4 3
P1	2 0 0	3 2 2	1 2 2
P2	3 0 2	9 0 2	6 0 0
P3	2 1 1	2 2 2	0 1 1
P4	0 0 2	4 3 3	4 3 1
Expected results
Available: `[3,3,2]`
Banker's safety check: SAFE
Safe sequence: `P1 → P3 → P4 → P0 → P2`
Deadlock detection (no pending requests): no deadlock
Status dashboard: green SAFE banner
Utilization: R0 70%, R1 40%, R2 71.4%
RAG: only allocation edges (Rj → Pi), no request edges
> Note: other safe sequences exist. Any valid order is acceptable, but the algorithm that scans processes in index order will print the one above.
---
Scenario 2 – Resource request granted
Start from: Scenario 1.
Action (menu 7): P1 requests `[1,0,2]`.
Expected results
Checks: Request ≤ Need (`[1,0,2] ≤ [1,2,2]`) ✔, Request ≤ Available (`[1,0,2] ≤ [3,3,2]`) ✔
Pretend allocation gives Available `[2,3,0]`, P1 Allocation `[3,0,2]`, P1 Need `[0,2,0]`
Safety check is still SAFE, sequence `P1 → P3 → P4 → P0 → P2`
Result: GRANTED, state is updated
Utilization after grant: R0 80%, R1 40%, R2 100%
---
Scenario 3 – Request denied: exceeds available (process must wait)
Start from: Scenario 2 (state after P1's grant).
Action: P4 requests `[3,3,0]`.
Expected results
Request ≤ Need (`[3,3,0] ≤ [4,3,1]`) ✔
Request ≤ Available (`[3,3,0] ≤ [2,3,0]`) ✘ because R0 needs 3 but only 2 are free
Result: DENIED / must wait, state is unchanged
---
Scenario 4 – Request denied: would lead to an UNSAFE state
Start from: Scenario 2.
Action: P0 requests `[0,2,0]`.
Expected results
Request ≤ Need ✔ (`[0,2,0] ≤ [7,4,3]`), Request ≤ Available ✔ (`[0,2,0] ≤ [2,3,0]`)
Tentative state: Available `[2,1,0]`, P0 Allocation `[0,3,0]`, P0 Need `[7,2,3]`
No process has Need ≤ `[2,1,0]` (P1 needs `[0,2,0]`, P3 needs `[0,1,1]`, etc.), so there is no safe sequence
Result: DENIED, the tentative allocation is rolled back (state unchanged)
---
Scenario 5 – Request denied: exceeds declared maximum
Start from: Scenario 1.
Action: P1 requests `[4,0,0]`.
Expected results
Request ≤ Need fails (`[4,0,0]` vs `[1,2,2]`)
Result: ERROR – process exceeded its maximum claim, state unchanged
---
Scenario 6 – UNSAFE state (but not deadlocked)
Total: `[2,2]`
Process	Allocation	Maximum	Need
P0	1 0	2 2	1 2
P1	0 1	2 2	2 1
Expected results
Available: `[1,1]`
Neither Need fits in `[1,1]` → no safe sequence → UNSAFE
No process is currently waiting (no pending requests), so detection reports no deadlock
Dashboard: orange UNSAFE banner
This shows the difference: unsafe means deadlock is possible, deadlocked means it has happened.
---
Scenario 7 – DEADLOCKED state (circular wait)
Total: `[1,1,1,1]` (four single-instance resources)
Process	Allocation	Maximum	Pending request
P0	1 0 0 0	1 1 0 0	wants R1
P1	0 1 0 0	0 1 1 0	wants R2
P2	0 0 1 0	1 0 1 0	wants R0
P3	0 0 0 1	0 0 0 1	nothing (running)
Expected results
Available: `[0,0,0,0]`
Detection: P3 can finish and releases R3, but nobody needs R3
Deadlocked processes: P0, P1, P2 (P3 is not deadlocked)
Status dashboard: red DEADLOCKED banner
RAG: the cycle `P0 → R1 → P1 → R2 → P2 → R0 → P0` (request edges Pi → Rj, allocation edges Rj → Pi); highlight it in red
Utilization: 100% for every resource
All four necessary conditions can be explained with this example:
Mutual exclusion – each resource has one instance
Hold and wait – each of P0–P2 holds one resource while waiting for another
No preemption – resources are only released voluntarily
Circular wait – P0 → P1 → P2 → P0
---
Scenario 8 – Recovery from Scenario 7
Start from: Scenario 7. Menu 10 offers three strategies.
Strategy	Expected outcome
Terminate all	P0, P1, P2 terminated; all resources free; Available `[1,1,1,0]`; no deadlock
Terminate one by one	After terminating one process (e.g. P0, which frees R0), P2 gets R0 and finishes, then P1 gets R2 and finishes; 1 termination is enough; detection is re-run after each step
Resource preemption (victim = P0)	R0 is taken from P0 and given to P2; P2 finishes and frees R2 and R0, then P1 finishes; P0 can later get R0 back and continue
After any successful recovery, re-run detection (menu 9): no deadlock, and the dashboard returns to green.
---
Scenario 9 – Input validation (setup screens)
Input	Expected error
Allocation of R0 for P3 greater than its Maximum	"P3 allocation of R0 exceeds its maximum"
Σ Allocation of R1 = 6 but Total R1 = 5	"Total allocation of R1 (6) exceeds its total instances (5)"
Negative number in any matrix cell	Rejected
Allocation entered before Maximum	Allowed (a Maximum row of all zeros means "not entered yet")
More than 10 processes or 10 resources	Rejected (`MAX_PROC` / `MAX_RES`)
---
Quick test checklist (for the demo)
[ ] Scenario 1 shows SAFE with the correct safe sequence
[ ] Scenario 2 is granted, Scenarios 3 and 4 are denied for different reasons
[ ] Scenario 6 shows UNSAFE (orange) with no deadlock reported
[ ] Scenario 7 shows DEADLOCKED (red) naming exactly P0, P1, P2
[ ] Each recovery strategy in Scenario 8 ends with no deadlock
[ ] RAG matches the matrices in every scenario
[ ] Status bar and dashboard update immediately after each action
