# PE-bear tests

Unit tests and model checks for PE-bear's own code. Disabled by default; the
ordinary build is untouched.

```sh
cmake -S . -B build -DUSE_QT5=ON -DPEBEAR_BUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Everything runs offline and headless, in well under a second.

## What is covered

| Target | Covers |
|---|---|
| `tst_minimalpe` | the synthetic PE fixture itself |
| `tst_bearvers` | `BearVers` version parsing, comparison and rendering |
| `tst_modification` | `ModifBackup`, `ResizeBackup`, `OperationBackup`, `ModificationHandler` — the undo path |
| `tst_commenthandler` | tags: set/clear, `.tag` save/load, malformed input |
| `tst_stringscollection` | the strings store, including wide-string byte sizes |
| `tst_treemodel` | `TreeItem` tree bookkeeping and `TreeModel`, with a contract check |
| `tst_pemodels` | every concrete PE view, under `QAbstractItemModelTester` |

## Fixtures are synthesised, not committed

`MinimalPe` builds valid PE32 and PE32+ images in memory, with options for
section count, imports and truncation. `PeFixture` writes one to a temporary
file and wraps it in a `PeHandler`, which is what the views need.

No executables are checked in. A committed `.exe` in a reversing tool's
repository is a liability, the interesting header fields stay readable as code,
and a test can vary one field at a time to say exactly what it is exercising.
`tst_minimalpe` guards the fixture, so if bearparser stops accepting these
images it fails first and the rest of the failures are explained.

`PeHandler` is reference-counted with a protected destructor and deletes its
`PEFile` and `FileBuffer` itself, so `PeFixture` disposes of it with `release()`
and every model must be destroyed before the fixture.

## Model checks

`tst_pemodels` instantiates all 18 concrete views and hands each to
`QAbstractItemModelTester`, which verifies that `index()`, `parent()`,
`rowCount()`, `columnCount()` and `hasIndex()` agree with one another. The sweep
runs over a sparse PE, a PE with imports, and a 64-bit image, because which
directories are present decides which code paths exist at all.

A model can look correct field by field and still break the contract in a way
that surfaces as a blank row, a duplicated entry or a crash inside `QTreeView`.
That is what these catch — and they caught several; see below.

## Findings

Three defects were found and fixed alongside the tests:

- **`BearVers(QString)` read past the end of its component list.** The ternary
  choosing the fourth component was inverted, so a three-part version like
  `"0.7.2"` indexed `strings[3]` on a three-element list — a segfault under the
  test — and a four-part version discarded its fourth component instead of
  reading it. Latent in the shipped application, which only uses the four-int
  constructor. Covered by `parsesThreeComponentVersions`.
- **`TreeModel::data()` was missing a `return`.** `if (role != Qt::DisplayRole) { QVariant(); }`
  constructs a temporary and discards it, so every role — `SizeHintRole`,
  `FontRole`, `DecorationRole` — received the display string.
  `QAbstractItemModelTester` flagged it as a `QSize` conversion failure.
- **`TreeItem::data()` returned a hardcoded `"demo"`**, ignoring the values the
  item was constructed with, which also made `TreeModel::headerData()` return
  `"demo"` for every column.

One defect is **recorded but not fixed**, because it is systemic rather than a
local slip:

- **Flat models report children for a valid parent.**
  `WrapperTableModel::rowCount()` ignores its `parent` argument and returns the
  field count whatever it is handed, and `PeTableModel::index()` returns
  `createIndex(row, column)` without consulting `parent` or bounds-checking
  anything. Meanwhile `parent()` — inherited from `TreeModel` with a null
  `rootItem` — always reports no parent. The two contradict each other: the
  model describes an infinitely deep tree in which no child knows its parent.

  Harmless in the application today only because these models are shown in
  table views, which never pass a valid parent. A `QTreeView` over one would put
  an expander on every row and recurse without end.

  `flatModelsShouldNotClaimChildren` pins this to a single assertion per model,
  marked `QEXPECT_FAIL`; `modelsSatisfyTheModelContract` skips the affected
  models so the rest of the sweep stays meaningful. Both detect the condition by
  behaviour rather than by a list of names, so they clear themselves once the
  models are fixed — and QtTest then reports an unexpected pass rather than
  letting a stale exception mask a regression.

  The fix is a guard at the top of each `rowCount()`:

  ```cpp
  if (parent.isValid()) return 0;
  ```

  in `WrapperTableModel` and in the subclasses that override it, plus a
  `hasIndex()` check in `PeTableModel::index()`. It touches around ten files,
  which is why it is proposed rather than bundled into a testing change.

## How the targets are built

PE-bear is built as a single executable, so there is no library for a test to
link against. Rather than restructure the production build, each target
compiles the sources it needs, reusing the source lists already defined in
`pe-bear/CMakeLists.txt` — CMake variables reach subdirectories.

Four classes reachable from the model checks are header-only `Q_OBJECT`s
(`StringsCollection`, `CollectorThread`, `FollowableOffsetedView`,
`MouseTrackingTableView`). AUTOMOC only emits a meta-object for a header listed
as a target source, so those are named individually — the same reason the
application passes `${pebear_hdrs}` through `qt_wrap_cpp`.
