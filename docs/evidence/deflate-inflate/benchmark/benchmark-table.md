| Workload | Before ms | After ms | Speedup | Before / libtiff | After / libtiff |
|---|---:|---:|---:|---:|---:|
| gray-deflate native | 7.3462 | 1.9223 | 3.82× | 8.94× | 2.35× |
| rgb-deflate native | 13.1309 | 4.9567 | 2.65× | 5.70× | 2.29× |
| rgb-deflate crop | 3.2590 | 1.1056 | 2.95× | 9.81× | 3.30× |
| noise-deflate native | 12.0201 | 1.9371 | 6.21× | 18.07× | 2.97× |
| gray16-deflate-p2 native | 35.0316 | 42.2740 | 0.83× | 5.38× | 6.92× |
| large-strip-deflate firstrow | 64.6929 | 16.9833 | 3.81× | 1727.33× | 432.62× |
| large-strip-deflate crop | 95.2387 | 25.7871 | 3.69× | 11.90× | 3.38× |
