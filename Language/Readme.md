# How to load a language file

1. The directory with languages (named `Language`) must be in same directory as PE-bear executable, or in User Data Directory (`Settings` -> `Configure...` -> `User Data Directory`).
2. Create a subdirectory with the name of the language version that you want to add (i.e. `zh_CN`)
3. Rename the Language file to `PELanguage.qm` and put it into the created folder
4. Restart PE-bear. Now, you should see the added language under `Settings` -> `Configure...` -> `Language`.
5. Select your language from the list, and restart PE-bear. The interface should be updated to the new language.

# How to translate
Download the language file, use QT linguist to load and select the language to be translated, and click Publish to generate qm file after translation.
# reference data
* https://doc.qt.io/qt-5/qtlinguist-index.html
* https://doc.qt.io/qt-5/linguist-translators.html

# Refreshing the string list (maintainers)

`lupdate` scans the sources and adds newly translatable strings to the `.ts`
files. Two things about it were learned the hard way and are worth knowing
before running it:

* **Never pass `-no-obsolete`.** Measured on this repository: it removed nine
  Japanese and three Korean translations outright. Without the flag the same
  entries are kept as `type="vanished"`, text intact, so nothing a translator
  wrote is lost.
* **Watch the line endings.** `lupdate` from a Windows Qt installation writes
  CRLF, which rewrites every line of the file and turns a ninety-line addition
  into a two-thousand-line diff. Convert back to LF before committing.

A plain `lupdate` run over this tree also reshuffles contexts for a handful of
upstream strings, because `DataDirTreeItem`, `OptionalHdrTreeItem` and
`PEFileNTHdrTreeItem` lack the `Q_OBJECT` macro and `lupdate` says so. Their
`tr()` calls are attributed to a surrounding class instead, so entries that
still exist in the code are marked vanished and reappear under a different
context. Until those classes gain the macro, prefer splicing in only the
entries you meant to add over committing a wholesale regeneration.

`lrelease` compiles a `.ts` into the `PELanguage.qm` that ships. It leaves out
anything still marked `unfinished`, so a newly added string shows its English
source until somebody translates it -- that is the intended behaviour, not a
fault.
