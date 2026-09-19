#ifndef AUDIO_RING_BUFFER_H
#define AUDIO_RING_BUFFER_H

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

/* Single-producer/single-consumer byte queue for audio transport.
 *
 * The emulation thread is the producer and the SDL audio callback is the
 * consumer.  The queue never waits: an underrun is reported to the caller and
 * an overrun drops the newest block.  Keeping this independent of SDL lets the
 * same transport be used by SDL2, SDL2-compat, or an SDL3 adapter. */
class AudioRingBuffer {
public:
    AudioRingBuffer() : read_index(0), write_index(0), underruns(0), overruns(0) {}

    bool initialize(size_t capacity_bytes)
    {
        if (capacity_bytes < 2)
            return false;

        buffer.assign(capacity_bytes, 0);
        read_index.store(0, std::memory_order_relaxed);
        write_index.store(0, std::memory_order_relaxed);
        underruns.store(0, std::memory_order_relaxed);
        overruns.store(0, std::memory_order_relaxed);
        return true;
    }

    void reset()
    {
        read_index.store(0, std::memory_order_relaxed);
        write_index.store(0, std::memory_order_relaxed);
    }

    size_t readable_bytes() const
    {
        const size_t read = read_index.load(std::memory_order_relaxed);
        const size_t write = write_index.load(std::memory_order_acquire);
        return write >= read ? write - read : buffer.size() - read + write;
    }

    size_t writable_bytes() const
    {
        const size_t read = read_index.load(std::memory_order_acquire);
        const size_t write = write_index.load(std::memory_order_relaxed);
        const size_t used = write >= read ? write - read : buffer.size() - read + write;
        return buffer.size() - used - 1;
    }

    size_t write(const uint8_t *source, size_t length)
    {
        if (buffer.empty() || source == NULL || length == 0)
            return 0;

        const size_t available = writable_bytes();
        if (length > available) {
            overruns.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }

        copy_into_buffer(source, length, write_index.load(std::memory_order_relaxed));
        const size_t next = advance(write_index.load(std::memory_order_relaxed), length);
        write_index.store(next, std::memory_order_release);
        return length;
    }

    size_t read(uint8_t *destination, size_t length)
    {
        if (buffer.empty() || destination == NULL || length == 0)
            return 0;

        const size_t available = readable_bytes();
        const size_t amount = std::min(length, available);
        if (amount == 0) {
            underruns.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }

        copy_from_buffer(destination, amount, read_index.load(std::memory_order_relaxed));
        const size_t next = advance(read_index.load(std::memory_order_relaxed), amount);
        read_index.store(next, std::memory_order_release);
        return amount;
    }

    uint64_t underrun_count() const
    {
        return underruns.load(std::memory_order_relaxed);
    }

    uint64_t overrun_count() const
    {
        return overruns.load(std::memory_order_relaxed);
    }

private:
    size_t advance(size_t index, size_t amount) const
    {
        index += amount;
        return index < buffer.size() ? index : index - buffer.size();
    }

    void copy_into_buffer(const uint8_t *source, size_t length, size_t index)
    {
        const size_t first = std::min(length, buffer.size() - index);
        memcpy(&buffer[index], source, first);
        if (first < length)
            memcpy(&buffer[0], source + first, length - first);
    }

    void copy_from_buffer(uint8_t *destination, size_t length, size_t index)
    {
        const size_t first = std::min(length, buffer.size() - index);
        memcpy(destination, &buffer[index], first);
        if (first < length)
            memcpy(destination + first, &buffer[0], length - first);
    }

    std::vector<uint8_t> buffer;
    std::atomic<size_t> read_index;
    std::atomic<size_t> write_index;
    std::atomic<uint64_t> underruns;
    std::atomic<uint64_t> overruns;
};

#endif
