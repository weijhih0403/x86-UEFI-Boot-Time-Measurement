# -*- coding: utf-8 -*-
"""
產生「專案說明_開機時間量測.docx」。
用法（在本檔所在目錄或任意目錄）：
  py -3 generate_project_doc.py
"""

from __future__ import annotations

from pathlib import Path

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.oxml.ns import qn
from docx.shared import Cm, Pt


def set_run_font(run, name_zh: str = "微軟正黑體", name_en: str = "Calibri", size: int = 11):
    run.font.size = Pt(size)
    run.font.name = name_en
    r = run._element
    rPr = r.get_or_add_rPr()
    rFonts = rPr.get_or_add_rFonts()
    rFonts.set(qn("w:eastAsia"), name_zh)


def add_heading(doc: Document, text: str, level: int = 1):
    p = doc.add_heading(text, level=level)
    for run in p.runs:
        set_run_font(run, size=16 if level == 1 else 13)
    return p


def add_para(doc: Document, text: str, bold: bool = False, size: int = 11):
    p = doc.add_paragraph()
    run = p.add_run(text)
    run.bold = bold
    set_run_font(run, size=size)
    p.paragraph_format.space_after = Pt(6)
    p.paragraph_format.line_spacing = 1.25
    return p


def add_bullets(doc: Document, items: list[str]):
    for item in items:
        p = doc.add_paragraph(style="List Bullet")
        run = p.add_run(item)
        set_run_font(run)
        p.paragraph_format.space_after = Pt(3)


def add_table(doc: Document, headers: list[str], rows: list[list[str]]):
    table = doc.add_table(rows=1 + len(rows), cols=len(headers))
    table.style = "Table Grid"
    for i, h in enumerate(headers):
        cell = table.rows[0].cells[i]
        cell.text = ""
        run = cell.paragraphs[0].add_run(h)
        run.bold = True
        set_run_font(run, size=10)
    for r_idx, row in enumerate(rows):
        for c_idx, val in enumerate(row):
            cell = table.rows[r_idx + 1].cells[c_idx]
            cell.text = ""
            run = cell.paragraphs[0].add_run(val)
            set_run_font(run, size=10)
    doc.add_paragraph()


