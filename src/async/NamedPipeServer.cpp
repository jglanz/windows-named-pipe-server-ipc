#include "NamedPipeServer.h"

#include <algorithm>
#include <format>
#include <ranges>

namespace IPC {



  Message::Message(const std::uint32_t& connectionId): connectionId_(connectionId) {
  }

  std::uint32_t Message::connectionId() const {
    return connectionId_;
  }
  bool Message::hasError() {
    return error_.has_value();
  }

  Message* Message::setError(const std::string& msg) {
    error_ = std::make_optional<std::runtime_error>(msg);
    return this;
  }

  bool Message::isHeaderProcessed() {
    if (headerProcessed_ && (header_.size <= 0 || header_.id <= 0)) {
      headerProcessed_ = false;
    }
    return headerProcessed_;
  }

  std::expected<bool, std::exception> Message::onRead(std::size_t bytesRead) {
    // TODO: Move the `readMessage_.position` by `bytesRead`

    return true;
  }

  std::expected<bool, std::exception> Message::onWrite(std::size_t bytesWritten) {
    return true;
  }

  std::expected<bool,std::exception> Message::processHeader() {
    if (header_.size <= 0 || header_.id <= 0) {
      reset();
      return false;
    }

    headerProcessed_ = true;
    packetCount_ = header_.size / packetSize();
    if (packetCount_ * packetSize() < header_.size) {
      packetCount_ + 1;
    }

    return true;
  }

  MessageHeader* Message::resetHeader() {
    header_.id = 0;
    header_.sourceId = 0;
    header_.size = 0;
    return &header_;
  }
  std::size_t Message::packetSize() const {
    return packetSize_;
  }
  std::size_t Message::packetIndex() {
    return packetIndex_;
  }

  std::optional<std::exception> Message::error() const {
    return error_;
  }

  Message* Message::setHeader(const MessageHeader& newHeader) {
    header_ = newHeader;

    return this;
  }

  MessageHeader* Message::header() {
    return &header_;
  }

  const MessageHeader* Message::header() const {
    return &header_;
  }

  Message* Message::reset() {
    error_ = std::nullopt;
    resetHeader();
    headerProcessed_ = false;
    packetCount_ = 0;
    packetIndex_ = 0;
    buffer_.reset();
    return this;
  }

  DynamicByteBuffer::ValueType* Message::getPacketData(std::size_t packetIndex) {
    auto bufOffset = packetIndex * packetSize_;
    auto reqBufferSize = bufOffset + packetSize_;
    if (buffer_.size() < reqBufferSize) {
      buffer_.resize(reqBufferSize);
    }

    return buffer_.data() + bufOffset;
  }

  DynamicByteBuffer::ValueType* Message::data(std::optional<std::size_t> overrideSize) {
    if (auto requiredSize = overrideSize.value_or(header_.size); requiredSize > buffer_.size()) {
      buffer_.resize(requiredSize);
    }
    return buffer_.data();
  }

  const DynamicByteBuffer::ValueType* Message::data() const {
    return buffer_.data();
  }

  NamedPipeIO::NamedPipeIO(std::uint32_t connectionId, Role role): id(connectionId),
                                                                   role(role) {
    std::memset(&overlapped, 0, sizeof(OVERLAPPED));
    overlapped.hEvent = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
  }

  std::string NamedPipeIO::toString() {
    return std::format("NamedPipeIO(connectionId={},role={})", id, std::string{magic_enum::enum_name(role)});
  }

  std::expected<bool, std::exception> NamedPipeConnection::connect() {
    connectIO_.hasPendingIO = false;
    connected_ = false;

    // Start an overlapped connection for this pipe instance.
    BOOL connected = ConnectNamedPipe(pipeHandle_, &connectIO_.overlapped);

    // Overlapped ConnectNamedPipe should return zero.
    if (connected) {
      spdlog::error("ConnectNamedPipe already connected: {}", GetLastErrorAsString());
      connected_ = true;
      return true;
    }

    auto res = GetLastError();
    auto resName = GetLastErrorAsString();
    spdlog::info("CONNECT ({})", resName);
    switch (res) {
      // The overlapped connection in progress.
      case ERROR_IO_PENDING:
        connectIO_.hasPendingIO = true;
        break;

      // Client is already connected, so signal an event.

      case ERROR_PIPE_CONNECTED: {
        connected_ = true;
        if (!SetEvent(connectIO_.overlapped.hEvent)) {
          spdlog::warn("CONNECT: Unable to set event");
        }
        break;
      }
      // If an error occurs during the connect operation...
      default: {
        auto msg = std::format("ConnectNamedPipe failed with {}", resName);
        spdlog::error(msg);
        return std::unexpected<std::runtime_error>(msg);
      }
    }

    return !connectIO_.hasPendingIO;
  }

