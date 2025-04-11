#include <grpcpp/create_channel.h>
#include <csignal>
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <format>
#include <named_pipe_transport/endpoint.hpp>

#include "service.grpc.pb.h"

std::atomic<bool> running{true};

static void handleSignal(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nSIGINT received. Shutting down...\n";
        running = false;
    }
}

class NamedPipeClient {
public:
    explicit NamedPipeClient(const std::string& pipe_name) {
        // Connect to the named pipe
        HANDLE pipe = CreateFile(
            pipe_name.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        if (pipe == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to connect to named pipe: " + 
                                     std::to_string(GetLastError()));
        }
        
        endpoint_ = std::make_unique<named_pipe::WinNamedPipeEndpoint>(pipe);
        
        // Create a custom channel using our named pipe endpoint
        grpc::ChannelArguments args;
        channel_ = grpc::CreateCustomChannel(
            pipe_name,
            grpc::InsecureChannelCredentials(),
            args);
        
        stub_ = namedpipe::NamedPipeService::NewStub(channel_);
    }

    void StreamData() {
        grpc::ClientContext context;
        std::shared_ptr<grpc::ClientReaderWriter<namedpipe::Request, namedpipe::Response>> 
            stream(stub_->StreamData(&context));

        // Start read thread
        std::thread reader([this, stream]() {
            namedpipe::Response response;
            while (running && stream->Read(&response)) {
                std::cout << "Received: " << response.message() 
                         << " (counter: " << response.counter() << ")\n";
            }
        });

        // Write requests
        namedpipe::Request request;
        uint32_t counter = 0;
        while (running) {
            request.set_message(std::format("Message {}", ++counter));
            if (!stream->Write(request)) {
                std::cerr << "Write failed" << std::endl;
                break;
            }
            
            if (counter % 100 == 0) {
                std::cout << "Sent " << counter << " messages" << std::endl;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        stream->WritesDone();
        reader.join();
    }

private:
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<namedpipe::NamedPipeService::Stub> stub_;
    std::unique_ptr<named_pipe::WinNamedPipeEndpoint> endpoint_;
};

int main() {
    const std::string pipe_name = "\\\\.\\pipe\\grpc_custom_pipe";
    std::signal(SIGINT, handleSignal);

    try {
        std::cout << "Connecting to server at " << pipe_name << std::endl;
        NamedPipeClient client(pipe_name);
        std::cout << "Connected. Starting data stream..." << std::endl;
        client.StreamData();
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Client shutdown complete" << std::endl;
    return 0;
}
