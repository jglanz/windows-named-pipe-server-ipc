// #include <boost/interprocess/windows_shared_memory.hpp>
// #include <boost/interprocess/mapped_region.hpp>
// #include <boost/asio.hpp>
//
// #include <cstring>
// #include <cstdlib>
// #include <string>
// #include <iostream>
// #include <csignal>
//
// std::atomic<bool> running(true);
//
// // Signal handler for SIGINT
// static void handleSignal(int signal) {
//   if (signal == SIGINT) {
//     std::cout << "\nSIGINT received. Exiting gracefully...\n";
//     running = false;
//   }
// }
//
// int main(int argc, char *argv[])
// {
//   using namespace boost::interprocess;
//
//   std::signal(SIGINT, handleSignal);
//
//
//   windows_shared_memory shm (open_or_create, "mypipe", read_write, 1000);
//
//   //Map the whole shared memory in this process
//   mapped_region region(shm, read_write);
//
//   //Write all the memory to 1
//   std::memset(region.get_address(), 1, region.get_size());
//
//   //Launch child process
//   // std::string s(argv[0]); s += " child ";
//   while (running) {
//
//   }
//   if(0 != std::system(s.c_str()))
//     return 1;
//
//   // if(argc == 1){  //Parent process
//   //   //Create a native windows shared memory object.
//   //
//   //   //windows_shared_memory is destroyed when the last attached process dies...
//   // }
//   // else{
//   //   //Open already created shared memory object.
//   //   windows_shared_memory shm (open_only, "mypipe", read_only);
//   //
//   //   //Map the whole shared memory in this process
//   //   mapped_region region(shm, read_only);
//   //
//   //   //Check that memory was initialized to 1
//   //   char *mem = static_cast<char*>(region.get_address());
//   //   for(std::size_t i = 0; i < region.get_size(); ++i)
//   //     if(*mem++ != 1)
//   //       return 1;   //Error checking memory
//   //   return 0;
//   // }
//   return 0;
// }

#include <windows.h>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <string>
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

std::atomic<bool> running(true);


// Signal handler for SIGINT
static void handleSignal(int signal) {
  if (signal == SIGINT) {
    std::cout << "\nSIGINT received. Exiting gracefully...\n";
    running = false;
  }
}

int main(int argc, char* argv[]) {

  HANDLE hPipe;
  DWORD dwWritten;

  std::signal(SIGINT, handleSignal);

  char buffer[1024] = "Hello from C++!";

  // Open the named pipe for writing
  hPipe = CreateNamedPipeW(
    L"\\\\.\\pipe\\mypipe",
    PIPE_ACCESS_DUPLEX,
    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
    // FILE_FLAG_FIRST_PIPE_INSTANCE is not needed but forces CreateNamedPipe(..) to fail if the pipe already exists...
    1,
    1024 * 16,
    1024 * 16,
    NMPWAIT_USE_DEFAULT_WAIT,
 nullptr
  );
  // ConnectNamedPipe(hPipe,NULL);
  if (hPipe == INVALID_HANDLE_VALUE) {
    std::cerr << "Error connecting to pipe: " << GetLastError() << std::endl;
    return 1;
  }

  // Write data to the pipe

  std::uint32_t counter{0};
  while (running && hPipe != INVALID_HANDLE_VALUE) {
    if (ConnectNamedPipe(hPipe, nullptr) != FALSE) // wait for someone to connect to the pipe
    {
      while (running) {
        ++counter;
        std::string s = std::format("Count is {}", counter);
        if (!WriteFile(hPipe, s.c_str(), s.length(), &dwWritten, nullptr)) {
          std::cerr << "Error writing to pipe: " << GetLastError() << std::endl;
          return 1;
        }

        if (counter % 100 == 0) {
          std::cout << std::format("Sent {0} messages", counter) << std::endl;
        }
        std::this_thread::sleep_for(100ms);
      }
    }

    DisconnectNamedPipe(hPipe);

  }
  // while ()
  // {
  //   if (ConnectNamedPipe(hPipe, NULL) != FALSE)   // wait for someone to connect to the pipe
  //   {
  //     while (ReadFile(hPipe, buffer, sizeof(buffer) - 1, &dwRead, NULL) != FALSE)
  //     {
  //       /* add terminating zero */
  //       buffer[dwRead] = '\0';
  //
  //       /* do something with data in buffer */
  //       printf("%s", buffer);
  //     }
  //   }
  //
  //   DisconnectNamedPipe(hPipe);
  // }
  // std::cout << "Message sent successfully." << std::endl;

  CloseHandle(hPipe);
  return 0;
}
