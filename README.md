# Air‑Traffic‑Control‑Net (ANU Project)

> **A concurrent client–server network that schedules aircraft landings across multiple simulated airports.**  
> Built in C for the *Systems, Networks & Concurrency* course at the Australian National University.

---

## 1 . Project Goals
* Implement a **controller** process that proxies client requests to the correct airport node, parses responses, and returns a human‑readable reply.  
* Implement one or more **airport nodes** (spawned as child processes) that hold per‑gate schedules and decide where/when to land a flight.  
* Support **concurrent handling** of multiple clients and inter‑process requests via a thread‑pool design (controller) and optional thread pools in each airport.  
* Guarantee **safety** (no runway conflicts) and **liveness** (every request eventually receives a response).  
* Provide a clean command‑line interface, clear error messages, self‑tests, and CI‑friendly Make rules.

---

## 2 . Building

```bash
git clone https://github.com/<your‑username>/air‑traffic‑control‑net.git
cd air‑traffic‑control‑net
make            # builds controller and airport node binaries
make test       # runs the provided regression tests
