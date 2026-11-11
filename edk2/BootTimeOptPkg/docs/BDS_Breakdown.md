# BDS 細分檢查點（單次驗證）

剖析用，不是 A/B 定稿。環境：QEMU TCG、OVMF RELEASE VS2022、`baseline`（Policy/Hotspot=FALSE）、1 run。

| 指標 | ns | ms |
|---|---:|---:|
| EndOfDxeToReadyToBootNs | 617549157 | 617.5 |
| PreConnectNs | 379589025 | 379.6 |
| ConnectAllNs | 196202031 | 196.2 |
| RefreshBootOptionsNs | 9249220 | 9.2 |
| PostRefreshToReadyToBootNs | 20971101 | 21.0 |
| 四桶加總 | 605911377 | 606.0 |
| 與相位差 | 11637780 | 11.5 |
| LoadPathNs（屬 RTB→EBS） | 196442005 | 196.4 |

`bds,Flags=31`：EndOfDxe／Connect／Refresh／ReadyToBoot／LoadPath 均有量到。

解讀：此 run 的 EndOfDxe→ReadyToBoot 以 **PreConnect**（約 380 ms）最大，其次 **ConnectAll**（約 196 ms）；Refresh 很小。LoadPath 落在 ReadyToBoot→EBS，不計入上表四桶。
