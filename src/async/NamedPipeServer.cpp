#include "NamedPipeServer.h"
#include "Win32Helpers.h"

#include <algorithm>
#include <format>
#include <ranges>

namespace IPC {
  namespace {
    template <class... Args>
    std::unexpected<std::runtime_error> LogAndReturnRuntimeError(
        const std::format_string<Args...> fmt, Args&&... args
    ) {
      auto msg = std::format(fmt, std::forward<Args>(args)...);
      spdlog::error(msg);
      return std::unexpected<std::runtime_error>(msg);
    }

  }

  Message::Message(const std::uint32_t& connectionId, std::size_t packetSize): connectionId_(connectionId),
                                                                               packetSize_(packetSize) {
  }

  std::uint32_t Message::id() {
    return header_.id;
  }

  std::uint32_t Message::sourceId() {
    return header_.sourceId;
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

  bool Message::allDataRead() {
    return isHeaderProcessed() && allDataRead_;
  }

  std::expected<bool, std::exception> Message::onRead(std::size_t bytesRead) {
    auto pos = buffer_.writePosition() + bytesRead;
    if (pos > buffer_.size()) {
      return LogAndReturnRuntimeError(
        "Buffer size ({}) for message is too small given new bytesRead({}), {} is too large",
        buffer_.size(),
        bytesRead,
        pos
      );
    }

    buffer_.setWritePosition(pos);
    if (buffer_.availableToRead() == header_.size) {
      allDataRead_ = true;
    }

    return allDataRead_;
  }

  std::expected<bool, std::exception> Message::onWrite(std::size_t bytesWritten) {
    buffer_.setReadPosition(buffer_.readPosition() + bytesWritten);
    if (buffer_.availableToRead() == 0) {
      allDataWritten_ = true;
    }

    return allDataWritten_;
  }

  std::expected<bool, std::exception> Message::processHeader() {
    if (header_.size <= 0 || header_.id <= 0) {
      reset();
      return false;
    }

    headerProcessed_ = true;

    buffer_.reset();
    buffer_.resize(header_.size);

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

  std::size_t Message::size() {
    return header_.size;
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
    allDataRead_ = false;
    allDataWritten_ = false;
    buffer_.reset();
    return this;
  }

  MessageDataPacket Message::getNextReadPacket() {
    return {buffer_.writeData(), buffer_.availableToWrite()};
  }

  MessageDataPacket Message::getNextWritePacket() {
    return {buffer_.readData(), buffer_.availableToRead()};
  }

  DynamicByteBuffer::ValueType* Message::data() {
    return buffer_.data();
  }

  const DynamicByteBuffer::ValueType* Message::data() const {
    return buffer_.data();
  }

  NamedPipeIO::NamedPipeIO(std::uint32_t connectionId, Role role): id(connectionId),
                                                                   role(role) {
    std::memset(&overlapped, 0, sizeof(OVERLAPPED));
    overlapped.hEvent = CreateManualResetEvent();
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

  MessageFactory::MessageFactory(NamedPipeServer* server, std::uint32_t connectionId) : server(server),
    connectionId(connectionId) {

  }

  Message* MessageFactory::operator()() {
    return new Message(connectionId, server->packetSize());
  }

  NamedPipeConnection::NamedPipeConnection(NamedPipeServer* server, HANDLE pipeHandle, std::size_t packetSize):
    server_(server),
    packetSize_(packetSize),
    pipeHandle_(pipeHandle),
    messageFactory_(server, id_),
    messagePool_(messageFactory_) {


  }

  void NamedPipeConnection::destroy() {
    if (pipeHandle_ != INVALID_HANDLE_VALUE) {
      DisconnectNamedPipe(pipeHandle_);
      pipeHandle_ = nullptr;
    }
  }

  std::size_t NamedPipeConnection::packetSize() const {
    return packetSize_;
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

  /**
   * Start a read operation
   *
   * @return if an error occurs, `std::unexpected<std::exception>`, otherwise `true` if a new read was queued or `false` if not connected or if a pending read was already scheduled
   */
  std::expected<bool, std::exception> NamedPipeConnection::startRead() {
    if (!isConnected()) {
      return false;
    }

    auto& io = readIO_;

    // If already pending, return
    if (io.hasPendingIO) {
      return false;
    }

    // Acquire message from pool here
    if (!readMessage_) {
      readMessage_ = messagePool_.acquire();
    }

    DWORD bytesReadCount{0};
    bool success;
    if (readMessage_->isHeaderProcessed()) {
      auto [readData, readDataSize] = readMessage_->getNextReadPacket();
      success = ::ReadFile(
        pipeHandle_,
        readData,
        std::min<std::size_t>(readDataSize, packetSize()),
        &bytesReadCount,
        &io.overlapped
      );
    } else {
      success = ::ReadFile(pipeHandle_, readMessage_->header(), MessageHeaderSize, &bytesReadCount, &io.overlapped);
    }

    auto readRes = GetLastError();
    auto readResName = GetLastErrorAsString(readRes);
    if (success) {
      io.hasPendingIO = false;
      if (bytesReadCount > 0) {
        spdlog::info("Read complete message (bytesReadCount={})", bytesReadCount);
        auto onReadRes = onRead(bytesReadCount);
        if (!onReadRes.has_value()) {
          spdlog::error("startRead immediate data, res has error");
          return std::unexpected(onReadRes.error());
        }

        return startRead();
      }

      auto msg = std::format("No bytes read, but success == true, should disconnect ({}):{}", readRes, readResName);
      spdlog::warn(msg);
      return std::unexpected<std::runtime_error>(msg);
    }

    if (readRes == ERROR_IO_PENDING || readRes == ERROR_MORE_DATA) {
      io.hasPendingIO = true;
      return true;
    }

    auto msg = std::format("Read failed, should disconnect ({}):{}", readRes, readResName);
    spdlog::error(msg);
    return std::unexpected<std::runtime_error>(msg);
  }

  std::expected<bool, std::exception> NamedPipeConnection::startWrite() {
    // TODO: Implement `startWrite`
    return true;
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


  std::expected<bool, std::exception> NamedPipeConnection::onRead(std::size_t bytesRead) {
    if (!readMessage_) {
      return std::unexpected<std::runtime_error>("readMessage is a nullptr");
    }

    readIO_.hasPendingIO = false;
    auto msg = readMessage_;

    if (!msg->isHeaderProcessed()) {
      if (MessageHeaderSize != bytesRead)
        return LogAndReturnRuntimeError(
          "bytes read for header should always be {} bytes, but {} bytes read",
          MessageHeaderSize,
          bytesRead
        );

      auto processedRes = msg->processHeader();
      if (!processedRes) {
        return std::unexpected(processedRes.error());
      }

      auto processed = processedRes.value();
      if (!processed) {
        return LogAndReturnRuntimeError("Invalid header read from {} bytes", bytesRead);
      }

      return false;
    }

    auto readCompletedRes = msg->onRead(bytesRead);
    if (!readCompletedRes) {
      return std::unexpected(readCompletedRes.error());
    }

    auto readCompleted = readCompletedRes.value();
    if (readCompleted) {
      spdlog::info("Message ({}) is fully read and can now be distributed", msg->id());
      server_->emitMessage(msg);
      readMessage_ = messagePool_.acquire();
      return true;
    }

    return false;
  }

  std::expected<bool, std::exception> NamedPipeConnection::onWrite(std::size_t bytesWritten) {
    // TODO: Implement `onWrite`
    return true;
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

      if (options.packetSize > 0) {
        packetSize = options.packetSize;
      }
    }
  }

  NamedPipeServer::NamedPipeServer(
    MessageHandler messageHandler,
    const std::optional<NamedPipeServerOptions>& overrideOptions
  ) : stopEventHandle_(CreateManualResetEvent()),
      messageHandler_(messageHandler),
      options_(overrideOptions),
      pipePath_(CreateNamedPipePath(options_.pipeName)) {

    assert(options_.packetSize >= MessageHeaderSize);
    spdlog::info("NamedPipeServer({}) Created", pipePath_);
  }

  NamedPipeServer::~NamedPipeServer() {
    stop();

    {
      std::lock_guard lock(mutex_);
      if (stopEventHandle_)
        CloseHandle(stopEventHandle_);
    }
    spdlog::info("NamedPipeServer({}) Destroyed", pipePath_);
  }

  std::size_t NamedPipeServer::packetSize() const {
    return options_.packetSize;
  }

  bool NamedPipeServer::start(bool wait) {
    {
      std::scoped_lock lock(mutex_);

      if (ioThread_ || isRunning() || running_.exchange(true)) {
        if (ioThread_) {
          spdlog::warn("NamedPipeServer can not be re-started");
        }
        return false;
      }

      ioThread_ = std::make_unique<std::thread>(&NamedPipeServer::ioRunnable, this);
      emitThread_ = std::make_unique<std::thread>(&NamedPipeServer::emitRunnable, this);
    }
    if (wait) {
      waitUntilStopped();
    }
    return true;
  }


  bool NamedPipeServer::isRunning() {
    return running_;
  }


  /**
   * Stop the server
   *
   * @return `true` if the operation was successfully stopped, `false` if there was no ongoing operation to stop or if stopping failed
   */
  void NamedPipeServer::stop() {
    {
      std::scoped_lock lock(mutex_);
      if (!running_.exchange(false)) {
        spdlog::warn("NamedPipeServer is invalid or not running, can not stop");
      }

      emitCondition_.notify_all();

      if (ioThread_ && ioThread_->joinable() && ioThread_->get_id() != std::this_thread::get_id()) {
        ioThread_->join();
        ioThread_.reset();
      }

      if (emitThread_ && emitThread_->joinable() && emitThread_->get_id() != std::this_thread::get_id()) {
        emitThread_->join();
        emitThread_.reset();
      }
    }

    stoppedCondition_.notify_all();

  }

  /**
   * Blocks the calling thread until the server is completely stopped.
   *
   * This method acquires a lock and waits for the internal server state
   * to indicate that the server is no longer running. The waiting is
   * based on the condition that `running_`, `ioThread_`, and `emitThread_`
   * are all in a state indicating the server is stopped. It ensures proper
   * synchronization when stopping the server.
   *
   * This function also logs a message when the waiting process completes.
   */
  void NamedPipeServer::waitUntilStopped() {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (!isRunning())
        return;

      stoppedCondition_.wait(
        lock,
        [&] {
          return !running_ || !ioThread_ || !emitThread_;
        }
      );
    }

    spdlog::info("waitUntilStopped completed");
  }

  std::shared_ptr<NamedPipeConnection> NamedPipeServer::getConnection(std::uint32_t connectionId) {
    auto it = std::ranges::find_if(
      connections_,
      [connectionId](const auto& connection) {
        return connection->id() == connectionId;
      }
    );

    return it != connections_.end() ? *it : nullptr;

  }


  bool NamedPipeServer::hasAvailableConnection() {
    return !connections_.empty() && !std::ranges::all_of(
      connections_,
      [](auto& connection) {
        return connection->isConnected();
      }
    );
  }

  bool NamedPipeServer::shouldCreateConnection() {
    auto maxConnections = options_.maxConnections;
    return !hasAvailableConnection() && (maxConnections == 0 || connections_.size() < maxConnections);
  }

  /**
   * Main I/O loop for the NamedPipeServer.
   *
   * This function continuously monitors and handles I/O operations for all client connections
   * to the named pipe server. It performs the following tasks:
   * - Handles connection requests by creating new pipe instances.
   * - Manages pending events across all active client connections.
   * - Waits for events to complete using `WaitForMultipleObjects`.
   * - Processes different types of events (Connect, Read, Write) based on their roles.
   *
   * The loop executes until the server is stopped or an unrecoverable error occurs.
   *
   * Detailed functionality:
   * - Creates new connections when requested by `shouldCreateConnection`.
   * - Ensures that a read operation is pending for each active connection.
   * - Aggregates pending events and their associated handles.
   * - Waits for I/O completion events using `WaitForMultipleObjects`.
   * - Processes the completed event and invokes the appropriate handler based on its role:
   *   - **Connect**: Marks a connection as established.
   *   - **Read**: Processes data received and invokes `onRead`.
   *   - **Write**: Acknowledges completion of a write operation.
   *
   * Error handling:
   * - Logs errors related to operations such as creating new connections, starting reads,
   *   and handling I/O results.
   * - Stops the server in case of critical errors.
   *
   * Notes:
   * - Uses `ResetEvent` to reset event handles after they are processed.
   * - Handles indefinite waiting until an event is available.
   * - May encounter unrecoverable errors, in which case it gracefully stops the server.
   */
  void NamedPipeServer::ioRunnable() {
    while (true) {
      if (!running_) {
        break;
      }
      // Create a new pipe instance for each client
      if (shouldCreateConnection()) {
        auto res = createNewConnection();
        if (!res) {
          spdlog::error("createNewConnection Failed: {}", res.error().what());
          break;
        }
      }

      // Pending event vectors
      std::vector<NamedPipePendingEvent> allPendingEvents{};
      std::vector<HANDLE> allPendingEventHandles{};

      // Populate the pending event vectors
      for (auto& connection : connections_) {
        // Ensure that if connected, a read is pending
        if (auto res = connection->startRead(); !res.has_value()) {
          spdlog::error("startRead failed: {}", res.error().what());
          running_.exchange(false);
          return;
        }

        // Get all pendingEvents for the given connection
        auto pendingEvents = connection->pendingEvents();

        // Map to a vector of HANDLE(s), which are `OVERLAPPED.hEvent`
        auto pendingEventHandles = pendingEvents | std::views::transform(
          [](auto& pendingEvent) {
            return std::get<1>(pendingEvent)->overlapped.hEvent;
          }
        );

        // Merge all pending events into `allPendingEvents` & event handles into `allPendingEventHandles`
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
        allPendingEventHandles.data(),
        // `FALSE` as the 3rd argument, means return on the first fired event
        FALSE,
        // Wait indefinitely
        INFINITE
      );

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
      if (!success && GetLastError() != ERROR_MORE_DATA) {
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
          connection->onRead(byteCount);
          break;
        };
        case NamedPipeIO::Role::Write: {
          // TODO: Pop message and start sending next if available
          spdlog::info("Write(byteCount={})", byteCount);
          connection->onWrite(byteCount);
          break;
        };
      }
    }

    running_.exchange(false);
  }

  void NamedPipeServer::emitRunnable() {
    while (true) {
      if (!running_) {
        break;
      }

      auto msgOpt = nextEmitMessage();
      if (!msgOpt) {
        spdlog::warn("No available message, likely shutdown or disconnected");
        continue;
      }

      auto msg = msgOpt.value();
      auto connection = getConnection(msg->connectionId());
      if (!connection) {
        spdlog::error("Unable to find active connection for id({})", msg->connectionId());
        continue;
      }

      messageHandler_(msg->size(), msg->data(), msg->header(), connection.get(), this);
    }

    running_.exchange(false);
  }

  void NamedPipeServer::emitMessage(const std::shared_ptr<Message>& message) {
    {
      std::lock_guard<std::mutex> lock(emitMutex_);
      emitMessageQueue_.push_back(message);
    }
    emitCondition_.notify_one();
  }

  std::optional<MessagePtr> NamedPipeServer::nextEmitMessage() {
    std::unique_lock<std::mutex> lock(emitMutex_);
    emitCondition_.wait(
      lock,
      [&]() {
        return !emitMessageQueue_.empty() || !running_;
      }
    );

    if (!running_) {
      return std::nullopt;
    }

    if (emitMessageQueue_.empty()) {
      return std::nullopt;
    }

    MessagePtr message = emitMessageQueue_.front();
    emitMessageQueue_.pop_front();
    return message;

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

    auto connection = std::make_shared<NamedPipeConnection>(this, pipeHandle, packetSize());
    auto res = connection->connect();
    if (!res.has_value()) return std::unexpected(res.error());

    connections_.push_back(connection);

    return connection;

  }


}
