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
	steady_timer session_check_timer;
	steady_timer ddos_attack_timer;
	std::shared_ptr<std::atomic_size_t> ddos_counter;
	std::shared_ptr<std::atomic_bool> ddos_timer_is_running;
};

enum class PacketStatus
{
	READ,
	WRITE,
	SESSION_CHECK,
	DDOS_CHECK,
};


struct OwnedBuffer
{
	std::shared_ptr<mini_session> session;
	std::shared_ptr<ReadBuffer> read_buffer;
	std::shared_ptr<reply::status_type> status;
	std::size_t buffer_size;
};


class mini_server;
class mini_session : public std::enable_shared_from_this<mini_session>
{
public:
	mini_session(
		ip::tcp::socket& socket,
		int counter, MessageQueue<OwnedBuffer>& read_buffer_q, std::unordered_map<std::string, std::shared_ptr<SessionStatus>>& ddos_ip_list)
		:
		m_socket(std::move(socket)), // ioc_co
		m_session_strand(m_socket.get_executor()),
		m_read_timer(m_session_strand),
		m_write_timer(m_session_strand),
		m_ip_address(m_socket.remote_endpoint().address().to_string()),
		m_connection_counter(counter),
		m_msg_q_from_server(read_buffer_q),
		m_ddos_ip_list(ddos_ip_list)
	{
		m_packet.reserve(8000);
	}


	~mini_session()
	{
		//Log(this, GetSessionCount(), 0, "destroy");
	}

	void Start()
	{
		post(m_session_strand, bind_executor(m_session_strand,
			std::bind(&mini_session::InsertSessionStatus, shared_from_this())));


		post(m_session_strand, bind_executor(m_session_strand,
			std::bind(&mini_session::OperateWaitHandler, shared_from_this())));
	}

	const std::string& GetAddress()
	{
		return m_ip_address;
	}

	std::string& GetPacket()
	{
		return m_packet;
	}

	ip::tcp::socket& GetSocket()
	{
		return m_socket;
	}

	std::shared_ptr<SessionStatus>& GetSessionStatus()
	{
		return m_session_status;
	}

	steady_timer& GetTimerInSession(PacketStatus status)
	{
		switch (status)
		{
		case PacketStatus::READ:
		{
			return m_read_timer;
			
		}
		case PacketStatus::WRITE:
		{
			return m_write_timer;
		}
		}
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

	void InsertSessionStatus()
	{
		const std::string& ip_address = GetAddress();

		auto it = m_ddos_ip_list.find(ip_address);

		if (it == m_ddos_ip_list.end())
		{
			m_ddos_ip_list.insert(std::pair<std::string, std::shared_ptr<SessionStatus>>(
				ip_address,
				std::make_shared<SessionStatus>(SessionStatus{
					steady_timer(m_session_strand),
					steady_timer(m_session_strand),
					std::make_shared<std::atomic_size_t>(1),
					std::make_shared<std::atomic_bool>(false)
				})));
		}
	}

	void OperateWaitHandler()
	{

		const std::string& ip_address = GetAddress();

		auto it = m_ddos_ip_list.find(ip_address);

		if (it != m_ddos_ip_list.end())
		{
			std::shared_ptr<SessionStatus>& status = it->second;

			SetSessionStatus(status);
			
		
			size_t ddos_count = status->ddos_counter->fetch_add(1, std::memory_order_release);

			if (ddos_count == 1)
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "session_check_start");

				co_spawn(m_socket.get_executor(), std::bind(&mini_session::WaitHandler, shared_from_this(),
						PacketStatus::SESSION_CHECK), detached);

			}
			if (ddos_count == 20)
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "ddos_atack_start");

