#include "msghub.h"

#include <mutex>
#include <condition_variable> 

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/test/unit_test_monitor.hpp>
#include <boost/test/test_tools.hpp>

namespace {
    using namespace std::chrono_literals;

    std::mutex mx;
    bool received = false;
    bool goodmessage = false;
    std::condition_variable newmessage;

    void test_create_on_message(const std::string& topic, std::vector<char> const& message)
    {
        std::unique_lock<std::mutex> lock(mx);
        std::vector<char> expected{'$','t','e','s','t','m','e','s','s','a','g','e','$'};
        received = true;
        goodmessage = (expected == message);
        BOOST_CHECK_EQUAL("test_topic", topic);
        BOOST_TEST(expected == message, boost::test_tools::per_element());
        newmessage.notify_one();
    }
}

void test_subscribe()
{
	boost::asio::thread_pool io(1);

    msghub hub(io.get_executor());
    BOOST_CHECK(hub.create(0xBEE));

    BOOST_CHECK(hub.subscribe("test_topic", test_create_on_message));
    BOOST_CHECK(hub.publish("test_topic", "$testmessage$"));

    {
        std::unique_lock<std::mutex> lock(mx);
        BOOST_CHECK(newmessage.wait_for(lock, 1s, [] { return received; }));
    }

    hub.stop();
    io.join();

    BOOST_CHECK(goodmessage);
}
