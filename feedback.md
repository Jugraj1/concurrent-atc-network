---
mark: 76

section_marks:
  code: 53
  report: 23
---

## Code 
Well styled. No locking mechanism is found around the airport schedule object.

## Report
Well formatted. Regarding airport nodes, I'm not entirely convinced that no lock is necessary on the shared object AIRPORT_DATA, though all tests (surprisingly) passed.
No justification on #threads is found.



