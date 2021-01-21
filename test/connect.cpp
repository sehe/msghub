#include "msghub.h"

#include <algorithm>
#include <string>

#include <boost/asio.hpp>
#include <boost/test/test_tools.hpp>

void test_connect()
{
    boost::asio::io_context io;

    msghub msghub1(io.get_executor());
    BOOST_CHECK(msghub1.create(0xBEE));

    BOOST_CHECK(msghub1.connect("localhost", 0xBEE));
    io.stop();
}
