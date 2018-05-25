/**
* @file boost_socks5.cpp
* @brief Simple SOCKS5 proxy server realization using boost::asio library
* @962072900@qq.com
*/
#include "stdafx.h"
#include <cstdlib>
#include <string>
#include <memory>
#include <utility>
#include <boost/asio.hpp>
#include <fstream>

#include "port_ran.h"

using boost::asio::ip::tcp;

// Common log function
inline void write_log(int prefix, short verbose, short verbose_level, int session_id, const std::string& what, const std::string& error_message = "")
{
	//if (verbose > verbose_level) return;

	//std::string session = "";
	//if (session_id >= 0) { session += "session("; session += std::to_string(session_id); session += "): "; }

	//if (prefix > 0)
	//{
	//	std::cerr << (prefix == 1 ? "Error: " : "Warning: ") << session << what;
	//	if (error_message.size() > 0)
	//		std::cerr << ": " << error_message;
	//	std::cerr << std::endl;
	//}
	//else
	//{
	//	std::cout << session << what;
	//	if (error_message.size() > 0)
	//		std::cout << ": " << error_message;
	//	std::cout << std::endl;
	//}
}

class Session : public std::enable_shared_from_this<Session>
{
public:
	Session(tcp::socket in_socket, unsigned session_id, size_t buffer_size, short verbose)
		: in_socket_(std::move(in_socket)),
		out_socket_(in_socket.get_io_service()),
		resolver(in_socket.get_io_service()),
		in_buf_(buffer_size),
		out_buf_(buffer_size),
		session_id_(session_id),
		verbose_(verbose)
	{
	}

	void start()
	{
		read_socks5_handshake();
	}

private:
	//��ȡ��һ������ ��֤
	void read_socks5_handshake()
	{
		auto self(shared_from_this());

		in_socket_.async_receive(boost::asio::buffer(in_buf_),
			[this, self](boost::system::error_code ec, std::size_t length)
		{
			if (!ec)
			{
				/*�ӿͻ��˽���Э������
				+----+----------+----------+
				|VER | NMETHODS | METHODS  |
				|�汾| ������   | ����1,����2...
				+----+----------+----------+
				| 1  |    1     | 1 to 255 |
				+----+----------+----------+
				METHODS
				0x00      ��Ҫ�����֤
				0x01      ͨ��GSSAPIЭ����֤
				0x02      ͨ���ʺ�������֤
				0x03-0x7f ��IANA��֯����
				0x80-0xFE ������˽����
				OxFF      û�пɽ��ܵķ���
				*/
				if (length < 3 || in_buf_[0] != 0x05)
				{
					write_log(1, 0, verbose_, session_id_, "SOCKS5 handshake request is invalid. Closing session.");
					return;
				}

				uint8_t num_methods = in_buf_[1];
				/*����˻�Ӧ����
				+----+-----------------+
				|VER | METHOD CHOSSED  |
				+----+-----------------+
				| 1  |    1 to 255     |
				+----+-----------------+
				0x00      ��Ҫ�����֤
				0x01      ͨ��GSSAPIЭ����֤
				0x02      ͨ���ʺ�������֤
				0x03-0x7f ��IANA��֯����
				0x80-0xFE ������˽����
				OxFF      û�пɽ��ܵķ���
				*/
				in_buf_[1] = 0xFF;

				// ֻ��0x00 - ����Ҫ�����֤�� ����ֻ֧��
				for (uint8_t method = 0; method < num_methods; ++method)
				{
					if (in_buf_[2 + method] == 0x00) { in_buf_[1] = 0x00; break; }
				}

				write_socks5_handshake();
			}
			else
				write_log(1, 0, verbose_, session_id_, "SOCKS5 handshake request", ec.message());

		});
	}
	//���� sock5 ��֤Э�� ����
	void write_socks5_handshake()
	{
		auto self(shared_from_this());
		boost::asio::async_write(in_socket_, boost::asio::buffer(in_buf_, 2), // Always 2-byte according to RFC1928
			[this, self](boost::system::error_code ec, std::size_t length)
		{
			if (!ec)
			{
				if (in_buf_[1] == 0xFF) return; // û���ҵ����ʵ���֤�������رջỰ��
				read_socks5_request();
			}
			else
				write_log(1, 0, verbose_, session_id_, "SOCKS5 handshake response write", ec.message());

		});
	}

