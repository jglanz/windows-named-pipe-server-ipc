//
// Created by jglanz on 1/28/2024.
//

#pragma once

#include <deque>
#include <expected>
#include <functional>
#include <windows.h>

#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <spdlog/spdlog.h>
#include <magic_enum/magic_enum.hpp>
#include "Buffer.h"
#include "NamedPipeHelpers.h"
#include "ObjectPool.h"


#ifndef NAMED_PIPE_SERVER_PIPE_NAME_DEFAULT
#define NAMED_PIPE_SERVER_PIPE_NAME_DEFAULT "server_pipe"
#endif

#ifndef NAMED_PIPE_SERVER_MAX_CONNECTIONS_DEFAULT
#define NAMED_PIPE_SERVER_MAX_CONNECTIONS_DEFAULT 0
#endif

#ifndef NAMED_PIPE_SERVER_BUFFER_SIZE_DEFAULT
#define NAMED_PIPE_SERVER_BUFFER_SIZE_DEFAULT 8192
#endif

namespace IPC {

  /**
   * @brief Represents the header structure of a message, which contains metadata such as identifiers and size.
   */

  struct MessageHeader {

    /**
     * @brief ID must be unique until rollover & must be > 0
     */
    std::uint32_t id{0};

    /**
     * @brief ID this message is related to, i.e. the ID that was sent in the request, if this is a response
     */
    std::uint32_t sourceId{0};


    /**
     * @brief Represents the unique identifier for a client, used to track and manage client-specific operations.
     */
    std::uint32_t clientId{0};

    /**
     * @brief Data size of this message (excluding header size)
     */
    std::uint32_t size{};

  };

  constexpr auto MessageHeaderSize = sizeof(MessageHeader);
  using MessageDataPacket = std::pair<DynamicByteBuffer::ValueType*, std::size_t>;
  class Message {

    MessageHeader header_{};
    std::optional<std::exception> error_{std::nullopt};

    std::atomic_bool headerProcessed_{false};
    std::atomic_bool allDataRead_{false};
    std::atomic_bool allDataWritten_{false};
    DynamicByteBuffer buffer_{};

    DynamicByteBuffer::SizeType availableSize_{0};

    const std::uint32_t connectionId_;
    const std::size_t packetSize_;

    protected:

      MessageHeader* resetHeader();

    public:

      Message() = delete;

      explicit Message(const std::uint32_t& connectionId, std::size_t packetSize);
      std::uint32_t id();
      std::uint32_t sourceId();
      std::uint32_t connectionId() const;

      std::size_t packetSize() const;

      std::size_t size();

      bool hasError();

      bool isHeaderProcessed();
      bool allDataRead();

      std::expected<bool,std::exception> onRead(std::size_t bytesRead);
      std::expected<bool,std::exception> onWrite(std::size_t bytesWritten);

      std::expected<bool,std::exception> processHeader();

      Message* setError(const std::string& msg);

      std::optional<std::exception> error() const;

      Message* setHeader(const MessageHeader& newHeader);

      MessageHeader* header();

      const MessageHeader* header() const;

      Message* reset();

      MessageDataPacket getNextReadPacket();

      MessageDataPacket getNextWritePacket();

      /**
       * @brief Get a pointer to the underlying data
       *
       * @return Data pointer, which is sized to match the header if needed
       */
      DynamicByteBuffer::ValueType* data();

      const DynamicByteBuffer::ValueType* data() const;
  };

  using MessagePtr = std::shared_ptr<Message>;

  struct NamedPipeIO {
    enum class Role {
      Connect,
      Read,
      Write
    };

    std::uint32_t id;
    Role role;

    OVERLAPPED overlapped{};
    bool hasPendingIO{false};

    NamedPipeIO(std::uint32_t connectionId, Role role);

    std::string toString();
  };

  class NamedPipeConnection;
  class NamedPipeServer;

  using NamedPipePendingEvent = std::tuple<NamedPipeConnection*, NamedPipeIO*>;

  struct MessageFactory {
    NamedPipeServer* server;
    std::uint32_t connectionId;
    MessageFactory(NamedPipeServer * server, std::uint32_t connectionId);

    Message * operator()();
  };

  class NamedPipeConnection {
    public:

      static HANDLE CreateEvent() {
        return ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
      };

    private:
      NamedPipeServer * server_;
      const std::uint32_t id_{NextNamedPipeConnectionId()};
      const std::size_t packetSize_;
      HANDLE pipeHandle_;
      MessageFactory messageFactory_;
      ObjectPool<Message> messagePool_;

      std::atomic_bool connected_{false};

      NamedPipeIO connectIO_{id_, NamedPipeIO::Role::Connect};
      NamedPipeIO readIO_{id_, NamedPipeIO::Role::Read};
      NamedPipeIO writeIO_{id_, NamedPipeIO::Role::Write};

      std::deque<MessagePtr> writeMessageQueue_{};
      MessagePtr writeMessage_{nullptr};