  NamedPipeConnection::NamedPipeConnection(HANDLE pipeHandle): pipeHandle_(pipeHandle) {


  }

  void NamedPipeConnection::destroy() {
    if (pipeHandle_ != INVALID_HANDLE_VALUE) {
      DisconnectNamedPipe(pipeHandle_);
      pipeHandle_ = nullptr;
    }
  }

  NamedPipeConnection::~NamedPipeConnection() {
    destroy();
  }

  std::vector<NamedPipePendingEvent> NamedPipeConnection::pendingEvents() {
    std::vector<NamedPipePendingEvent> events;
    for (auto io : allIO()) {
      if (io->hasPendingIO) {
        events.emplace_back(this, io);
      }
    }

    return events;
  }

  std::vector<NamedPipeIO*> NamedPipeConnection::allIO() {
    return {&connectIO_, &readIO_, &writeIO_};
  }

  std::uint32_t NamedPipeConnection::id() const {
    return id_;
  }

  HANDLE NamedPipeConnection::pipeHandle() {
    return pipeHandle_;
  }

  NamedPipeIO& NamedPipeConnection::readIO() {
    return readIO_;
  }

  NamedPipeIO& NamedPipeConnection::writeIO() {
    return writeIO_;
  }

  NamedPipeIO& NamedPipeConnection::connectIO() {
    return connectIO_;
  }

  bool NamedPipeConnection::isConnected() {
    if (connected_) {
      // assert(!connectIO_.hasPendingIO && "connectIO_.hasPendingIO is true?");
      return true;
    }

    return false;
  }

  bool NamedPipeConnection::isReadPending() {
    return readIO_.hasPendingIO;
  }

  std::expected<bool, std::exception> NamedPipeConnection::startRead() {
    if (!isConnected()) {
      // spdlog::warn("Not connected, can not read");
      return false;
    }

    auto& io = readIO_;
    if (io.hasPendingIO) {
      return false;
    }

    if (!readMessage_) {
      readMessage_ = std::make_shared<Message>(id());
    }
    DWORD bytesReadCount{0};
    auto success = readMessage_->isHeaderProcessed() ?
    ::ReadFile(
      pipeHandle_,
      readMessage_->data(),
      readMessage_->packetSize(),
      &bytesReadCount,
      &io.overlapped
      ) : ::ReadFile(
        pipeHandle_,
        readMessage_->header(),
        sizeof(MessageHeader),
        &bytesReadCount,
        &io.overlapped
      );
    auto readRes = GetLastError();
    auto readResName = GetLastErrorAsString(readRes);
    if (success) {
      io.hasPendingIO = false;
      if (bytesReadCount > 0) {
        spdlog::info("Read complete message (bytesReadCount={})", bytesReadCount);
        return true;
      }

      auto msg = std::format("No bytes read, but success == true, should disconnect ({}):{}", readRes, readResName);
      spdlog::warn(msg);
      return std::unexpected<std::runtime_error>(msg);
    }

    if (readRes == ERROR_IO_PENDING) {
      io.hasPendingIO = true;
      return true;
    }

    auto msg = std::format("Read failed, should disconnect ({}):{}", readRes, readResName);
    spdlog::error(msg);
    return std::unexpected<std::runtime_error>(msg);
  }

  bool NamedPipeConnection::setConnected(bool connected) {
    auto wasConnected = connected_.exchange(connected);
    connectIO_.hasPendingIO = false;
    return wasConnected;
  }

  LPOVERLAPPED NamedPipeConnection::readOverlapped() {
    return &readIO_.overlapped;
  }

  LPOVERLAPPED NamedPipeConnection::writeOverlapped() {
    return &writeIO_.overlapped;
  }

  NamedPipeServerOptions::NamedPipeServerOptions(const std::optional<NamedPipeServerOptions>& overrideOptions) {
    if (overrideOptions) {
      auto options = overrideOptions.value();
      if (!options.pipeName.empty()) {
        pipeName = options.pipeName;
      }

      if (options.maxConnections > 0) {
        maxConnections = options.maxConnections;
      }
    }
  }

  NamedPipeServer::NamedPipeServer(
    MessageHandler messageHandler,
    const std::optional<NamedPipeServerOptions>& overrideOptions
  ) : messageHandler_(messageHandler),
      options_(overrideOptions),
      pipePath_(CreateNamedPipePath(options_.pipeName)) {
  }

  NamedPipeServer::~NamedPipeServer() {
    stop();
  }

  bool NamedPipeServer::start(bool wait) {
    std::scoped_lock lock(mutex_);

    if (eventThread_ || isRunning() || running_.exchange(true)) {
      if (eventThread_) {
        spdlog::warn("NamedPipeServer can not be re-started");
      }
      return false;
    }

    eventThread_ = std::make_unique<std::thread>(&NamedPipeServer::runnable, this);
    if (wait && eventThread_->joinable()) {
      eventThread_->join();
    }
    return true;
  }


