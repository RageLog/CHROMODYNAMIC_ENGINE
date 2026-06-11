# probe12 + warning-class probe summaries (2026-06-11, phases 1067-1089)

Raw run-clang-tidy outputs are gitignored (15 MB); this file preserves
the counts the .clang-tidy rationale block and the phase 1067-1070 /
1086-1089 commit messages reference.


## probe12_chunk1.txt

| rule | diagnostics (incl. dupes across TUs) |
|---|---|
| fuchsia-overloaded-operator | 1673 |
| llvm-header-guard | 1530 |
| fuchsia-default-arguments-calls | 1472 |
| fuchsia-default-arguments-declarations | 1027 |
| fuchsia-statically-constructed-objects | 65 |
| llvm-namespace-comment | 64 |
| google-default-arguments | 61 |
| llvm-include-order | 44 |
| google-explicit-constructor | 33 |
| fuchsia-trailing-return | 19 |
| google-runtime-int | 9 |
| google-build-using-namespace | 6 |

## probe12_chunk2.txt

| rule | diagnostics (incl. dupes across TUs) |
|---|---|
| fuchsia-overloaded-operator | 1357 |
| llvm-header-guard | 728 |
| fuchsia-default-arguments-declarations | 405 |
| fuchsia-default-arguments-calls | 286 |
| fuchsia-statically-constructed-objects | 37 |
| google-default-arguments | 36 |
| llvm-include-order | 17 |
| llvm-namespace-comment | 10 |
| google-build-using-namespace | 9 |

## probe12_chunk3.txt

| rule | diagnostics (incl. dupes across TUs) |
|---|---|
| fuchsia-default-arguments-calls | 4649 |
| fuchsia-overloaded-operator | 3583 |
| llvm-header-guard | 2806 |
| fuchsia-default-arguments-declarations | 2101 |
| llvm-namespace-comment | 172 |
| fuchsia-statically-constructed-objects | 128 |
| llvm-include-order | 98 |
| google-runtime-int | 96 |
| google-default-arguments | 94 |
| google-explicit-constructor | 69 |
| fuchsia-trailing-return | 19 |
| google-build-using-namespace | 3 |

## probe_warning_classes.txt

| rule | diagnostics (incl. dupes across TUs) |
|---|---|
| readability-use-std-min-max | 53 |
| modernize-use-std-numbers | 46 |
| readability-avoid-nested-conditional-operator | 39 |
| misc-use-internal-linkage | 10 |
| readability-redundant-casting | 1 |
