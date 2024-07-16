#pragma once


#include <memory>
#include <array>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <functional>
#include <iostream>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/strand.hpp>
#include <asio/steady_timer.hpp>
#include <asio/bind_executor.hpp>
#include <asio/co_spawn.hpp>
#include <asio/awaitable.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/use_future.hpp>
#include <asio/detached.hpp>
#include <asio/redirect_error.hpp>
#include "request.hpp"
#include "reply.hpp"
#include "request_handler.hpp"
#include "request_parser.hpp"
#include "MessageQueue.hpp"


using namespace asio;
using namespace std::placeholders;


using ReadBuffer = std::array<char, 300>;
using MilliSecs = std::chrono::milliseconds;
using Seconds = std::chrono::seconds;

class mini_session;

void Log(mini_session* ptr, std::size_t session_count, std::size_t ref_count, const std::string& log)
{
	std::string print_status = "address : %p / session_count : %d / ref_count : %d / thr_id : %d / func_name : %s\n";
	printf(print_status.c_str(), ptr, session_count, ref_count, std::this_thread::get_id(), log.c_str());
}


struct SessionStatus
{
	steady_timer read_timer;
	steady_timer write_timer;
	steady_timer session_check_timer;
	steady_timer ddos_attack_timer;
	steady_timer parsing_timer;
	steady_timer data_timer;
	std::unique_ptr<std::atomic_size_t> ddos_counter;
};

enum class PacketStatus
{
	READ,
	WRITE,
	SESSION_CHECK,
	DDOS_CHECK,
	PARSING_FAIL,
	INVALID_DATA
};


struct OwnedBuffer
{
	std::shared_ptr<mini_session> session;
	std::shared_ptr<ReadBuffer> read_buffer;
	std::size_t buffer_size;
};

class mini_server;
class mini_session : public std::enable_shared_from_this<mini_session>
{
public:
	mini_session(
		ip::tcp::socket& socket, request_handler& request_handler, request_parser& request_parser,
		int counter, MessageQueue<OwnedBuffer>& read_buffer_q, std::unordered_map<std::string, std::shared_ptr<SessionStatus>>& ddos_ip_list)
		:
		m_socket(std::move(socket)), // ioc_co
		m_session_strand(m_socket.get_executor()),
		m_request_handler(request_handler),
		m_request_parser(request_parser),
		m_ip_address(m_socket.remote_endpoint().address().to_string()),
		m_connection_counter(counter),
		m_msg_q_from_server(read_buffer_q),
		m_ddos_ip_list(ddos_ip_list)
	{
		m_packet.reserve(8000);
	}


	~mini_session()
	{
		Log(this, GetSessionCount(), 0, "destroy");
	}

	void Start()
	{
		//m_wait_handler_f = std::bind(&mini_session::WaitHandler, shared_from_this(), _1, _2, _3);

		const std::string& ip_address = GetAddress();

		auto it = m_ddos_ip_list.find(ip_address);

		if (it == m_ddos_ip_list.end())
		{
			m_ddos_ip_list.insert(std::pair<std::string, std::shared_ptr<SessionStatus>>(
				ip_address,
				std::make_shared<SessionStatus>(SessionStatus{
					steady_timer(m_session_strand),
					steady_timer(m_session_strand),
					steady_timer(m_session_strand),
					steady_timer(m_session_strand),
					steady_timer(m_session_strand),
					steady_timer(m_session_strand),
					std::make_unique<std::atomic_size_t>(1) 
				})));

			it = m_ddos_ip_list.find(ip_address);

			//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "init");
			goto JUMP_TO_ELSE_PART;
		}
		else
		{
		JUMP_TO_ELSE_PART:

			std::shared_ptr<SessionStatus>& status = it->second;

			SetSessionStatus(status);

			size_t counter = status->ddos_counter->fetch_add(1, std::memory_order_release);

			if (counter == 1)
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "session_check_start");

				co_spawn(m_session_strand, bind_executor(m_session_strand,
					std::bind(&mini_session::WaitHandler, shared_from_this(),
						PacketStatus::SESSION_CHECK, std::ref(status->session_check_timer), Seconds(20))), detached);

			}
			else if (counter == 4)
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "ddos_atack_start");

				co_spawn(m_session_strand, bind_executor(m_session_strand,
					std::bind(&mini_session::WaitHandler, shared_from_this(),
						PacketStatus::DDOS_CHECK, std::ref(status->ddos_attack_timer), Seconds(20))), detached);
			}

			if (counter > 3)
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "delay_to_write");

				co_spawn(m_session_strand, bind_executor(m_session_strand,
					std::bind(&mini_session::WaitHandler, shared_from_this(),
						PacketStatus::WRITE, std::ref(status->write_timer), Seconds(3))), detached);
			}
			else
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "wait_start");

				co_spawn(m_session_strand, bind_executor(m_session_strand,
					std::bind(&mini_session::WaitHandler,
						shared_from_this(), PacketStatus::READ, std::ref(status->read_timer), Seconds(2))), detached);

				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "read_start");

				co_spawn(m_session_strand, bind_executor(m_session_strand, std::bind(&mini_session::ReadPacket, shared_from_this())), detached);

			}
		}
	}

	const std::string& GetAddress()
	{
		return m_ip_address;
	}

	std::string& GetPacket()
	{
		return m_packet;
	}

	std::shared_ptr<SessionStatus>& GetSessionStatus()
	{
		return m_session_status;
	}

	void SetSessionStatus(std::shared_ptr<SessionStatus>& status)
	{
		if (m_session_status == nullptr)
			m_session_status = status;
	}

	void CloseSocket()
	{
		if (m_socket.is_open())
			m_socket.close();
	}

	std::size_t& GetSessionCount()
	{
		return m_connection_counter;
	}