				co_spawn(m_socket.get_executor(), std::bind(&mini_session::WaitHandler, shared_from_this(),
						PacketStatus::DDOS_CHECK), detached);
			}

			if (ddos_count > 20)
			{
				if(!status->ddos_timer_is_running->load())
					co_spawn(m_socket.get_executor(), std::bind(&mini_session::WaitHandler, shared_from_this(),
							PacketStatus::WRITE), detached);
			}
			else
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "wait_start");

				co_spawn(m_session_strand, bind_executor(m_session_strand,
					std::bind(&mini_session::WaitHandler,
						shared_from_this(), PacketStatus::READ)), detached);

				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "read_start");

				co_spawn(m_session_strand, bind_executor(m_session_strand, std::bind(&mini_session::ReadPacket, shared_from_this())), detached);
			}
		}
	}

	awaitable<void> ReadPacket()
	{
		std::error_code ec;
		while (true)
		{
			std::shared_ptr<ReadBuffer> char_buffer = std::make_shared<ReadBuffer>();

			size_t bytes_transferred = co_await m_socket.async_read_some(buffer(*char_buffer), bind_executor(m_session_strand, redirect_error(use_awaitable, ec)));
	
			if (!ec)
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "read_c!");
				
				m_msg_q_from_server.push(OwnedBuffer{ shared_from_this(), char_buffer, nullptr, bytes_transferred }, true);
			}
			else {
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "read_f!");
				break;
			}
		}
	}

	awaitable<void> WaitHandler(PacketStatus status)
	{
		

		std::error_code ec;

		switch (status)
		{
		case PacketStatus::READ:
		{
			m_read_timer.expires_after(Seconds(2));
			co_await m_read_timer.async_wait(redirect_error(use_awaitable, ec));

			if (!ec)
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "wait_close_true!");

				m_msg_q_from_server.push(OwnedBuffer(shared_from_this(), nullptr, std::make_shared<reply::status_type>(reply::status_type::not_found), 0), true);
	
			}
			else
			{
				//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "wait_close_false!");
			}

			//CloseSocket();

			break;
		}
		case PacketStatus::WRITE:
		{
			m_write_timer.expires_after(Seconds(3));
			co_await m_write_timer.async_wait(use_awaitable);

			//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "ddos_attack_write_end!");
			
			m_msg_q_from_server.push(OwnedBuffer(shared_from_this(), nullptr, std::make_shared<reply::status_type>(reply::status_type::forbidden), 0), true);

			if (m_session_status != nullptr)
				m_session_status->ddos_timer_is_running->store(false);

			break;
		}
		case PacketStatus::DDOS_CHECK:
		{
			m_session_status->ddos_attack_timer.expires_after(Seconds(600));
			co_await m_session_status->ddos_attack_timer.async_wait(use_awaitable);

			//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "ddos_check_end!");

			m_session_status->ddos_counter->store(0);

			break;
		}
		case PacketStatus::SESSION_CHECK:
		{
			m_session_status->session_check_timer.expires_after(Seconds(600));
			co_await m_session_status->session_check_timer.async_wait(use_awaitable);

			//Log(shared_from_this().get(), GetSessionCount(), shared_from_this().use_count(), "session_check_end!");

			post(m_session_strand, bind_executor(m_session_strand, [this]() { m_ddos_ip_list.erase(GetAddress()); }));

			break;
		}

		}
	}

