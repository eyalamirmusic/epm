# What epm needs from eacp

epm uses eacp for files, paths, SHA-256, processes, the cross-process lock,
HTTP downloads, zip, temporary files, the environment, platform checks, the
console, time and argument parsing. Below are the pieces eacp (at the locked
develop commit) does not have yet. Each is kept in one place in epm, and that copy is
deleted once eacp has the proposed addition.

| Proposed eacp addition | Header | Kept in epm |
|---|---|---|
| `FilePath::longPathSafe()`: absolute, with the `\\?\` prefix on Windows | `Core/Utils/FilePath.h` | `Util/LongPath.{h,cpp}` |
| `FilePath::canonical()` | `Core/Utils/FilePath.h` | `canonical` in `Apps/epm/Main.cpp` |
| `Files::writeFileAtomically` that creates parents and returns false rather than throwing | `Core/Utils/Files.h` | `writeTextFile` in `Util/Text.cpp` |
| `Files::findExecutable(name)` (search `PATH`) | `Core/Utils/Files.h` | `findExecutable` in `Util/Command.cpp` |
| `Strings::splitLines(text)` (drops `\r` and empty lines) | `Core/Utils/Strings.h` | `splitLines` in `Util/Text.cpp` |
| `Strings::replaceAll(text, from, to)` | `Core/Utils/Strings.h` | `replaceAll` in `Util/Text.cpp` |
| `Strings::lastLines(text, count)` | `Core/Utils/Strings.h` | `lastLines` in `Util/Command.cpp` |

eacp #198 added the rest (`Sha256`, `Strings::toHex`, `FilePath::relativeTo`,
`lexicallyNormal`, `currentDirectory`, `withSuffix`, `Files::readSymlink`, the
`string_view` `writeFileAtomically`, `wide()` in native separators), and epm's
copies are gone.
