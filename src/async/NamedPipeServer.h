//
// Created by jglanz on 1/28/2024.
//

#pragma once

#include <deque>
#include <expected>
#include <functional>
#include <iostream>
#include <windows.h>

#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <spdlog/spdlog.h>
#include <magic_enum/magic_enum.hpp>
#include "Buffer.h"
#include "NamedPipeHelpers.h"
#include "Win32Helpers.h"

#ifndef NAMED_PIPE_SERVER_PIPE_NAME_DEFAULT
#define NAMED_PIPE_SERVER_PIPE_NAME_DEFAULT "server_pipe"
#endif

#ifndef NAMED_PIPE_SERVER_MAX_CONNECTIONS_DEFAULT
#define NAMED_PIPE_SERVER_MAX_CONNECTIONS_DEFAULT 0
#endif

#ifndef NAMED_PIPE_SERVER_BUFFER_SIZE_DEFAULT
#define NAMED_PIPE_SERVER_BUFFER_SIZE_DEFAULT 1024
#endif

namespace IPC {

  /**
   * @brief Message header that contains message info
   *
   * @tparam MessageType enumeration of a message type
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
     * @brief Data size of this message (excluding header size)
     */
    std::size_t size{};

  };


  class Message {

    MessageHeader header_{};
    std::optional<std::exception> error_{std::nullopt};

    std::atomic_bool headerProcessed_{false};
    std::size_t packetIndex_{0};
    std::size_t packetCount_{0};

    DynamicByteBuffer buffer_{};

    DynamicByteBuffer::SizeType availableSize_{0};

    const std::uint32_t connectionId_;
    const std::size_t packetSize_{NAMED_PIPE_SERVER_BUFFER_SIZE_DEFAULT};

    protected:

      MessageHeader* resetHeader();

    public:

      Message() = delete;

      explicit Message(const std::uint32_t& connectionId);

      std::uint32_t connectionId() const;

      std::size_t packetSize() const;

      std::size_t packetIndex();

      bool hasError();

      bool isHeaderProcessed();

      std::expected<bool,std::exception> onRead(std::size_t bytesRead);
      std::expected<bool,std::exception> onWrite(std::size_t bytesWritten);

      std::expected<bool,std::exception> processHeader();

      Message* setError(const std::string& msg);

      std::optional<std::exception> error() const;

      Message* setHeader(const MessageHeader& newHeader);

      MessageHeader* header();

      const MessageHeader* header() const;

      Message* reset();

      DynamicByteBuffer::ValueType* getPacketData(std::size_t packetIndex);

      /**
       * @brief Get a pointer to the underlying data
       *
       * @return Data pointer, which is sized to match the header if needed
       */
      DynamicByteBuffer::ValueType* data(std::optional<std::size_t> overrideSize = std::nullopt);

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

  using NamedPipePendingEvent = std::tuple<NamedPipeConnection*, NamedPipeIO*>;

  class NamedPipeConnection {
    public:

      static HANDLE CreateEvent() {
        return ::CreateEvent(nullptr, TRUE, FALSE, nullptr);
      };

    private:

      const std::uint32_t id_{NextNamedPipeConnectionId()};
      HANDLE pipeHandle_;

      std::atomic_bool connected_{false};

      NamedPipeIO connectIO_{id_, NamedPipeIO::Role::Connect};
      NamedPipeIO readIO_{id_, NamedPipeIO::Role::Read};
      NamedPipeIO writeIO_{id_, NamedPipeIO::Role::Write};

      std::deque<MessagePtr> writeMessageQueue_{};
      MessagePtr writeMessage_{nullptr};

      std::deque<MessagePtr> readMessageQueue_{};
      MessagePtr readMessage_{nullptr};

    public:

      std::expected<bool, std::exception> connect();;

      NamedPipeConnection() = delete;

      explicit NamedPipeConnection(HANDLE pipeHandle);


      virtual ~NamedPipeConnection();

      void destroy();

      std::vector<NamedPipePendingEvent> pendingEvents();
      std::vector<NamedPipeIO*> allIO();
      std::uint32_t id() const;

      HANDLE pipeHandle();

      NamedPipeIO& readIO();

      NamedPipeIO& writeIO();

      NamedPipeIO& connectIO();

      bool isConnected();

      bool isReadPending();
      std::expected<bool,std::exception> startRead();

      bool setConnected(bool connected);

      LPOVERLAPPED readOverlapped();

      LPOVERLAPPED writeOverlapped();


  };


  class NamedPipeServer;

  using MessageHandler = std::function<void(
    std::size_t size,
    DynamicByteBuffer::ValueType* data,
    NamedPipeConnection* connection,
    NamedPipeServer* server
  )>;

  struct NamedPipeServerOptions {

    /**
     * @brief Pipe name to use (excludes \\\\.pipe\\)
     */
    std::string pipeName{NAMED_PIPE_SERVER_PIPE_NAME_DEFAULT};

    /**
     * @brief Max concurrent connections
     *
     * 0 = unlimited
     */
    std::size_t maxConnections{NAMED_PIPE_SERVER_MAX_CONNECTIONS_DEFAULT};

    explicit NamedPipeServerOptions(const std::optional<NamedPipeServerOptions>& overrideOptions = std::nullopt);
  };


  class NamedPipeServer {

    std::mutex mutex_{};
    std::mutex connectionMutex_{};
    std::unique_ptr<std::thread> eventThread_{nullptr};
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

      bool hasAvailableConnection();

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

    private:

      void runnable();

      std::expected<std::shared_ptr<NamedPipeConnection>, std::exception> createNewConnection();
  };
} // namespace IPC
