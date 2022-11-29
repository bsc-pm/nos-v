
# Hardware Counters

nOS-V offers an infrastructure to obtain hardware counter statistics of tasks. The usage of this API is controlled through the nOS-V configure file. Currently, nOS-V supports the PAPI backend.

Specific counters can be enabled or disabled by adding or removing their name from the list of counters inside the PAPI subsection.

Next we showcase a simplified version of the hardware counter section of the configure file, where the PAPI backend is enabled with counters that monitor the total number of instructions and cycles, and the PAPI backend is enabled as well:

```toml
[hwcounters]
    verbose = true
    backend = "papi"
    papi_events = ["PAPI_TOT_INS", "PAPI_TOT_CYC"]
```
