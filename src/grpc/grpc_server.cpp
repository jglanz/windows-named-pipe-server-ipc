#include <grpcpp/server_builder.h>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>
#include <iostream>
#include <string>
#include <thread>
#include <grpcpp/grpcpp.h>
#include <windows.h>
#include <winbase.h>
#include <csignal>
#include <atomic>
#include <chrono>
#include "named_pipe_transport/listener.hpp"
#include "service.grpc.pb.h"

std::atomic<bool> running{true};

static void handleSignal(int signal) {
    if (signal == SIGINT) {
        std::cout << "\nSIGINT received. Shutting down...\n";
        running = false;
    }
}

class NamedPipeServiceImpl final : public namedpipe::NamedPipeService::Service {
public:
    grpc::Status StreamData(
        grpc::ServerContext* context,
        grpc::ServerReaderWriter<namedpipe::Response, namedpipe::Request>* stream) override {
        
        namedpipe::Request request;
        namedpipe::Response response;
        uint32_t counter = 0;

        while (running && stream->Read(&request)) {
            auto msg = std::format("Processed: {}", request.message());
            std::cout << msg << counter << std::endl;
            response.set_counter(++counter);
            response.set_message(msg);
            
            if (!stream->Write(response)) {
                return grpc::Status(grpc::StatusCode::INTERNAL, "Write failed");
            }

            if (counter % 100 == 0) {
                std::cout << "Processed " << counter << " messages\n";
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        return grpc::Status::OK;
    }
};

class GrpcNamedPipeServer {
public:
    explicit GrpcNamedPipeServer(const std::string& pipe_name) 
        : pipe_name_(pipe_name), running_(true) {
        
        service_ = std::make_unique<NamedPipeServiceImpl>();

        listener_ = std::make_unique<named_pipe::WinNamedPipeListener>(pipe_name);
        
        if (!listener_->Start()) {
            throw std::runtime_error("Failed to start listener");
        }
        
        server_thread_ = std::thread(&GrpcNamedPipeServer::RunServer, this);
    }
    
    ~GrpcNamedPipeServer() {
        Shutdown();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }
    
    void RunServer() {
        grpc::ServerBuilder builder;
        builder.RegisterService(service_.get());
        
        auto server = builder.BuildAndStart();
        std::cout << "Server ready for connections on " << pipe_name_ << std::endl;

        grpc::ByteBuffer resBuf;
        bool resBufOwned = true;

        // Accept client connections
        while (running_ && listener_->IsRunning()) {
            auto endpoint = listener_->Accept();
            if (endpoint) {
                std::cout << "Client connected" << std::endl;
                // namedpipe::Response res;
                // res.set_message("blah");
                //
                // grpc::SerializationTraits<namedpipe::Request>::Serialize(res, &resBuf, &resBufOwned);
                // endpoint->Write([] (auto ok) {
                //     std::cout << "Client write ok " << ok << std::endl;
                //     // return ok;
                // }, &resBuf);
                //
                // endpoint->Read()
                // In a production environment, you'd want to create a dedicated thread
                // for each client connection and handle the gRPC protocol properly
                // This is simplified for demonstration purposes
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        server->Shutdown();
    }
    
    void Shutdown() {
        running_ = false;
    }

private:
    std::string pipe_name_;
    std::unique_ptr<NamedPipeServiceImpl> service_;
    std::unique_ptr<named_pipe::WinNamedPipeListener> listener_;
    std::thread server_thread_;
    std::atomic<bool> running_;
};

class NamedPipeServer {
public:
    explicit NamedPipeServer(const std::string& pipe_name)
        : pipe_name_(pipe_name), running_(true) {
        Initialize();
    }

    ~NamedPipeServer() {
        Shutdown();
    }

    void Run() {
        // Create gRPC server
        grpc::ServerBuilder builder;

        // Set custom channel argument for named pipe
        builder.AddChannelArgument(GRPC_ARG_MINIMAL_STACK, 1);

        // Register our service
        builder.RegisterService(&service_);

        // Create named pipe specific credentials
        auto creds = grpc::InsecureServerCredentials();
        builder.AddListeningPort("" + pipe_name_, creds);

        // Start the server
        std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
        std::cout << "Server listening on named pipe: " << pipe_name_ << std::endl;

        // Create and configure the named pipe
        pipe_handle_ = CreateNamedPipeA(
            ("" + pipe_name_).c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            PIPE_UNLIMITED_INSTANCES,
            32768,  // Output buffer size
            32768,  // Input buffer size
            0,      // Default timeout
            nullptr // Default security attributes
        );

        if (pipe_handle_ == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to create named pipe: " + std::to_string(GetLastError()));
        }

        // Accept connections loop
        while (running_) {
            OVERLAPPED overlap = {0};
            overlap.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

            if (!ConnectNamedPipe(pipe_handle_, &overlap)) {
                auto error = GetLastError();
                if (error == ERROR_PIPE_CONNECTED) {
                    std::cout << "Client already connected" << std::endl;
                } else if (error == ERROR_IO_PENDING) {
                    WaitForSingleObject(overlap.hEvent, INFINITE);
                    std::cout << "Client connected" << std::endl;
                } else {
                    CloseHandle(overlap.hEvent);
                    throw std::runtime_error("ConnectNamedPipe failed: " + std::to_string(error));
                }
            }

            CloseHandle(overlap.hEvent);

            // Handle client connection
            HandleClientConnection();

            // Disconnect and prepare for next client
            DisconnectNamedPipe(pipe_handle_);
        }

        server->Shutdown();
    }

    void Shutdown() {
        running_ = false;
        if (pipe_handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe_handle_);
            pipe_handle_ = INVALID_HANDLE_VALUE;
        }
    }

private:
    void Initialize() {
        pipe_handle_ = INVALID_HANDLE_VALUE;
    }

    void HandleClientConnection() {
        // Buffer for reading data
        std::vector<char> buffer(32768);
        DWORD bytes_read;
        OVERLAPPED overlap = {0};
        overlap.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

        while (running_) {
            if (!ReadFile(pipe_handle_, buffer.data(), buffer.size(), &bytes_read, &overlap)) {
                auto error = GetLastError();
                if (error == ERROR_IO_PENDING) {
                    WaitForSingleObject(overlap.hEvent, INFINITE);
                    GetOverlappedResult(pipe_handle_, &overlap, &bytes_read, FALSE);
                } else if (error == ERROR_BROKEN_PIPE) {
                    // Client disconnected
                    break;
                } else {
                    std::cerr << "Read error: " << error << std::endl;
                    break;
                }
            }

            if (bytes_read > 0) {
                // Process the received data
                ProcessData(buffer.data(), bytes_read);
            }
        }

        CloseHandle(overlap.hEvent);
    }

    void ProcessData(const char* data, DWORD length) {
        // Here you can process the received data
        // For example, parse it as a gRPC message and handle it
        namedpipe::Request request;
        if (request.ParseFromArray(data, length)) {
            namedpipe::Response response;
            response.set_counter(++message_counter_);
            response.set_message("Processed: " + request.message());

            // Serialize and send response
            std::string serialized_response;
            if (response.SerializeToString(&serialized_response)) {
                DWORD bytes_written;
                OVERLAPPED overlap = {0};
                overlap.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

                if (!WriteFile(pipe_handle_,
                             serialized_response.data(),
                             serialized_response.length(),
                             &bytes_written,
                             &overlap)) {
                    auto error = GetLastError();
                    if (error == ERROR_IO_PENDING) {
                        WaitForSingleObject(overlap.hEvent, INFINITE);
                        GetOverlappedResult(pipe_handle_, &overlap, &bytes_written, FALSE);
                    }
                }

                CloseHandle(overlap.hEvent);
            }
        }
    }

    std::string pipe_name_;
    HANDLE pipe_handle_;
    std::atomic<bool> running_;
    NamedPipeServiceImpl service_{};
    // namedpipe::NamedPipeService::Service service_;
    uint32_t message_counter_ = 0;
};

void RunServer() {
    const std::string pipe_name = "\\\\.\\pipe\\grpc_custom_pipe";
    HANDLE pipe_handle = CreateNamedPipe(
        pipe_name.c_str(),
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,
        1024 * 16,
        1024 * 16,
        NMPWAIT_USE_DEFAULT_WAIT,
        nullptr);

    if (pipe_handle == INVALID_HANDLE_VALUE) {
        std::cerr << "Failed to create named pipe: " << GetLastError() << std::endl;
        return;
    }

    std::cout << "Named pipe server listening on " << pipe_name << std::endl;

    while (true) {
        if (ConnectNamedPipe(pipe_handle, nullptr) != FALSE) {
            // Handle gRPC connection over the named pipe
            // You'll need to use grpc::ServerBuilder and pass the pipe handle
            // Implement your service logic here
            std::cout << "Client connected." << std::endl;

            // Example:
            grpc::ServerBuilder builder;
            NamedPipeServiceImpl service;
            builder.AddListeningPort("local:0", grpc::InsecureServerCredentials());
            // Use a custom ChannelArgument to pass the pipe handle
            builder.AddChannelArgument("named_pipe_handle", (intptr_t)pipe_handle);
            builder.RegisterService(&service);
            std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
            server->Wait();

            DisconnectNamedPipe(pipe_handle);
        } else {
            std::cerr << "Failed to connect client: " << GetLastError() << std::endl;
        }
    }
    CloseHandle(pipe_handle);
}
int main() {
    RunServer();
    // const std::string pipe_name = "\\\\.\\pipe\\grpc_custom_pipe";
    // // const std::string pipe_name = "np:////./pipe/grpc_custom_pipe";
    // std::signal(SIGINT, handleSignal);
    // NamedPipeServer server(pipe_name);
    // server.Run();
    // try {
    // NamedPipeServiceImpl service;
    // grpc::EnableDefaultHealthCheckService(true);
    // grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    //
    // grpc::ServerBuilder builder;
    // builder.AddListeningPort(pipe_name, grpc::InsecureServerCredentials());
    // builder.RegisterService(&service);
    // auto server = builder.BuildAndStart();
    //     // std::unique_ptr<grpc::Server> server(serverPtr);
    //     std::cout << "Server listening on " << pipe_name << std::endl;
    //server->Wait();
    //     // GrpcNamedPipeServer server(pipe_name);
    //     //
    //     // std::cout << "Server started. Press Ctrl+C to exit." << std::endl;
    //     //
    //     // while (running) {
    //     //     std::this_thread::sleep_for(std::chrono::milliseconds(100));
    //     // }
    // }
    // catch (const std::exception& e) {
    //     std::cerr << "Error: " << e.what() << std::endl;
    //     return 1;
    // }
    //
    // std::cout << "Server shutdown complete" << std::endl;
    return 0;
}
