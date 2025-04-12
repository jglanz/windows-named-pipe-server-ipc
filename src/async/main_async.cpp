#include <windows.h>
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <string>
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>

#include "NamedPipeServer.h"

using namespace std::chrono_literals;

namespace {
  std::atomic_bool running{true};


  // Signal handler for SIGINT
  static void handleSignal(int signal) {
    if (signal == SIGINT) {
      std::cout << "\nSIGINT received. Exiting gracefully...\n";
      running = false;
    }
  }


}

int main(int argc, char* argv[]) {


  std::signal(SIGINT, handleSignal);

  IPC::NamedPipeServer server([&] (std::size_t size, const BYTE * data, auto header, IPC::NamedPipeConnection* connection, IPC::NamedPipeServer* serverPtr) {
    std::string payload(reinterpret_cast<const char*>(data), size);
    spdlog::info("onMessage(clientId={},messageId={},messageSourceId={},connectionId={}): {}",
      header->clientId,
      header->id,
      header->sourceId,
      connection->id(),
      payload);

  });

  server.start(true);


  return 0;
}
