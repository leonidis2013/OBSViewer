#pragma once

#include <cstddef>
#include <vector>

namespace obsviewer {

// Fixed-size history of samples, oldest first. Used for the 5-minute bitrate and
// loudness graphs (300 samples at one per second).
template<typename T> class History {
public:
	explicit History(size_t capacity = 300) : data_(capacity) {}

	void push(const T &value)
	{
		if (data_.empty())
			return;
		data_[head_] = value;
		head_ = (head_ + 1) % data_.size();
		if (size_ < data_.size())
			++size_;
	}

	void clear()
	{
		head_ = 0;
		size_ = 0;
	}

	size_t size() const { return size_; }
	size_t capacity() const { return data_.size(); }
	bool empty() const { return size_ == 0; }

	// i = 0 is the oldest sample still held.
	const T &at(size_t i) const { return data_[(head_ + data_.size() - size_ + i) % data_.size()]; }
	const T &latest() const { return at(size_ - 1); }

	std::vector<T> toVector() const
	{
		std::vector<T> out;
		out.reserve(size_);
		for (size_t i = 0; i < size_; ++i)
			out.push_back(at(i));
		return out;
	}

private:
	std::vector<T> data_;
	size_t head_ = 0;
	size_t size_ = 0;
};

} // namespace obsviewer
