# BDS 階段時間細分（單次驗證）

用來分析時間花在哪裡，不會直接縮短開機時間；也不是兩組比較的正式結果。  
環境：QEMU TCG、OVMF RELEASE VS2022、`baseline`（Policy／Hotspot=FALSE）、1 次開機。

| 指標 | ns | ms |
|---|---:|---:|
| EndOfDxeToReadyToBootNs | 617549157 | 617.5 |
| PreConnectNs | 379589025 | 379.6 |
| ConnectAllNs | 196202031 | 196.2 |
| RefreshBootOptionsNs | 9249220 | 9.2 |
| PostRefreshToReadyToBootNs | 20971101 | 21.0 |
| 四個階段的時間加總 | 605911377 | 606.0 |
| 與相位差 | 11637780 | 11.5 |
| LoadPathNs（屬 ReadyToBoot→ExitBootServices） | 196442005 | 196.4 |

`bds,Flags=31`：EndOfDxe／Connect／Refresh／ReadyToBoot／LoadPath 均有量到。

解讀：此 run 的 EndOfDxe→ReadyToBoot 以 **PreConnect**（約 380 ms）最大，其次 **ConnectAll**（約 196 ms）；刷新開機選項很小。LoadPath 落在 ReadyToBoot→ExitBootServices，不計入上面四個階段。
