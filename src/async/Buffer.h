//
// Created by jglanz on 1/25/2024.
//

#pragma once

#include <windows.h>

#include <array>
#include <cassert>
#include <type_traits>
#include <vector>

#include "Traits.h"

namespace IPC {

  constexpr std::size_t BufferMinSize = 1024;

  template <
    typename Storage,
    typename = std::enable_if<
      is_container<Storage> &&
      std::is_scalar_v<typename Storage::value_type>
    >,
    typename = std::void_t<typename Storage::size_type>
  >
  class Buffer {
    public:

      using ValueType = typename Storage::value_type;
      using SizeType = typename Storage::size_type;
      using StorageType = Storage;

    protected:

      virtual Storage& getStorage() = 0;

      virtual const Storage& getStorage() const = 0;

      struct {
        SizeType read;
        SizeType write;
      } position_{0, 0};

    public:

      Buffer() = default;

      virtual ~Buffer() = default;

      /**
       * \brief Access the underlying memory/data pointer
       *
       * \return Underlying data pointer
       */
      virtual ValueType* data() {
        return getStorage().data();
      }

      virtual const ValueType* data() const {
        return getStorage().data();
      }

      /**
       * \brief
       * \return size of the buffer
       */
      virtual SizeType size() {
        return getStorage().size();
      }

      // /**
      //  * \brief Clone the buffer
      //  *
      //  * \return cloned buffer
      //  */
      // virtual Buffer<Storage> clone() = 0;

      /**
       * \brief Resize the target buffer
       *
       * \param newSize target size of buffer
       * \return pair<newSize,oldSize>
       */
      virtual bool resize(SizeType newSize) {
        return false;
      }

      virtual void reset() {
        position_ = {0, 0};
      }

      virtual SizeType availableToWrite() {
        return std::min<SizeType>(size() - position_.write, size());
      }

      virtual SizeType availableToRead() {
        return std::min<SizeType>(static_cast<SizeType>(position_.write) - static_cast<SizeType>(position_.read), 0);
      }

      virtual void setWritePosition(SizeType newWritePosition = 0) {
        newWritePosition = std::min<SizeType>(size(), newWritePosition);
        position_.write = newWritePosition;
      }

      virtual void setReadPosition(SizeType newReadPosition = 0) {
        newReadPosition = std::min<SizeType>(availableToRead(), newReadPosition);
        position_.read = newReadPosition;
      }

      virtual SizeType writePosition() {
        return std::min<SizeType>(size(), position_.write);
      }

      virtual SizeType readPosition() {
        return std::min<SizeType>(writePosition(), position_.read);
      }

      virtual SizeType write(ValueType* src, SizeType len) {
        auto availSize = availableToWrite();

        if (len > availSize) {
          SizeType extraSize = ::abs(static_cast<int>(len - availSize));
          assert(resize(extraSize));
        }

        auto pos = writePosition();
        auto* dst = data();
        dst = dst + pos;
        memcpy(dst, src, len);
        setWritePosition(pos + len);
        return len;
      }

      virtual SizeType read(ValueType* dst, SizeType len) {
        auto availSize = availableToRead();

        assert(len <= availSize);

        auto pos = readPosition();
        auto* src = data();
        src = src + pos;
        memcpy(dst, src, len);
        setReadPosition(pos + len);
        return len;
      }


  };

  /**
   * \brief A fixed size buffer
   *
   * \tparam N Size of the buffer
   * \tparam T Data type of the buffer, defaults to `BYTE`
   */
  template <std::size_t N, typename T = BYTE>
  class FixedBuffer : public Buffer<std::array<T, N>> {
    using Storage = std::array<T, N>;

    public:

      FixedBuffer() {
        clear();
      }

      void clear() {
        storage_.fill(0);
      }

      virtual Buffer<std::array<T, N>> clone() {
        FixedBuffer copy;
        std::copy(storage_.begin(), storage_.end(), copy.storage_.begin());
        return std::move(copy);
      }

    protected:

      Storage& getStorage() override {
        return storage_;
      }

      const Storage& getStorage() const override {
        return storage_;
      }

    private:

      Storage storage_{};


  };

  template <typename T = BYTE>
  using DynamicBufferStorage = std::vector<T>;

  template <typename T = BYTE> requires is_resizeable<DynamicBufferStorage<T>>
  class DynamicBuffer : public Buffer<DynamicBufferStorage<T>> {

    public:

      using Storage = DynamicBufferStorage<T>;
      using BufferType = Buffer<DynamicBufferStorage<T>>;
      using SizeType = typename BufferType::SizeType;
      using ValueType = typename BufferType::ValueType;

      explicit DynamicBuffer(SizeType initialSize = 1024u) {
        resizeInternal(initialSize);
      };

      virtual void reset() override {
        storage_.resize(storage_.size(), 0);
        Buffer<Storage>::reset();
      };

      void clear() {
        reset();

      }

      virtual bool resize(SizeType newSize) override {
        return resizeInternal(newSize);
      }

    protected:

      Storage& getStorage() override {
        return storage_;
      }

      const Storage& getStorage() const override {
        return storage_;
      }

    private:

      bool resizeInternal(SizeType newSize) {
        if (newSize < BufferMinSize) {
          newSize = BufferMinSize;
        }
        storage_.resize(newSize);
        return storage_.size() >= newSize;
      }

      Storage storage_{};
  };

  using DynamicByteBuffer = DynamicBuffer<BYTE>;

}