private:

	awaitable<void> ReadPacket()
	{
		std::error_code ec;
		while (m_socket.is_open())
		{
			std::shared_ptr<ReadBuffer> char_buffer = std::make_shared<ReadBuffer>();

			size_t bytes_transferred = co_await m_socket.async_read_some(buffer(*char_buffer), bind_executor(m_session_strand, redirect_error(use_awaitable, ec)));

			if (!ec)
			{
				m_msg_q_from_server.push(OwnedBuffer(shared_from_this(), char_buffer, bytes_transferred));
			}
		}

	}

	awaitable<void> WaitHandler(PacketStatus status, steady_timer& timer, Seconds milli_sec)
	{
		timer.expires_after(milli_sec);

		switch (status)
		{
		case PacketStatus::READ:
		{
			std::error_code ec;

			co_await timer.async_wait(bind_executor(m_session_strand, redirect_error(use_awaitable, ec)));

			if (ec)
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "wait_close_false!");

				parse_result result;

				m_request_parser.reset();

				std::tie(result, std::ignore) = m_request_parser.parse(
					m_request, m_packet.data(), m_packet.data() + m_packet.size());

				if (result == parse_result::YES)
				{
					m_request_handler.handle_request(m_request, m_reply);
				}
				else if (result == parse_result::NO)
				{
					m_reply = reply::stock_reply(reply::status_type::bad_request);
				}

				co_await async_write(m_socket, m_reply.to_buffers(), bind_executor(m_session_strand, use_awaitable));
			}
			else
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "wait_close_true!");
			}

			CloseSocket();

			break;
		}
		case PacketStatus::WRITE:
		{
			std::error_code ec;

			co_await timer.async_wait(bind_executor(m_session_strand, redirect_error(use_awaitable, ec)));

			if (!ec)
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "ddos_attack_write_end!");

				co_await async_write(m_socket, reply::stock_reply(reply::status_type::forbidden).to_buffers(), bind_executor(m_session_strand, use_awaitable));
			}

			CloseSocket();

			break;
		}
		case PacketStatus::DDOS_CHECK:
		{
			co_await timer.async_wait(bind_executor(m_session_strand, use_awaitable));

			//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "ddos_check_end!");

			m_session_status->ddos_counter->store(0, std::memory_order_release);
			break;
		}
		case PacketStatus::SESSION_CHECK:
		{
			co_await timer.async_wait(bind_executor(m_session_strand, use_awaitable));

			//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "session_check_end!");

			m_ddos_ip_list.erase(GetAddress());

			break;
		}
		case PacketStatus::PARSING_FAIL:
		{
			co_await timer.async_wait(bind_executor(m_session_strand, use_awaitable));

			//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "parsing_fail!");

			co_await async_write(m_socket, reply::stock_reply(reply::status_type::bad_request).to_buffers(), bind_executor(m_session_strand, use_awaitable));

			break;
		}
		case PacketStatus::INVALID_DATA:
		{
			co_await timer.async_wait(bind_executor(m_session_strand, use_awaitable));

			//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "invalid_data!");

			co_await async_write(m_socket, reply::stock_reply(reply::status_type::not_found).to_buffers(), bind_executor(m_session_strand, use_awaitable));

			break;
		}
		}
	}

private:
	ip::tcp::socket m_socket;
	strand<any_io_executor> m_session_strand; //ioc_co


	std::string m_packet;

	request m_request;
	reply m_reply;
	request_handler& m_request_handler;
	request_parser& m_request_parser;
	const std::string m_ip_address;

	std::size_t m_connection_counter;

	std::shared_ptr<SessionStatus> m_session_status;
	MessageQueue<OwnedBuffer>& m_msg_q_from_server;
	std::unordered_map<std::string, std::shared_ptr<SessionStatus>>& m_ddos_ip_list;

	std::function<void(PacketStatus, steady_timer&, std::chrono::seconds)> m_wait_handler_f;
};