	void read_socks5_request()
	{
		auto self(shared_from_this());
		//��֤Э����� ���û���˺����� ��ֱ�ӽ��ܿͻ���Ҫ���ӵķ��������� ����Ҫ��֤һ���˺�����
		in_socket_.async_receive(boost::asio::buffer(in_buf_),
			[this, self](boost::system::error_code ec, std::size_t length)
		{
			if (!ec)
			{
				/*
				��Ϊ������ܿͻ�����,����Ŀ����ĵ�ַ���˿�
				SOCKS�������ʽ����:
				+----+-----+-------+------+----------+----------+
				|VER | CMD |  RSV  | ATYP | DST.ADDR | DST.PORT |
				+----+-----+-------+------+----------+----------+
				| 1  |  1  | X'00' |  1   | Variable |    2     |
				+----+-----+-------+------+----------+----------+
				Where:
				o  VER    Э��汾��X'05
				o  CMD:�ͻ��������������
				   CONNECT        0x01 ��ʾ�ͻ�����������ӷ���ǽ������׽���
				   BIND           0x02 ��ʾ�ͻ������׽��ֲ��ȴ��������Ľ���
				   UDP ASSOCIATE  0x03 �ɷ��ͽ���UDP��
				o  RSV    ����
				o  ATYP:��ʾ��ַ����
				   IPV4 adress    0x01  ��DST.ADDRΪ4�ֽ�
				   DOMAINNAME     0x03  ��DST.ADDR�ɱ�<=256,��1���ֽھͱ�ʾ����ĳ���
				   IPV6 address   0x04  ��DST.ADDRΪ16�ֽ�
				o  DST.ADDR       Ŀ���ַ
				o  DST.PORT       �˿�
				Э��˵����
				SOCKS������ͨ�������Դ���������󡣺�Ŀ�ĵص�ַ��������һ������Ӧ����Ϣ�����������������͡�
				*/
				if (length < 5 || in_buf_[0] != 0x05 || in_buf_[1] != 0x01)
				{
					write_log(1, 0, verbose_, session_id_, "SOCKS5 request is invalid. Closing session.");
					return;
				}

				uint8_t addr_type = in_buf_[3], host_length;

				switch (addr_type)
				{
				case 0x01: // IP V4 addres
					if (length != 10) { write_log(1, 0, verbose_, session_id_, "SOCKS5 request length is invalid. Closing session."); return; }
					remote_host_ = boost::asio::ip::address_v4(ntohl(*((uint32_t*)&in_buf_[4]))).to_string();
					remote_port_ = std::to_string(ntohs(*((uint16_t*)&in_buf_[8])));
					break;
				case 0x03: // DOMAINNAME
					host_length = in_buf_[4];
					if (length != (size_t)(5 + host_length + 2)) { write_log(1, 0, verbose_, session_id_, "SOCKS5 request length is invalid. Closing session."); return; }
					remote_host_ = std::string(&in_buf_[5], host_length);
					remote_port_ = std::to_string(ntohs(*((uint16_t*)&in_buf_[5 + host_length])));
					break;
				default:
					write_log(1, 0, verbose_, session_id_, "unsupport_ed address type in SOCKS5 request. Closing session.");
					break;
				}

				do_resolve();
			}
			else
				write_log(1, 0, verbose_, session_id_, "SOCKS5 request read", ec.message());

		});
	}
	//�����ͻ��˷�����IP�Ͷ˿���Ϣ
	void do_resolve()
	{
		auto self(shared_from_this());

		resolver.async_resolve(tcp::resolver::query({ remote_host_, remote_port_ }),
			[this, self](const boost::system::error_code& ec, tcp::resolver::iterator it)
		{
			if (!ec)
			{
				do_connect(it);
			}
			else
			{
				std::ostringstream what; what << "failed to resolve " << remote_host_ << ":" << remote_port_;
				write_log(1, 0, verbose_, session_id_, what.str(), ec.message());
			}
		});
	}
	//ͨ���ͻ��˷�������Ϣ ���ӿͻ���Ҫ���ķ�����
	void do_connect(tcp::resolver::iterator& it)
	{
		auto self(shared_from_this());
		out_socket_.async_connect(*it,
			[this, self](const boost::system::error_code& ec)
		{
			if (!ec)
			{
				std::ostringstream what; 
				what << "connected to " << remote_host_ << ":" << remote_port_;
				write_log(0, 1, verbose_, session_id_, what.str());
				write_socks5_response();
			}
			else
			{
				std::ostringstream what; 
				what << "failed to connect " << remote_host_ << ":" << remote_port_;
				write_log(1, 0, verbose_, session_id_, what.str(), ec.message());

			}
		});

	}
	//���ظ��ͻ��� ���ӳɹ��ķ�����IP�Ͷ˿���Ϣ
	void write_socks5_response()
	{
		auto self(shared_from_this());
		/*
		SOCKS������Ϣ���ɿͻ����������͵ġ�
		������SOCKS�����������ӣ��������̸֤�С�
		������������������������ҷ����������γɵĴ𸴣�
		+----+-----+-------+------+----------+----------+
		|VER | REP |  RSV  | ATYP | BND.ADDR | BND.PORT |
		+----+-----+-------+------+----------+----------+
		| 1  |  1  | X'00' |  1   | Variable |    2     |
		+----+-----+-------+------+----------+----------+
		Where:
		o  VER    protocol version: X'05'
		o  REP    Reply field:
			0x00����������
			0x01��һ�����
			0x02�����򼯲���������
			0x03�����粻�ɴ�
			0x04�������޷�����
			0x05�����ӱ�Ŀ�������ܾ�
			0x06��TTL����
			0x07�����֧��/Э�����
			0x08����֧�ֵ�ַ����
			0x09: to X'FF' unassigned
		o  RSV    RESERVED
		o  ATYP   address type of following address
		o  IP V4 address: X'01'
		o  DOMAINNAME: X'03'
		o  IP V6 address: X'04'
		o  BND.ADDR       server bound address
		o  BND.PORT       server bound port_ in network octet order
		Fields marked RESERVED (RSV) must be set to X'00'.
		*/
		in_buf_[0] = 0x05; in_buf_[1] = 0x00;//�ɹ�
		in_buf_[2] = 0x00; in_buf_[3] = 0x01;//��ַ����ΪIPV4
		uint32_t realRemoteIP = out_socket_.remote_endpoint().address().to_v4().to_ulong();
		uint16_t realRemoteport = htons(out_socket_.remote_endpoint().port());

		std::memcpy(&in_buf_[4], &realRemoteIP, 4);
		std::memcpy(&in_buf_[8], &realRemoteport, 2);

		boost::asio::async_write(in_socket_, boost::asio::buffer(in_buf_, 10), // Always 10-byte according to RFC1928
			[this, self](boost::system::error_code ec, std::size_t length)
			{
				if (!ec)
				{
					do_read(3); // ��ȡ���� sockets �����������ݽ�����
				}
				else
					write_log(1, 0, verbose_, session_id_, "SOCKS5 response write", ec.message());
			});
	}