      MessagePtr readMessage_{nullptr};

    public:

      std::expected<bool, std::exception> connect();;

      NamedPipeConnection() = delete;

      explicit NamedPipeConnection(NamedPipeServer * server, HANDLE pipeHandle, std::size_t packetSize);


      virtual ~NamedPipeConnection();

      void destroy();
      std::size_t packetSize() const;
      std::vector<NamedPipePendingEvent> pendingEvents();
      std::vector<NamedPipeIO*> allIO();
      std::uint32_t id() const;

      HANDLE pipeHandle();

      NamedPipeIO& readIO();

      NamedPipeIO& writeIO();

      NamedPipeIO& connectIO();

      bool isConnected();

      bool isReadPending();

      /**
       * TODO: implement write message
       * @param id
       * @param sourceId
       * @param data
       * @param size
       * @return
       */
      std::expected<bool,std::exception> writeMessage(
        std::uint32_t id,
        std::uint32_t sourceId,
        const DynamicByteBuffer::ValueType* data,
        std::uint32_t size
        );

      std::expected<bool,std::exception> startRead();
      std::expected<bool,std::exception> startWrite();

      bool setConnected(bool connected);

      LPOVERLAPPED readOverlapped();

      LPOVERLAPPED writeOverlapped();
    protected:
      friend class NamedPipeServer;
      std::expected<bool,std::exception> onRead(std::size_t bytesRead);
      std::expected<bool,std::exception> onWrite(std::size_t bytesWritten);

  };

  using MessageHandler = std::function<void(
    std::size_t size,
    const DynamicByteBuffer::ValueType* data,
    const MessageHeader* header,
    NamedPipeConnection* connection,
    NamedPipeServer* server
  )>;

  struct NamedPipeServerOptions {

    /**
     * @brief Pipe name to use (excludes \\\\.\\pipe\\)
     */
    std::string pipeName{NAMED_PIPE_SERVER_PIPE_NAME_DEFAULT};

    /**
     * @brief Max concurrent connections
     *
     * 0 = unlimited
     */
    std::size_t maxConnections{NAMED_PIPE_SERVER_MAX_CONNECTIONS_DEFAULT};

    /**
     * Size of an individual packet (1..n packets make a message)
     */
    std::size_t packetSize{NAMED_PIPE_SERVER_BUFFER_SIZE_DEFAULT};

    explicit NamedPipeServerOptions(const std::optional<NamedPipeServerOptions>& overrideOptions = std::nullopt);
  };


  class NamedPipeServer {
    HANDLE stopEventHandle_;
    std::mutex mutex_{};
    std::mutex connectionMutex_{};
    std::mutex emitMutex_{};
    std::condition_variable emitCondition_{};
    std::condition_variable stoppedCondition_{};
    std::deque<MessagePtr> emitMessageQueue_{};
    std::unique_ptr<std::thread> emitThread_{nullptr};

    std::unique_ptr<std::thread> ioThread_{nullptr};

    std::atomic_bool running_{false};
    std::vector<std::shared_ptr<NamedPipeConnection>> connections_{};

    MessageHandler messageHandler_;

    const NamedPipeServerOptions options_;

    const std::string pipePath_;

    public:

      NamedPipeServer() = delete;

      NamedPipeServer(const NamedPipeServer& other) = delete;

      NamedPipeServer(NamedPipeServer&& other) noexcept = delete;

      NamedPipeServer& operator=(const NamedPipeServer& other) = delete;

      NamedPipeServer& operator=(NamedPipeServer&& other) noexcept = delete;

      /**
       * @brief the only acceptable constructor
       */
      explicit NamedPipeServer(
        MessageHandler messageHandler,
        const std::optional<NamedPipeServerOptions>& overrideOptions = std::nullopt
      );

      /**
       * @brief Destructor required to stop any running threads
       */
      virtual ~NamedPipeServer();

      std::size_t packetSize() const;

      bool hasAvailableConnection();

      bool shouldCreateConnection();

      std::expected<bool,std::exception> writeMessage(
        std::uint32_t connectionId,
        std::uint32_t id,
        std::uint32_t sourceId,
        const DynamicByteBuffer::ValueType* data,
        std::uint32_t size
        );

      /**
       * @inherit
       */
      bool start(bool wait = false);


      /**
       * @inherit
       */
      bool isRunning();

      /**
       * @inherit
       */
      void stop();

      void waitUntilStopped();

      std::shared_ptr<NamedPipeConnection> getConnection(std::uint32_t connectionId);

    protected:
      friend class NamedPipeConnection;

      void emitMessage(const std::shared_ptr<Message>& message);

    private:
      void ioRunnable();
      void emitRunnable();
      std::optional<MessagePtr> nextEmitMessage();



      std::expected<std::shared_ptr<NamedPipeConnection>, std::exception> createNewConnection();
  };
} // namespace IPC
