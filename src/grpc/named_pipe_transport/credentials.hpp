#pragma once
#include <grpcpp/security/credentials.h>
#include <windows.h>
#include <memory>
#include "endpoint.hpp"

namespace named_pipe {

class WinNamedPipeChannelCredentials : public grpc::ChannelCredentials {
public:
    std::shared_ptr<grpc::Channel> CreateChannel(
        const std::string& target,
        const grpc::ChannelArguments& args) override {
        // Connect to the named pipe
        HANDLE pipe = CreateFile(
            target.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            return nullptr;
        }
        
        auto endpoint = std::make_shared<WinNamedPipeEndpoint>(pipe);
        
        // In a full implementation, we would use this endpoint to create a custom channel
        // For simplicity, we're returning a standard insecure channel
        return grpc::CreateCustomChannel(
            target, 
            grpc::InsecureChannelCredentials(), 
            args);
    }

    grpc::SecureChannelCredentials* AsSecureCredentials() override { 
        return nullptr; 
    }
};

class WinNamedPipeServerCredentials : public grpc::ServerCredentials {
public:
    int AddPortToServer(const std::string& addr,
                        grpc::ServerCredentials* creds) override {
        // This would be used by the server builder to add the port
        // In a full implementation, we would store the pipe name
        return 0;
    }
    
    bool IsTlsCredentials() const override { 
        return false; 
    }
    
    bool IsIPv4Supported() override { 
        return false; 
    }
    
    bool IsIPv6Supported() override { 
        return false; 
    }
};

} // namespace named_pipe
