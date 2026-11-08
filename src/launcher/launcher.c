/* Windows only. The shipped folder keeps just this small program at the top
 * and the real app, with its hundreds of DLLs, plugins and QML files, in the
 * "app" subfolder. Windows looks for an exe's DLLs next to the exe, so the
 * real app has to live beside them; this launcher starts it, passes on the
 * command line (so opening a file with it works), and returns its exit code.
 * Plain C, linked statically: it needs no DLLs of its own. */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static const wchar_t* skipProgramName(const wchar_t* cmd)
{
    if (*cmd == L'"') {
        ++cmd;
        while (*cmd && *cmd != L'"')
            ++cmd;
        if (*cmd == L'"')
            ++cmd;
    } else {
        while (*cmd && *cmd != L' ' && *cmd != L'\t')
            ++cmd;
    }
    while (*cmd == L' ' || *cmd == L'\t')
        ++cmd;
    return cmd;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, PWSTR unused, int show)
{
    (void)instance; (void)prev; (void)unused; (void)show;

    wchar_t exe[MAX_PATH * 4];
    DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH * 4);
    if (len == 0 || len >= MAX_PATH * 4)
        return 1;
    wchar_t* slash = exe + len;
    while (slash > exe && *slash != L'\\')
        --slash;
    slash[1] = L'\0';

    static const wchar_t kApp[] = L"app\\CorporalJackie.exe";
    lstrcatW(exe, kApp);

    const wchar_t* args = skipProgramName(GetCommandLineW());
    /* Quoted program path, a space, then the original arguments. */
    int size = lstrlenW(exe) + lstrlenW(args) + 4;
    wchar_t* cmd = (wchar_t*)HeapAlloc(GetProcessHeap(), 0, size * sizeof(wchar_t));
    if (!cmd)
        return 1;
    lstrcpyW(cmd, L"\"");
    lstrcatW(cmd, exe);
    lstrcatW(cmd, L"\" ");
    lstrcatW(cmd, args);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        MessageBoxW(NULL, L"Could not start app\\CorporalJackie.exe. "
                          L"Keep the \"app\" folder next to this program.",
                    L"Corporal Jackie", MB_ICONERROR);
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}
