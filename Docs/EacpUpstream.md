# What epm needs from eacp

epm uses eacp for files, paths, processes, the cross-process lock, HTTP
downloads, zip, temporary files, the environment, platform checks, the console,
time and argument parsing. Below are the pieces eacp (at the pinned develop
commit) does not have yet. Each is kept in one place in epm, and that copy is
deleted once eacp has the proposed addition.

| Proposed eacp addition | Header | Kept in epm |
|---|---|---|
| `Sha256` (`update`, `finish`), `computeSha256Hex(text)`, `computeFileSha256(path)` | `Core/Utils/Sha256.h` | `Util/Sha256.{h,cpp}` |
| `Strings::toHex(Span<const uint8_t>)` | `Core/Utils/Strings.h` | `convertDigestToHex` in `Util/Sha256.cpp` |
| `FilePath::wide()` with `\` separators, as `nativeString()` already has, so a `\\?\` long path works | `Core/Utils/FilePath.h` | `Util/LongPath.{h,cpp}` |
| `FilePath::relativeTo(base)`, `FilePath::lexicallyNormal()` | `Core/Utils/FilePath.h` | `getPathForLock`, `makeAbsolute`, `getPathForUser` in `Resolve/Commands.cpp` |
| `FilePath::currentDirectory()` | `Core/Utils/FilePath.h` | `getDefaultLockFile` in `Apps/epm/Main.cpp`, `getPathForUser` |
| `FilePath::canonical()` | `Core/Utils/FilePath.h` | `canonical` in `Apps/epm/Main.cpp` |
| `FilePath::withSuffix(suffix)` | `Core/Utils/FilePath.h` | `getSiblingWithSuffix` in `Util/Paths.h` |
| `Files::readSymlink(path)` | `Core/Utils/Files.h` | `computeSymlinkHash` in `Lock/MerkleHash.cpp` |
| `Files::writeFileAtomically(path, std::string_view)`, creating parents | `Core/Utils/Files.h` | `writeTextFile` in `Util/Text.cpp` |
| `Files::findExecutable(name)` (search `PATH`) | `Core/Utils/Files.h` | `findExecutable` in `Util/Command.cpp` |
| `Strings::splitLines(text)` (drops `\r` and empty lines) | `Core/Utils/Strings.h` | `splitLines` in `Util/Text.cpp` |
| `Strings::replaceAll(text, from, to)` | `Core/Utils/Strings.h` | `replaceAll` in `Util/Text.cpp` |
| `Strings::lastLines(text, count)` | `Core/Utils/Strings.h` | `lastLines` in `Util/Command.cpp` |

SHA-256 matters most: the rest are thin wrappers, but it is about 170 lines of
real code.
