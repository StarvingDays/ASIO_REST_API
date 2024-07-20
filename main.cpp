#include <iostream>
#include "mini_server_co.hpp"
#include <ctime>

int main()
{
	
	//PacketStatus status = PacketStatus::WRITE;
	//auto type = std::underlying_type<PacketStatus>::type(status);

	try
	{
		mini_server server("192.168.0.20", "2005", "C:/Users/Administrator/source/repos/ASIO/HTTP_Server");

		server.Run();
	}
	catch (std::exception& e)
	{
		std::cerr << "exception: " << e.what() << "\n";
	}

	return 0;
}