  bool NamedPipeServer::isRunning() {
    return running_;
  }

  void NamedPipeServer::stop() {
    std::scoped_lock lock(mutex_);
    if (!eventThread_ || !running_.exchange(false)) {
      spdlog::warn("NamedPipeServer is invalid or not running, can not stop");
    }

    if (eventThread_ && eventThread_->joinable()) {
      eventThread_->join();
    }
  }

  bool NamedPipeServer::hasAvailableConnection() {
    return !connections_.empty() && !std::ranges::all_of(
      connections_,
      [](auto& connection) {
        return connection->isConnected();
      }
    );
  }

  void NamedPipeServer::runnable() {
    while (true) {
      if (!running_) {
        break;
      }
      // Create a new pipe instance for each client
      if (!hasAvailableConnection()) {
        auto res = createNewConnection();
        if (!res) {
          spdlog::error("createNewConnection Failed: {}", res.error().what());
          break;
        }
      }
      std::vector<NamedPipePendingEvent> allPendingEvents{};
      std::vector<HANDLE> allPendingEventHandles{};
      for (auto& connection : connections_) {
        if (!connection->isReadPending()) {
          if (auto res = connection->startRead(); !res) {
            spdlog::error("startRead failed: {}", res.error().what());
            running_.exchange(false);
            return;
          }

        }
        auto pendingEvents = connection->pendingEvents();
        auto pendingEventHandles = pendingEvents | std::views::transform(
          [](auto& pendingEvent) {
            return std::get<1>(pendingEvent)->overlapped.hEvent;
          }
        );

        allPendingEvents.insert(allPendingEvents.end(), pendingEvents.begin(), pendingEvents.end());
        allPendingEventHandles.insert(
          allPendingEventHandles.end(),
          pendingEventHandles.begin(),
          pendingEventHandles.end()
        );
      }
      spdlog::info(
        "Waiting for an event (pendingEvents={},connections={})",
        allPendingEvents.size(),
        connections_.size()
      );
      auto waitRes = WaitForMultipleObjects(
        allPendingEventHandles.size(),
        // number of event objects
        allPendingEventHandles.data(),
        // array of event objects
        FALSE,
        // does not wait for all
        INFINITE
      ); // waits indefinitely

      // dwWait shows which pipe completed the operation.

      auto idx = waitRes - WAIT_OBJECT_0;
      if (idx >= allPendingEvents.size()) {
        spdlog::error("Unknown Error (waitRes={},idx={})", waitRes, idx);
        break;
      }

      auto& pendingEvent = allPendingEvents[idx];
      auto connection = std::get<0>(pendingEvent);
      auto io = std::get<1>(pendingEvent);
      auto role = io->role;
      auto overlappedPtr = &io->overlapped;
      ResetEvent(overlappedPtr->hEvent);

      DWORD byteCount{0};
      auto success = GetOverlappedResult(connection->pipeHandle(), overlappedPtr, &byteCount, FALSE);
      spdlog::info("GetOverlappedResult(success={},byteCount={})", success, byteCount);
      if (!success) {
        spdlog::error("ERROR: GetOverlappedResult({}): {}", GetLastError(), GetLastErrorAsString());
        break;
      }
      switch (role) {
        case NamedPipeIO::Role::Connect: {
          spdlog::info("Connect(byteCount={})", byteCount);
          connection->setConnected(true);
          break;
        };
        case NamedPipeIO::Role::Read: {
          // TODO: Populate message
          spdlog::info("Read(byteCount={})", byteCount);
          break;
        };
        case NamedPipeIO::Role::Write: {
          // TODO: Pop message and start sending next if available
          spdlog::info("Write(byteCount={})", byteCount);
          break;
        };
      }

    }

    running_.exchange(false);
  }

  std::expected<std::shared_ptr<NamedPipeConnection>, std::exception> NamedPipeServer::createNewConnection() {
    std::scoped_lock<std::mutex> lock(connectionMutex_);

    HANDLE pipeHandle = CreateNamedPipe(
      pipePath_.c_str(),
      PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
      PIPE_UNLIMITED_INSTANCES,
      NAMED_PIPE_SERVER_BUFFER_SIZE_DEFAULT,
      NAMED_PIPE_SERVER_BUFFER_SIZE_DEFAULT,
      5000,
      nullptr
    );

    if (pipeHandle == INVALID_HANDLE_VALUE) {
      return std::unexpected<std::runtime_error>("Failed to create named pipe instance.");
    }

    auto connection = std::make_shared<NamedPipeConnection>(pipeHandle);
    auto res = connection->connect();
    if (!res.has_value()) return std::unexpected(res.error());

    connections_.push_back(connection);

    return connection;

  }


}
