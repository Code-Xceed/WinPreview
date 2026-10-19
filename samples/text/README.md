# Supported Text & Code Files Directory

This directory contains organized sample files for all text, code, configuration, and spreadsheet formats supported by the **WinPreview Mini VS Code Engine**.

---

## 📂 Directory Layout

```
text/
├── 01_programming_languages/       # Modern programming languages & source code
│   ├── python_pipeline.py         (.py)
│   ├── cpp_engine.cpp             (.cpp, .cxx, .cc)
│   ├── c_header.h                 (.h, .hpp, .hxx)
│   ├── typescript_component.tsx   (.ts, .tsx, .mts)
│   ├── javascript_service.js      (.js, .jsx, .mjs, .cjs)
│   ├── csharp_repository.cs       (.cs)
│   ├── rust_algorithm.rs          (.rs)
│   ├── golang_worker.go           (.go)
│   ├── java_service.java          (.java)
│   ├── kotlin_state.kt            (.kt, .kts)
│   ├── swift_viewmodel.swift      (.swift)
│   ├── zig_allocator.zig          (.zig)
│   ├── ruby_processor.rb          (.rb)
│   ├── php_controller.php         (.php)
│   ├── lua_script.lua             (.lua)
│   ├── dart_model.dart            (.dart)
│   ├── r_stats.r                  (.r)
│   └── database_queries.sql       (.sql)
│
├── 02_scripts_and_shells/          # Shell scripts & command utilities
│   ├── deploy_service.sh          (.sh, .bash, .zsh)
│   ├── build_pipeline.ps1         (.ps1, .psm1)
│   ├── clean_environment.bat      (.bat)
│   └── quick_launch.cmd           (.cmd)
│
├── 03_web_and_styles/              # Web markup and stylesheets
│   ├── index.html                 (.html, .htm)
│   ├── styles.css                 (.css)
│   ├── variables.scss             (.scss, .sass)
│   └── palette.less               (.less)
│
├── 04_data_and_configs/            # Structured data & configurations
│   ├── package.json               (.json)
│   ├── app_config.xml             (.xml, .xaml)
│   ├── docker-compose.yaml        (.yaml, .yml)
│   ├── settings.toml              (.toml)
│   ├── config.ini                 (.ini, .cfg)
│   ├── .env                       (.env)
│   └── nginx.conf                 (.conf)
│
├── 05_documentation_and_notes/     # Markdown, text notes, logs & diffs
│   ├── README.md                  (.md, .markdown)
│   ├── quick_notes.txt            (.txt, .rtf)
│   ├── server.log                 (.log)
│   ├── feature_changes.diff       (.diff)
│   └── patch_v2.patch             (.patch)
│
└── 06_spreadsheets_and_tables/     # Delimited tabular spreadsheets
    ├── financial_summary.csv      (.csv - Table Grid & Raw Editor)
    ├── customer_dataset.tsv       (.tsv)
    └── inventory_catalog.tab      (.tab)
```

---

## ⚡ Mini VS Code Controls & Shortcuts

* **Type & Edit**: Start typing immediately to edit any file in place.
* **Auto-Indent**: Press `Enter` to create a new line preserving indentation.
* **Save (`Ctrl + S`)**: Directly saves file to disk preserving original encoding (`UTF-8`, `UTF-8 BOM`, `UTF-16 LE`, `ANSI`) and line endings (`CRLF` / `LF`).
* **Undo / Redo (`Ctrl + Z` / `Ctrl + Y`)**: 100-level snapshot history.
* **Select All (`Ctrl + A`)**: Selects entire document text.
* **Cut / Copy / Paste (`Ctrl + X` / `Ctrl + C` / `Ctrl + V`)**: Windows Clipboard integration.
* **Word Wrap (`Alt + Z` or toolbar button)**: Toggle between soft word-wrap and horizontal scrolling.
* **Tabular View Toggle (`T` on spreadsheets)**: Switch between interactive table grid and raw delimited text.
* **Indentation (`Tab` / `Shift + Tab`)**: Indents or unindents single lines or multi-line blocks.
* **Status Bar (Bottom 24px)**: Shows live line & column numbers, character count, clickable tab size, clickable word wrap, encoding, and save indicator.