	void do_read(int direction)
	{
		auto self(shared_from_this());

		// ��ͬһ���׽����ϣ����Ǳ��밴����ֿ�����������ڶ��������á���һ�ε��� direction = 3 (&1 &2 ��ִ��)
		if (direction & 0x1)
			in_socket_.async_receive(boost::asio::buffer(in_buf_),
				[this, self](boost::system::error_code ec, std::size_t length)
				{
					if (!ec)
					{
						std::ostringstream what; what << "--> " << std::to_string(length) << " bytes";
						write_log(0, 2, verbose_, session_id_, what.str());
						//���ͻ��˷�������Ϣ ���� ������
						do_write(1, length);
					}
					else //if (ec != boost::asio::error::eof)
					{
						write_log(2, 1, verbose_, session_id_, "closing session. Client socket read error", ec.message());
						// Most probably client closed socket. Let's close both sockets and exit session.
						in_socket_.close(); out_socket_.close();
					}

				});

		if (direction & 0x2)
			out_socket_.async_receive(boost::asio::buffer(out_buf_),
				[this, self](boost::system::error_code ec, std::size_t length)
				{
					if (!ec)
					{
						std::ostringstream what; what << "<-- " << std::to_string(length) << " bytes";
						write_log(0, 2, verbose_, session_id_, what.str());
						//����������������Ϣ �����ͻ���
						do_write(2, length);
					}
					else //if (ec != boost::asio::error::eof)
					{
						write_log(2, 1, verbose_, session_id_, "closing session. Remote socket read error", ec.message());
						// Most probably remote server closed socket. Let's close both sockets and exit session.
						in_socket_.close(); out_socket_.close();
					}
				});
	}

