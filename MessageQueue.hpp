#pragma once
#include <mutex>
#include <condition_variable>
#include <queue>

template<typename T>
class MessageQueue
{
public:
	T front()
	{
		std::scoped_lock lock(m_mux_q);
		return m_message_q.front();
	}

	void pop()
	{
		std::scoped_lock lock(m_mux_q);
		m_message_q.pop();
	}

	T pop_front()
	{
		std::scoped_lock lock(m_mux_q);
		T temp = std::move(m_message_q.front());
		m_message_q.pop();
		return temp;
	}

	void push(T msg, bool is_notifying)
	{
		std::scoped_lock lock_q(m_mux_q);
		m_message_q.push(msg);

		if (is_notifying)
		{
			std::unique_lock<std::mutex> lock_conv(m_mux_lock);
			m_conv.notify_one();
		}

	}
	bool empty()
	{
		std::scoped_lock lock(m_mux_q);
		return m_message_q.empty();
	}

	std::size_t size()
	{
		std::scoped_lock lock(m_mux_q);
		return m_message_q.size();
	}

	void clear()
	{
		std::scoped_lock lock(m_mux_q);
		std::queue<T> temp;
		std::swap(m_message_q, temp);
	}

	void wait()
	{
		while (empty())
		{
			std::unique_lock<std::mutex> lock(m_mux_lock);
			m_conv.wait(lock);
		}
	}
private:

	std::mutex m_mux_lock, m_mux_q;
	std::condition_variable m_conv;
	std::queue<T> m_message_q;
};