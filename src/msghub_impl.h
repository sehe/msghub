#ifndef _MSGHUB_MSGHUB_IMPL_H_
#define _MSGHUB_MSGHUB_IMPL_H_

#include "hub.h"
#include "msghub.h"
#include "hubconnection.h"
#include "hubclient.h"
#include "hubmessage.h"

#include <memory>
#include <cstdlib>
#include <functional>
#include <algorithm>
#include <vector>
#include <map>
#include <boost/asio.hpp>
#include "charbuf.h"

using boost::asio::ip::tcp;

template <typename Executor> class msghub : 
    public hub,
    public std::enable_shared_from_this<msghub<Executor> >
{
  public:
    typedef std::function< void(std::string_view topic, const_charbuf message) > onmessage;
  private:
	std::map<std::string, onmessage> messagemap_;

  private:
	tcp::acceptor acceptor_;
    boost::asio::executor_work_guard<Executor> work_;
	std::shared_ptr<hubconnection> publisher_;

  public:

    msghub(Executor executor)
        : acceptor_(make_strand(executor))
        , work_(make_work_guard(executor))
    {}

    void stop()
    {
        if (publisher_)
            publisher_->close(false);

        publisher_.reset();

        if (acceptor_.is_open()) 
            acceptor_.cancel();

        work_.reset();
    }

    ~msghub()
    {
        stop();
    }

    bool connect(const std::string& hostip, uint16_t port)
    {
        auto p = std::make_shared<hubconnection>(acceptor_.get_executor(), *this);

        if (p->init(hostip, port)) {
            publisher_ = p;
        }

        return publisher_.get();
    }

    bool create(uint16_t port)
    {
        try {
            acceptor_.open(tcp::v4());
            acceptor_.set_option(tcp::acceptor::reuse_address(true));
            acceptor_.bind({{}, port});
            acceptor_.listen();

            accept_next();

            auto p = std::make_shared<hubconnection>(
                    acceptor_.get_executor(),
                    *this);

            if (p->init("localhost", port))
                publisher_ = p;

            return publisher_.get();
        } catch(boost::system::system_error const&) {
            return false;
        }
    }

    bool publish(std::string_view topic, const_charbuf message)
    {
        if (publisher_) {
            return publisher_->write({hubmessage::action::publish, topic, message});
        }
        return false;
    }

    // Treat string literals specially, not including the terminating NUL
    template <size_t N>
	bool publish(std::string_view topic, char const (&literal)[N]) {
        static_assert(N>0);
        return publish(topic, const_charbuf{literal, N-1});
    }

    bool unsubscribe(const std::string& topic)
    {
        if (publisher_ && messagemap_.find(topic) != messagemap_.end()) {
            return publisher_->write({hubmessage::action::unsubscribe, topic}, true);
        }
        return false;
    }

    bool subscribe(const std::string& topic, onmessage handler)
    {
        if (auto [it,ok] = messagemap_.emplace(topic, handler); !ok) {
            it->second = handler; // overwrite
        }

        if (publisher_) {
            return publisher_->write({hubmessage::action::subscribe, topic}, true);
        }

        // TODO: wait feedback from server here?
        return false;
    }

  private:
    void distribute(std::shared_ptr<hubclient> const& subscriber, hubmessage const& msg)
    {
        //post(acceptor_.get_executor(), do
        std::string topic(msg.topic());
        auto range = client_subs_.equal_range(topic);

        switch (msg.get_action())
        {
        case hubmessage::action::publish:
            for (auto it = range.first; it != range.second; ++it)
                it->second->write(msg);
            break;

        case hubmessage::action::subscribe:
            client_subs_.emplace(topic, subscriber);
            break;

        case hubmessage::action::unsubscribe:
            for (auto it = range.first; it != range.second;) {
                if (it->second == subscriber)
                    it = client_subs_.erase(it);
                else ++it;
            }
            break;

        default:
            break;
        }
    }

    void deliver(hubmessage const& msg)
    { 
        if (auto it = messagemap_.find(std::string(msg.topic()));
                it != messagemap_.end())
        {
            it->second(msg.topic(), msg.body());
        }
    }

    void accept_next()
    {
        auto subscriber = std::make_shared<hubclient>(acceptor_.get_executor(), *this);

        // Schedule next accept
        acceptor_.async_accept(subscriber->socket(),
            [=, this, self = this->shared_from_this()](boost::system::error_code ec) {
                handle_accept(subscriber, ec);
            });
    }

    void handle_accept(std::shared_ptr<hubclient> const& client, const boost::system::error_code& error)
    {
        if (!error)
        {
            client->start();
            accept_next();
        }
        else
        {
            //// TODO: Handle IO error - on thread exit
            //int e = error.value();
        }
    }

	std::multimap<std::string, std::shared_ptr<hubclient> > client_subs_;
};

#endif
