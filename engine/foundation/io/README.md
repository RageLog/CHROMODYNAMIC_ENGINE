# cd::io

**Purpose**: cross-platform synchronous + asynchronous filesystem IO. Replaces std::ifstream / fopen with a uniform Path / Result API that works the same way on Windows, Linux, macOS, Android, iOS.

**Namespace**: `cd::io`.

**Headers**: `cd/io/{File,Path,AsyncFile,DirIter}.hpp`.

**Primary types**:
- `cd::io::Path` -- normalized path object; supports POSIX + Win32 separators interchangeably.
- `cd::io::read_file(path) -> Result<std::vector<std::byte>>` -- blocking read; std::expected error carries platform errno.
- `cd::io::write_file(path, span)` -- blocking write.
- `cd::io::AsyncFile` -- iouring (Linux), IOCP (Windows), kqueue (macOS) async backend; submit + poll.
- `cd::io::DirIter` -- range-based directory iteration with stat-on-demand.

**Test command**: `ctest --preset ninja-debug -R cd_test_io --output-on-failure`.

**Notes**:
- Header-only public API; platform backends live under `cd::io::detail`.
- Used by cd::asset_pak + cd::asset_streaming for mounted-archive reads.
- AsyncFile not yet exposed in samples; production engine ties it to JobGraph via the AsyncSubmit library.