private:
	ip::tcp::socket m_socket;
	strand<any_io_executor> m_session_strand; //ioc_co

	std::string m_packet;
	const std::string m_ip_address;

	steady_timer m_read_timer;
	steady_timer m_write_timer;

	std::size_t m_connection_counter;

	std::shared_ptr<SessionStatus> m_session_status;
	MessageQueue<OwnedBuffer>& m_msg_q_from_server;
	std::unordered_map<std::string, std::shared_ptr<SessionStatus>>& m_ddos_ip_list;

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
		m_connection_counter(0)
	{
		co_spawn(m_main_ioc, StartToAccept(), detached);
		
		co_spawn(m_sub_ioc, []()->awaitable<void> { 
			steady_timer timer(co_await this_coro::executor, std::chrono::steady_clock::time_point::max()); 
			co_await timer.async_wait(use_awaitable);  
			}, detached);
		
	}

	void Run()
	{
		//is_server_running = true;

		m_chainning_buffer_f = std::bind(&mini_server::ChainingBuffer, this, _1, _2, _3, _4);

		for (auto& thr : m_sub_thread_pool) { thr = std::thread([this]() { m_sub_ioc.run(); }); }
		for (auto& thr : m_main_thread_pool) { thr = std::thread([this]() { m_main_ioc.run(); }); }

		printf("Server On!\n");


		while (1)
		{
			Update(true);
		}

		m_main_ioc.stop();
		m_sub_ioc.stop();
		
		for (auto& thr : m_main_thread_pool) if (thr.joinable()) thr.join();
		for (auto& thr : m_sub_thread_pool) if (thr.joinable()) thr.join();
	}

	void ChainingBuffer(std::shared_ptr<mini_session> session, std::shared_ptr<ReadBuffer> buf_arr_ptr, std::shared_ptr<reply::status_type> reply_status, size_t bytes_transffered)
	{
		//Log(session.get(), session->GetSessionCount(), session.use_count(), "chainning_buffer!");


		if (buf_arr_ptr != nullptr)
		{
			std::string& packet = session->GetPacket();

			packet.append(buf_arr_ptr->begin(), buf_arr_ptr->begin() + bytes_transffered);

			size_t size = packet.size();

			SessionStatus& session_status = *session->GetSessionStatus();


			if (packet[size - 4] == '\r' && packet[size - 3] == '\n' && packet[size - 2] == '\r' && packet[size - 1] == '\n')
			{
				std::shared_ptr<reply> rep = std::make_shared<reply>();

				if (size < 9999)
				{
					request req;
					parse_result result;

					m_request_parser.reset();

					std::tie(result, std::ignore) = m_request_parser.parse(
						req, packet.data(), packet.data() + packet.size());

					if (result == parse_result::YES)
						m_request_handler.handle_request(req, *rep);
					else
						*rep = reply::stock_reply(reply::status_type::bad_request);

					session->GetTimerInSession(PacketStatus::READ).cancel();
					// 파싱 분석 후 쓰기 큐에 삽입??? or 여기서 루틴 생성 후 바로 전송?
				}
				else
				{
					*rep = reply::stock_reply(reply::status_type::service_unavailable);
					session_status.ddos_counter->fetch_add(1);
				}

				co_spawn(m_sub_ioc, SendToBrowser(session, rep), detached);
			}


	
		}
		else
		{
			std::shared_ptr<reply> rep = std::make_shared<reply>();
			*rep = reply::stock_reply(*reply_status);
			co_spawn(m_sub_ioc, SendToBrowser(session, rep), detached);
		}



	}

	void Update(bool is_waitting) 
	{
		if (is_waitting) m_read_buffer_q.wait();

		while (!m_read_buffer_q.empty())
		{
			//m_timer_block_to_parse.cancel();

			auto&& buf_from_session = m_read_buffer_q.pop_front();

			//Log(buf_from_session.session.get(), buf_from_session.session->GetSessionCount(), buf_from_session.session.use_count(), "update!");

			
			post(m_server_strand, bind_executor(m_server_strand, std::bind(m_chainning_buffer_f, buf_from_session.session, buf_from_session.read_buffer, buf_from_session.status, buf_from_session.buffer_size)));
		}
	}

private:


	awaitable<void> StartToAccept()
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
					m_connection_counter,
					m_read_buffer_q,
					m_ddos_ip_list);
				
				//Log(session.get(), session->GetSessionCount(), session.use_count(), "listen!");

				session->Start();

			}
		}
	}


	awaitable<void> SendToBrowser(std::shared_ptr<mini_session> session, std::shared_ptr<reply> rep)
	{
		std::error_code ec;


		co_await async_write(session->GetSocket(), rep->to_buffers(), use_awaitable);

		//Log(session.get(), session->GetSessionCount(), session.use_count(), "SendToBrowser!");

		session->CloseSocket();

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



	std::function<void(std::shared_ptr<mini_session>, std::shared_ptr<ReadBuffer>, std::shared_ptr<reply::status_type>, std::size_t)> m_chainning_buffer_f;
};

