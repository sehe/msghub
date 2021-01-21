#include "msghub.h"

#include <algorithm>
#include <string>

#include <boost/asio.hpp>
#include <boost/test/test_tools.hpp>

void test_create()
{
	// Create 2 instances with same address and port, second shoul fail
	{
		boost::asio::io_context io;
		//boost::asio::io_service io_service2;
		
		msghub msghub1(io.get_executor());
		BOOST_CHECK(msghub1.create(0xBEE));
		
		msghub msghub2(io.get_executor());
		
		// Fail as port is in use by previous instance (-SO_REUSEPORT, issue on Windows)
		//BOOST_CHECK(!msghub2.create(0xBEE));

		BOOST_CHECK(msghub2.create(0xB0B));
		//io.run();
		//io_service2.run();
		io.stop();
		//io_service2.stop();
	}

}
