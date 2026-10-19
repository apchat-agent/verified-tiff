| Workload | Before ms | After ms | Speedup | Before / libtiff | After / libtiff |
|---|---:|---:|---:|---:|---:|
| gray-deflate native | 5.1697 | 1.2555 | 4.12× | 9.33× | 2.18× |
| rgb-deflate native | 9.3179 | 3.3395 | 2.79× | 5.80× | 2.10× |
| rgb-deflate crop | 2.2688 | 0.8496 | 2.67× | 10.16× | 3.68× |
| noise-deflate native | 7.7445 | 1.2639 | 6.13× | 16.53× | 2.62× |
| gray16-deflate-p2 native | 23.5573 | 20.0303 | 1.18× | 5.25× | 4.56× |
| large-strip-deflate firstrow | 43.6501 | 11.7067 | 3.73× | 1682.83× | 442.71× |
| large-strip-deflate crop | 66.9196 | 18.2546 | 3.67× | 12.18× | 3.36× |
