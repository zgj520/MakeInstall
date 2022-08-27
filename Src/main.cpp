#include <string>
#include <Windows.h>
#include <codecvt>

std::string wstring2String(const std::wstring& input_wstr) {
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    return converter.to_bytes(input_wstr);
}

BOOL WriteFileData(HANDLE hFile, BYTE* buffer, DWORD dwSize) {
    DWORD dwNumWrited = 0;
    do {
        auto ret = WriteFile(hFile, buffer, dwSize, &dwNumWrited, NULL);
        if (!ret || !dwNumWrited) {
            return FALSE;
        }
        dwSize -= dwNumWrited;
        buffer += dwNumWrited;
    } while (dwSize > 0);

    return TRUE;
}

BOOL CopyFileData(HANDLE hDstFile, HANDLE hSrcFile, DWORD dwSize) {
    if (hDstFile == INVALID_HANDLE_VALUE || hSrcFile == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    BOOL retCode = TRUE;
    DWORD dwBufferSize = 1024 * 64;
    BYTE* buffer = new BYTE[1024 * 64];
    DWORD dwNumToRead = 0;
    DWORD dwNumReaded = 0;
    do {
        dwNumToRead = dwSize > dwBufferSize ? dwBufferSize : dwSize;
        memset(buffer, 0, dwBufferSize);
        auto ret = ::ReadFile(hSrcFile, buffer, dwNumToRead, &dwNumReaded, NULL);
        if (!ret) {
            retCode = FALSE;
            break;
        }
        if (dwNumReaded <= 0) {
            break;
        }
        ret = WriteFileData(hDstFile, buffer, dwNumReaded);
        if (!ret) {
            retCode = FALSE;
            break;
        }
        dwSize -= dwNumReaded;
    } while (dwSize > 0);
    return retCode;
}

UINT GetFileCrc32(HANDLE hFile) {
    return 0;
    const int cnSize = 1024 * 4;
    BYTE buffer[cnSize];
    DWORD dwNumReaded = 0;
    ::SetFilePointer(hFile, 0, NULL, FILE_END);
    do {
        auto ret = ::ReadFile(hFile, buffer, cnSize, &dwNumReaded, NULL);
        if (!ret) {
            return 0;
        }
        if (dwNumReaded > 0) {

        }
        else {
            break;
        }
    } while (true);
}

PIMAGE_FILE_HEADER ConvertFileBufferToPEFileHeader(BYTE* pBuffer) {
    PIMAGE_DOS_HEADER dosHeader = (PIMAGE_DOS_HEADER)(pBuffer);
    if (dosHeader == nullptr) {
        return nullptr;
    }
    PIMAGE_NT_HEADERS ntHeader = (PIMAGE_NT_HEADERS)(pBuffer + dosHeader->e_lfanew);
    if (ntHeader == nullptr) {
        return nullptr;
    }
    return &ntHeader->FileHeader;

}

BOOL WriteStartOffset(std::wstring outPath) {
    WCHAR currentProcessPath[MAX_PATH] = {0};
    ::GetModuleFileName(NULL, currentProcessPath, MAX_PATH);
    auto hFile = ::CreateFile(outPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    auto dwFileSize = ::GetFileSize(hFile, NULL);
    if (dwFileSize == 0) {
        return FALSE;
    }

    auto hFileMapping = ::CreateFileMapping(hFile, NULL, PAGE_READWRITE, 0, dwFileSize, NULL);
    if (hFileMapping == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    auto pFileBuffer = (PBYTE)MapViewOfFile(hFileMapping, FILE_MAP_WRITE, 0, 0, dwFileSize);
    if (pFileBuffer == nullptr) {
        return FALSE;
    }

    PIMAGE_FILE_HEADER pfh = ConvertFileBufferToPEFileHeader(pFileBuffer);
    if (pfh == nullptr) {
        return FALSE;
    }
    pfh->TimeDateStamp = dwFileSize;

    UnmapViewOfFile(pFileBuffer);
    CloseHandle(hFileMapping);
    CloseHandle(hFile);
    return TRUE;
}

BOOL MakePacket(std::wstring installExePath, std::wstring zipPath, std::wstring version, std::wstring exeFullName) {
    BOOL bRet = FALSE;

    // 记录安装程序本身的大小，使用的是PE的 TimeDateStamp
    if (!WriteStartOffset(installExePath)) {
        printf("WriteStartOffset error");
        return FALSE;
    }

    auto hExePacket = ::CreateFile(installExePath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    // 写入包开始标记
    ::SetFilePointer(hExePacket, 0, NULL, FILE_END);
    char szStartFlag[] = "ljp.";
    if (!WriteFileData(hExePacket, (BYTE*)szStartFlag, strlen(szStartFlag))) {
        printf("Write start tag error");
        return FALSE;
    }

    auto exeName = wstring2String(exeFullName);

    // 写入exe名称长
    ::SetFilePointer(hExePacket, 0, NULL, FILE_END);
    WORD count = exeName.length();
    printf("exe name:%d, %d", count, sizeof(WORD));
    if (!WriteFileData(hExePacket, (BYTE*)&count, sizeof(count))) {
        printf("Write pkg count error");
        return FALSE;
    }

    // 写入名称
    ::SetFilePointer(hExePacket, 0, NULL, FILE_END);
    if (!WriteFileData(hExePacket, (BYTE*)exeName.c_str(), exeName.length())) {
        printf("Write pkg count error");
        return FALSE;
    }

    // 写入真正的压缩包数据
    auto zipFileHandle = ::CreateFile(zipPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (zipFileHandle == INVALID_HANDLE_VALUE) {
        printf("open zip file error");
        return FALSE;
    }
    ::SetFilePointer(hExePacket, 0, NULL, FILE_END);
    DWORD dwFileSize = ::GetFileSize(zipFileHandle, NULL);
    if (!WriteFileData(hExePacket, (BYTE*)&dwFileSize, sizeof(dwFileSize))) {
        printf("Write zip file size error");
        return FALSE;
    }
    /* 第一版先不处理安全问题
    unsigned int zipFileCrc32 = GetFileCrc32(zipFileHandle);
    WriteFileData(hExePacket, (BYTE*)&zipFileCrc32, sizeof(zipFileCrc32));
    */
    if (!CopyFileData(hExePacket, zipFileHandle, dwFileSize)) {
        printf("copy zip data error");
        return FALSE;
    }
    CloseHandle(zipFileHandle);

    //完成后关闭句柄
    CloseHandle(hExePacket);

    return TRUE;
}


int wmain(int argc, wchar_t* argv[]) {
    if (argc < 4) {
        printf("params error, less 4");
        return -1;
    }
    std::wstring strInstallContainer = argv[1];// 安装程序容器，带UI
    std::wstring binPath = argv[2]; // 需要安装的具体应用程序包
    std::wstring strversion = argv[3]; // 应用程序版本
    std::wstring strExeName = argv[4]; // 启动exe的名称
    std::wstring strOther = argv[5]; // 启动exe的名称
    if (!MakePacket(strInstallContainer, binPath, strversion, strExeName)) {
        printf("MakePacket error");
        return -1;
    }
    auto exeName = wstring2String(strExeName);
    auto version = wstring2String(strversion);
    auto oldFullPath = wstring2String(strInstallContainer);
    auto index1 = oldFullPath.find_last_of("\\");
    auto index2 = oldFullPath.find_last_of("/");
    std::string otherInfo = wstring2String(strOther);
    std::string newName = exeName + "_" + version;
    if (!otherInfo.empty()) {
        newName += "_" + otherInfo + ".exe";
    }
    else {
        newName += ".exe";
    }
    std::string newPath = newName;
    std::string dir = "";

    if (index1 != std::string::npos) {
        dir = oldFullPath.substr(0, index1);
        newPath = dir + "\\" + newName;
    }
    else if (index2 != std::string::npos) {
        dir = oldFullPath.substr(0, index2);
        newPath = dir + "\\" + newName;
    }
    rename(oldFullPath.c_str(), newPath.c_str());
    return 0;
}