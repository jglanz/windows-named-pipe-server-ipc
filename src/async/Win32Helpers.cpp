#include "Win32Helpers.h"

namespace IPC {
  std::string GetLastErrorAsString(DWORD err) {

    if (err == 0) {
      return std::string(); // No error
    }

    LPVOID lpMsgBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        err,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPTSTR>(&lpMsgBuf),
        0,
 nullptr
    );
    std::string message(static_cast<LPSTR>(lpMsgBuf));
    LocalFree(lpMsgBuf);
    return message;
  }
}