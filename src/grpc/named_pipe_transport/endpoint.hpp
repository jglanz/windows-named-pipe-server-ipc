#pragma once
#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/codegen/status.h>
#include <windows.h>
#include <string>
#include <memory>
#include <absl/functional/any_invocable.h>

namespace named_pipe {

class WinNamedPipeEndpoint {
public:
    explicit WinNamedPipeEndpoint(HANDLE pipe) : pipe_(pipe) {}
    
    ~WinNamedPipeEndpoint() {
        if (pipe_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_);
        }
    }

    bool Read(absl::AnyInvocable<void(bool) const> on_read,
              grpc::ByteBuffer* buffer) {
        char temp_buffer[8192];
        DWORD bytesRead;
        
        if (!ReadFile(pipe_, temp_buffer, sizeof(temp_buffer), &bytesRead, nullptr)) {
            on_read(false);
            return false;
        }
        
        grpc::Slice slice(temp_buffer, bytesRead);
        grpc::ByteBuffer tmp_buffer(&slice, 1);
        buffer->Swap(&tmp_buffer);
        
        on_read(true);
        return true;
    }

    bool Write(absl::AnyInvocable<void(bool) const> on_write,
               grpc::ByteBuffer* data) {
        std::vector<grpc::Slice> slices;
        data->Dump(&slices);
        
        for (const auto& s : slices) {
            DWORD bytesWritten;
            if (!WriteFile(pipe_, s.begin(), s.size(), &bytesWritten, nullptr)) {
                on_write(false);
                return false;
            }
        }
        
        on_write(true);
        return true;
    }

    HANDLE GetHandle() const { return pipe_; }

private:
    HANDLE pipe_;
};

} // namespace named_pipe
