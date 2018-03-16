#ifndef __BUFFER_H__
#define __BUFFER_H__

#include <cstdint>
#include <vector>
#include <algorithm>
#include <boost/asio.hpp>
#include <boost/log/trivial.hpp>

#include "common_utils/common.h"

class Buffer {
public:
    Buffer(size_t max_length = 8192)
        : buf_(std::max(1024UL, max_length)), curr_(0) {
    }

    void DeQueue(size_t len) {
        size_t total = curr_;
        if (len > total) {
            LOG(FATAL) << "Buffer::Dequeue len > total";
            len = total;
        }
        std::copy(Begin() + len, End(), Begin());
        curr_ = total - len;
    }

    void Append(size_t len) {
        PrepareCapacity(len);
        curr_ += len;
    }

    template<class Container>
    void AppendData(const Container cont) {
        PrepareCapacity(cont.size());
        std::copy(std::begin(cont), std::end(cont), End());
        Append(cont.size());
    }

    void Reset(size_t new_len = 0) {
        curr_ = new_len;
    }

    size_t Size() const {
        return curr_;
    }

    uint8_t *Begin() {
        return buf_.data();
    }

    uint8_t *End() {
        return Begin() + Size();
    }

    void PrepareCapacity(size_t more_length) {
        size_t total = Size() + more_length;
        if (total > buf_.size()) {
            ExpandCapacity(total);
        }
    }

    void ExpandCapacity(size_t new_capacity) {
        if (buf_.size() < new_capacity) {
            buf_.resize(new_capacity);
        }
    }

    boost::asio::mutable_buffer get_buffer() {
        return boost::asio::buffer(buf_) + curr_;
    }

    boost::asio::const_buffer get_const_buffer() const {
        return boost::asio::buffer(buf_.data(), curr_);
    }

    uint8_t *get_data() {
        return Begin();
    }

private:
    std::vector<uint8_t> buf_;
    size_t curr_;
};

#endif

