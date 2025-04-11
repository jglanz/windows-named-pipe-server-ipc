#pragma once
#include <grpcpp/grpcpp.h>
#include <windows.h>
#include <atomic>
#include <string>
#include "endpoint.hpp"

namespace named_pipe {

class WinNamedPipeListener {
public:
    explicit WinNamedPipeListener(const std::string& pipe_name) 
        : pipe_name_(pipe_name), running_(true) {}

    ~WinNamedPipeListener() {
        running_ = false;
        if (pipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_);
        }
    }

    bool Start() {
        pipe_ = CreateNamedPipe(
            pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            8192,  // Output buffer size
            8192,  // Input buffer size
            0,     // Default timeout
            nullptr);

        if (pipe_ == INVALID_HANDLE_VALUE) {
            return false;
        }
        return true;
    }

    std::unique_ptr<WinNamedPipeEndpoint> Accept() {
        if (!ConnectNamedPipe(pipe_, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
            return nullptr;
        }
        
        // Create a new endpoint for the client, and a new pipe instance for the listener
        auto endpoint = std::make_unique<WinNamedPipeEndpoint>(pipe_);
        
        // Create a new pipe for future clients
        pipe_ = CreateNamedPipe(
            pipe_name_.c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            8192,
            8192,
            0,
            nullptr);
            
        return endpoint;
    }

    bool IsRunning() const { return running_; }

private:
    std::string pipe_name_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    std::atomic<bool> running_;
};

} // namespace named_pipe