def build_document() -> Document:
    doc = Document()

    section = doc.sections[0]
    section.top_margin = Cm(2.2)
    section.bottom_margin = Cm(2.2)
    section.left_margin = Cm(2.2)
    section.right_margin = Cm(2.2)

    # Cover
    title = doc.add_paragraph()
    title.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = title.add_run("x86 UEFI Boot-Time Measurement")
    run.bold = True
    set_run_font(run, size=20)

    sub = doc.add_paragraph()
    sub.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = sub.add_run("專案說明文件")
    run.bold = True
    set_run_font(run, size=16)

    meta = doc.add_paragraph()
    meta.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = meta.add_run(
        "開機時間量測與分析（EDK II／OVMF／QEMU）\n"
        "文件用途：用簡單方式說明專案做什麼、架構如何串起來、結果怎麼讀"
    )
    set_run_font(run, size=11)

    add_para(
        doc,
        "閱讀建議：先看第 1～3 章建立整體概念，再看第 4～6 章理解架構與細分量測，"
        "最後看第 7～9 章解讀數字與限制。",
    )

    # ---- 1 ----
    add_heading(doc, "1. 專案是什麼", 1)
    add_para(
        doc,
        "一句話：本專案在 QEMU 上的 OVMF（虛擬機用的 UEFI 韌體）裡，"
        "量測並分析開機各段花多少時間，方便找出時間主要落在哪一段。",
    )
    add_para(doc, "這個專案在做什麼", bold=True)
    add_bullets(
        doc,
        [
            "在 DXE／BDS 階段記錄各段開機時間",
            "把結果寫進 UEFI 變數（NVRAM），再輸出 CSV 或用選單查看",
            "可重複開機，比較兩組設定的統計差異",
        ],
    )
    add_para(doc, "這個專案沒有做什麼", bold=True)
    add_bullets(
        doc,
        [
            "沒有實作「改開機流程讓系統變快」的加速策略",
            "腳本裡的 baseline／optimized 只是兩組量測開關名稱，不代表第二組已經完成加速",
        ],
    )
    add_para(doc, "使用方式（概要）", bold=True)
    add_para(
        doc,
        "本倉庫採 overlay：只保留新增或修改的程式碼。使用前需整合到完整 EDK II 專案，"
        "建出 OVMF 後以 QEMU 執行。細部建置步驟見專案根目錄 README.md。",
    )

    # ---- 2 ----
    add_heading(doc, "2. 背景：UEFI 開機階段", 1)
    add_para(
        doc,
        "UEFI 開機大致依序為：SEC → PEI → DXE → BDS → ExitBootServices。"
        "若只看「整段開機多久」，很難知道時間花在哪。",
    )
    add_table(
        doc,
        ["階段", "簡單意思", "本專案是否量測"],
        [
            ["SEC／PEI", "最早啟動、記憶體與平台準備", "目前沒有"],
            ["DXE", "載入多數驅動程式", "有（從 Profiler 載入起）"],
            ["BDS", "連線裝置、處理開機選項", "有（含細分檢查點）"],
            ["ExitBootServices", "結束 Boot Services，準備把控制權交給載入程式", "有（自動測試路徑）"],
        ],
    )
    add_para(
        doc,
        "重點：計時起點是 BootTimeProfilerDxe 被載入時，不是按下電源的瞬間，"
        "也不是整段 DXE 的絕對起點。",
    )

    # ---- 3 ----
    add_heading(doc, "3. 目標與完成項目", 1)
    add_para(doc, "為什麼要做", bold=True)
    add_para(
        doc,
        "先把 DXE／BDS 拆成可對照的大區間；再在 BDS 裡把「連線裝置、刷新開機選項」等步驟分開記錄。"
        "目的是分析時間花在哪裡，不會直接縮短開機時間。",
    )
    add_para(doc, "實際完成了什麼", bold=True)
    add_bullets(
        doc,
        [
            "記錄 EndOfDxe、ReadyToBoot、ExitBootServices 之間的時間（BootTimeSummary）",
            "在 BDS 記錄連線裝置／刷新開機選項等開始與結束時間（BootTimeBdsBreakdown）",
            "可選地收集執行時間較長的項目（BootTimeHotspotLog；常為階段備援）",
            "自動測試：BootTimeDump 印 CSV 並呼叫 ExitBootServices",
            "互動查看：BootTimeDashboard 文字選單（不呼叫 ExitBootServices）",
            "主機腳本 run_benchmark.ps1／compare_ab.ps1 重複開機與比較統計",
        ],
    )

    # ---- 4 ----
    add_heading(doc, "4. 系統架構", 1)
    add_para(doc, "兩個套件怎麼分工", bold=True)
    add_table(
        doc,
        ["目錄", "角色"],
        [
            ["OvmfPkg", "虛擬機用的 UEFI 韌體平台（能開機）"],
            ["BootTimeOptPkg", "本專案的量測套件（記時間、Dump、Dashboard）"],
        ],
    )
    add_para(
        doc,
        "建置時由 OvmfPkgX64.dsc／.fdf 把 BootTimeOptPkg 的模組編進 OVMF；"
        "BDS 平台程式 PlatformBootManagerLib 會呼叫 BootTimeBdsProbeLib 打點。",
    )

    add_para(doc, "資料怎麼流動（自動測試）", bold=True)
    add_para(
        doc,
        "開機 DXE／BDS → Profiler／BdsProbe／Hotspot 打點並 SetVariable 寫入 NVRAM → "
        "BootTimeDump GetVariable 印 CSV → 呼叫 ExitBootServices → Profiler 用序列埠補最終欄位 → "
        "主機 PowerShell 擷取並統計。",
    )
    add_para(
        doc,
        "文字流程：打點模組 → NVRAM（BootTimeSummary／BootTimeBdsBreakdown／BootTimeHotspotLog）"
        " → Dump 或 Dashboard →（自動測）序列埠 CSV → run_benchmark／compare_ab。",
    )

    add_para(doc, "NVRAM 三種主要資料", bold=True)
    add_table(
        doc,
        ["變數名稱", "內容"],
        [
            ["BootTimeSummary", "大階段時間與是否走到各事件的旗標"],
            ["BootTimeBdsBreakdown", "BDS 內更細的各段時間（Connect／Refresh 等）"],
            ["BootTimeHotspotLog", "執行時間較長項目的排行（或階段備援）"],
        ],
    )
    add_para(
        doc,
        "結構定義見 Include/Guid/BootTimeOptPkgVariable.h。"
        "旗標為 0 且時間為 0，代表事件未發生，不是耗時極短。",
    )

    # ---- 5 ----
    add_heading(doc, "5. 各模組簡單說明", 1)
    modules = [
        (
            "BootTimeProfilerDxe",
            "DXE 驅動。在 EndOfDxe、ReadyToBoot、ExitBootServices 讀效能計數器，"
            "寫入 BootTimeSummary；EBS 時用序列埠補最終 CSV。",
        ),
        (
            "BootTimeBdsProbeLib",
            "函式庫。提供開始／結束打點 API；由 BdsPlatform、Profiler、Dump 呼叫，"
            "結果寫入 BootTimeBdsBreakdown。",
        ),
        (
            "BootTimeHotspotDxe",
            "DXE 驅動。ReadyToBoot 時若兩個 PCD 都開啟，嘗試讀 PerformanceEx；"
            "沒有細項就改記大階段時間。",
        ),
        (
            "BootTimeDump",
            "UEFI 應用。自動測試用 BOOTX64：讀 NVRAM、印 CSV、補 LoadPath，"
            "再呼叫 ExitBootServices；不載入作業系統。",
        ),
        (
            "BootTimeDashboard",
            "UEFI 應用。在 Shell 用手操作選單查看量測／系統／記憶體；不呼叫 EBS。",
        ),
        (
            "run_benchmark.ps1／compare_ab.ps1",
            "Windows 主機腳本：重複 QEMU 開機抓 CSV、算統計，並比較兩組結果。",
        ),
    ]
    for name, desc in modules:
        add_para(doc, name, bold=True)
        add_para(doc, desc)

    add_para(doc, "兩種執行方式", bold=True)
    add_table(
        doc,
        ["用途", "程式", "說明"],
        [
            ["自動測試", "BootTimeDump.efi", "設為 BOOTX64；印 CSV 後呼叫 EBS"],
            ["互動查看", "BootTimeDashboard.efi", "Shell 執行；不呼叫 EBS"],
        ],
    )

    # ---- 6 ----
    add_heading(doc, "6. BDS 細分怎麼量", 1)
    add_para(
        doc,
        "OVMF 在 PlatformBootManagerBeforeConsole 內才送出 EndOfDxe。"
        "本專案把 EndOfDxe → ReadyToBoot 再拆成較細的段落：",
    )
    add_table(
        doc,
        ["欄位", "意義"],
        [
            ["PreConnectNs", "EndOfDxe → 開始連線裝置"],
            ["ConnectAllNs", "連線裝置本身（Connect 序列）"],
            ["RefreshBootOptionsNs", "刷新開機選項清單"],
            ["PostRefreshToReadyToBootNs", "刷新結束 → ReadyToBoot 事件"],
            ["LoadPathNs", "ReadyToBoot → Dump 進入（屬下一段，不計入上面四段）"],
        ],
    )
    add_para(
        doc,
        "四個階段（PreConnect＋Connect＋Refresh＋PostRefresh）的時間加總，"
        "應接近 EndOfDxeToReadyToBootNs（允許小誤差）。"
        "這些數字用來分析時間花在哪裡，不會直接縮短開機時間。",
    )
    add_para(
        doc,
        "實作提示：Connect／Refresh 發生在 PlatformBootManagerLib；"
        "ReadyToBoot 回呼在 Profiler。不同 .efi 不能共用 STATIC 變數，"
        "因此把原始 tick 一併存進 NVRAM，另一個映像才能接著算間隔。",
    )

    # ---- 7 ----
    add_heading(doc, "7. 實驗與結果怎麼解讀", 1)
    add_para(doc, "兩組設定", bold=True)
    add_para(
        doc,
        "baseline：Policy／Hotspot 皆關閉（較少額外紀錄）。"
        "optimized：兩者開啟（啟用額外效能紀錄）。"
        "optimized 不是「已經改過開機路徑」的意思。",
    )

    add_para(doc, "本次測試結果（2026-11-07，每組 10 次，QEMU TCG）", bold=True)
    add_para(
        doc,
        "資料夾：baseline-20261107-214115、optimized-20261107-214243、"
        "ab-20261107-ebs-policy-hotspot。全部 ExitBootServicesReached=1。",
    )
    add_table(
        doc,
        ["測量區間", "Baseline 平均", "Optimized 平均", "差值"],
        [
            ["EndOfDxe → ReadyToBoot", "718.43 ms", "676.94 ms", "−41.49 ms"],
            ["Driver 入口 → ExitBootServices", "1,527.85 ms", "1,492.54 ms", "−35.31 ms"],
        ],
    )
    add_para(
        doc,
        "在本次各 10 次的 QEMU 測試中，啟用額外效能紀錄的設定，"
        "測得 EndOfDxe 到 ReadyToBoot 的平均時間約減少 41.49 毫秒。"
        "不過，這兩組設定並沒有直接修改開機執行流程，因此結果只能說明此次測試觀察到的時間差異，"
        "不能直接證明已完成開機加速。",
    )
    add_bullets(
        doc,
        [
            "中位數差約 36.2 ms（EndOfDxe→ReadyToBoot）",
            "baseline 同組 Max−Min 約 93.9 ms（變異本身就不小）",
            "Hotspot 定稿實驗多為階段備援（PHASE），不是每個驅動的詳細排名",
        ],
    )

    add_para(doc, "BDS 細分單次驗證摘要", bold=True)
    add_para(
        doc,
        "單次 baseline 驗證（見 docs/BDS_Breakdown.md）：EndOfDxe→ReadyToBoot 約 617.5 ms；"
        "其中 PreConnect 約 379.6 ms 最大，ConnectAll 約 196.2 ms，Refresh 約 9.2 ms，"
        "PostRefresh 約 21.0 ms；四段加總約 606.0 ms（與相位差約 11.5 ms）。"
        "LoadPath 約 196.4 ms，屬 ReadyToBoot→EBS。",
    )

    # ---- 8 ----
    add_heading(doc, "8. 目錄結構與關鍵檔案", 1)
    add_para(
        doc,
        "edk2/BootTimeOptPkg/ 放量測套件；edk2/OvmfPkg/ 放平台整合（dsc／fdf、"
        "PlatformBootManagerLib 打點）。",
    )
    add_table(
        doc,
        ["位置", "角色（點到為止）"],
        [
            ["Include/Guid/", "NVRAM 變數名稱與結構（資料合約）"],
            ["Include/Library/", "函式庫 API 宣告"],
            ["Library/BootTimeBdsProbeLib/", "BDS 打點實作"],
            ["BootTime* Dxe／Dump／Dashboard", "驅動與應用進入點"],
            ["BootTimeOptPkg.dec", "套件 GUID、PCD"],
            ["OvmfPkgX64.dsc／.fdf", "把量測模組編進韌體映像"],
            ["tools/*.ps1", "主機自動化測試與比較"],
        ],
    )

    # ---- 9 ----
    add_heading(doc, "9. 限制與後續", 1)
    add_bullets(
        doc,
        [
            "QEMU TCG 受執行電腦負載影響，不宜跨機或跨日直接比絕對時間",
            "Dump 呼叫 EBS 但不載入作業系統；Dashboard 不呼叫 EBS",
            "Hotspot 常為階段備援，不是 per-driver 排名",
            "NVRAM 可能殘留前次開機資料；Dashboard 會提醒",
            "optimized／Policy 只控制是否多開紀錄，不代表已完成開機加速",
        ],
    )
    add_para(doc, "可再往下做的方向", bold=True)
    add_para(
        doc,
        "依 BDS 細分結果，對最大的桶（例如 PreConnect）再加更細檢查點，"
        "或改善 PerformanceEx／gauge 路徑，讓 Hotspot 真的能對到模組。"
        "若要涵蓋更早階段，需另做 PEI 等量測（目前不在範圍內）。",
    )

    add_heading(doc, "附錄：環境與操作概要", 1)
    add_para(
        doc,
        "需要完整 edk2、VS2022、NASM／iASL、QEMU、Python。"
        "整合 overlay 後建 OVMF，產物含 BootTimeDump.efi、BootTimeDashboard.efi。"
        "自動測試用 run_benchmark.ps1（-Mode baseline 或 optimized）；"
        "比較用 compare_ab.ps1。完整指令與欄位說明見專案 README.md。",
    )

    footer = doc.add_paragraph()
    run = footer.add_run(
        "本文件由 docs/generate_project_doc.py 產生；數字取材自 README、"
        "AB_Results.md、BDS_Breakdown.md，未另行宣稱加速成效。"
    )
    set_run_font(run, size=9)

    return doc


def main():
    out_dir = Path(__file__).resolve().parent
    out_path = out_dir / "專案說明_開機時間量測.docx"
    doc = build_document()
    doc.save(out_path)
    print(f"Wrote: {out_path}")


if __name__ == "__main__":
    main()