class mini_server
{
public:

	explicit mini_server(const std::string& address, const std::string& port, const std::string& doc_root)
		:
		m_acceptor(m_main_ioc, asio::ip::tcp::endpoint(asio::ip::make_address(address), std::stoi(port))),
		m_request_handler(doc_root),
		m_server_strand(m_sub_ioc.get_executor()),
		m_main_thread_pool(1),
		m_sub_thread_pool(2),
		m_blocking_timer(m_server_strand),
		server_is_shuttdown(false),
		m_connection_counter(0)
	{
		co_spawn(m_main_ioc, StartByCoroutine(), detached);
		co_spawn(m_sub_ioc, LoopByCoroutine(), detached);
	}

	void Run()
	{
		m_chainning_buffer_f = std::bind(&mini_server::ChainingBuffer, this, _1, _2, _3);

		for (auto& thr : m_main_thread_pool) { thr = std::thread([this]() { m_main_ioc.run(); }); }
		for (auto& thr : m_sub_thread_pool) { thr = std::thread([this]() { m_sub_ioc.run(); }); }

		printf("Server On!\n");


		while (1)
		{
			Update(true);
		}

		m_main_ioc.stop();
		m_sub_ioc.stop();
		for (auto& thr : m_main_thread_pool) if (thr.joinable()) thr.join();
		for (auto& thr : m_sub_thread_pool) if (thr.joinable()) thr.join();

		server_is_shuttdown = true;
	}

	void ChainingBuffer(std::shared_ptr<mini_session> session, std::shared_ptr<ReadBuffer> buf_arr_ptr, size_t bytes_transffered)
	{
		//Log(session.get(), session->GetSessionCount(), session.use_count(), "chainning_buffer!");

		std::string& packet = session->GetPacket();

		packet.append(buf_arr_ptr->begin(), buf_arr_ptr->begin() + bytes_transffered);

		size_t size = packet.size();

		SessionStatus& status = *session->GetSessionStatus();

		if (size < 9999)
		{
			if (packet[size - 4] == '\r' && packet[size - 3] == '\n' && packet[size - 2] == '\r' && packet[size - 1] == '\n')
			{				
				status.read_timer.cancel();
			}
		}
		else
		{
			status.ddos_counter->fetch_add(1);
		}
	}

	awaitable<void> LoopByCoroutine()
	{
		std::error_code ec;
		while (!server_is_shuttdown)
		{
			m_blocking_timer.expires_at(std::chrono::steady_clock::time_point::max());
			co_await m_blocking_timer.async_wait(redirect_error(use_awaitable, ec));

			if (ec)
			{
				auto message_from_session = m_read_buffer_q.pop_front();
				post(m_server_strand, bind_executor(m_server_strand, std::bind(m_chainning_buffer_f, message_from_session.session, message_from_session.read_buffer, message_from_session.buffer_size)));
			}

		}
		co_return;
	}

	void Update(bool is_waitting)
	{
		if (is_waitting) m_read_buffer_q.wait();

		while (!m_read_buffer_q.empty())
		{
			m_blocking_timer.cancel();
		}

	}


private:
	awaitable<void> StartByCoroutine()
	{
		for (;;)
		{
			m_connection_counter++;

			std::error_code ec;

			ip::tcp::socket&& socket = co_await m_acceptor.async_accept(co_await this_coro::executor, redirect_error(use_awaitable, ec));

			if (!ec)
			{
				auto session = std::make_shared<mini_session>(
					socket,
					m_request_handler,
					m_request_parser,
					m_connection_counter,
					m_read_buffer_q,
					m_ddos_ip_list);
				
				Log(session.get(), session->GetSessionCount(), session.use_count(), "listen!");

				session->Start();

			}
		}
	}



private:
	io_context m_main_ioc, m_sub_ioc;
	strand<any_io_executor> m_server_strand;
	std::vector<std::thread> m_main_thread_pool, m_sub_thread_pool;


	ip::tcp::acceptor m_acceptor;
	request_handler m_request_handler;
	request_parser m_request_parser;
	std::unordered_map<std::string, std::shared_ptr<SessionStatus>> m_ddos_ip_list;
	std::size_t m_connection_counter;

	MessageQueue<OwnedBuffer> m_read_buffer_q;
	steady_timer m_blocking_timer;
	bool server_is_shuttdown;
	std::function<void(std::shared_ptr<mini_session>, std::shared_ptr<ReadBuffer>, std::size_t)> m_chainning_buffer_f;
};