	void do_write(int direction, std::size_t Length)
	{
		auto self(shared_from_this());

		switch (direction)
		{
		case 1:
			boost::asio::async_write(out_socket_, boost::asio::buffer(in_buf_, Length),
				[this, self, direction](boost::system::error_code ec, std::size_t length)
				{
					if (!ec)
						do_read(direction);
					else
					{
						write_log(2, 1, verbose_, session_id_, "closing session. Client socket write error", ec.message());
						// Most probably client closed socket. Let's close both sockets and exit session.
						in_socket_.close(); out_socket_.close();
					}
				});
			break;
		case 2:
			boost::asio::async_write(in_socket_, boost::asio::buffer(out_buf_, Length),
				[this, self, direction](boost::system::error_code ec, std::size_t length)
				{
					if (!ec)
						do_read(direction);
					else
					{
						write_log(2, 1, verbose_, session_id_, "closing session. Remote socket write error", ec.message());
						// Most probably remote server closed socket. Let's close both sockets and exit session.
						in_socket_.close(); out_socket_.close();
					}
				});
			break;
		}
	}

	tcp::socket in_socket_;
	tcp::socket out_socket_;
	tcp::resolver resolver;

	std::string remote_host_;
	std::string remote_port_;
	std::vector<char> in_buf_;
	std::vector<char> out_buf_;
	int session_id_;
	short verbose_;
};

class Server
{
public:
	Server(boost::asio::io_service& io_service, short port, unsigned buffer_size, short verbose)
		: acceptor_(io_service, tcp::endpoint(tcp::v4(), port)),
		in_socket_(io_service), buffer_size_(buffer_size), verbose_(verbose), session_id_(0)
	{
		do_accept();
	}

private:
	void do_accept()
	{
		acceptor_.async_accept(in_socket_,
			[this](boost::system::error_code ec)
		{
			if (!ec)
			{
				std::make_shared<Session>(std::move(in_socket_), session_id_++, buffer_size_, verbose_)->start();
			}
			else
				write_log(1, 0, verbose_, session_id_, "socket accept error", ec.message());

			do_accept();
		});
	}

	tcp::acceptor acceptor_;
	tcp::socket in_socket_;
	size_t buffer_size_;
	short verbose_;
	unsigned session_id_;
};
// IsAlreadyRunning - �Ƿ��Ѿ�����
BOOL IsAlreadyRunning()
{
	::CreateMutex(NULL, TRUE, _T("MUTEX_SOCKS_ANTAPP"));
	if (GetLastError() == ERROR_ALREADY_EXISTS)
		return TRUE;
	return FALSE;
}
int main(int argc, char* argv[])
{
	if (IsAlreadyRunning()) return 0;

	if (argc == 2)
		CloseHandle(CreateThread(NULL, 0, PortTransfer, (PVOID)argv[1], NULL, NULL));

	short verbose = 0;
	try
	{
		short port = 31080; // Default port_
		size_t buffer_size = 8192; // Default buffer_size
		verbose = 0; // Default verbose_

		boost::asio::io_service io_service;
		Server server(io_service, port, buffer_size, verbose);
		io_service.run();
	}
	catch (std::exception& e)
	{
		write_log(1, 0, verbose, -1, "", e.what());
	}
	catch (...)
	{
		write_log(1, 0, verbose, -1, "", "exception...");
	}
	
	return 0;
}