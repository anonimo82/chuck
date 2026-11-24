# Windows x64 diagnostic build

Download this branch as ZIP from GitHub, extract it to a writable local folder, and open PowerShell in the extracted repository root. Review the scripts first. Run:

    powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\multimedia\run-windows.ps1

The execution policy option applies only to this process. The script installs or updates MSYS2 and its UCRT64 dependencies, builds the six Chugins with CMake, and runs the 35 smoke checks if a compatible x64 ChucK executable is available. It may require an installer approval. If MSYS2 requests a restart, rerun the same command. Existing MSYS2 installations will be updated.

If no ChucK binary is found, the script tries the installed Visual Studio MSBuild toolchain (v142 and Windows SDK required). Otherwise supply a compatible existing binary using -ChuckExe "C:\path\to\chuck.exe". Visual Studio is not installed automatically. No GitHub credentials or uploads are required.

Send setup-windows.log and build-and-test.log from tools/multimedia if present (review for personal paths before sharing). Output binaries are in build/multimedia-windows/install/chugins. This procedure is experimental and has not been run on Windows yet. Dependency revisions are not pinned; the MIDI commit is recorded in the log.
