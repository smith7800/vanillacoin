/*
 * Copyright (c) 2013-2014 John Connor (BM-NC49AxAjcqVcF5jNPu85Rb8MJ2d9JqZt)
 *
 * This file is part of coinpp.
 *
 * coinpp is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License with
 * additional permissions to the one published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version. For more information see LICENSE.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cassert>

#include <coin/address_manager.hpp>
#include <coin/configuration.hpp>
#include <coin/globals.hpp>
#include <coin/logger.hpp>
#include <coin/message.hpp>
#include <coin/network.hpp>
#include <coin/stack_impl.hpp>
#include <coin/status_manager.hpp>
#include <coin/tcp_connection.hpp>
#include <coin/tcp_connection_manager.hpp>
#include <coin/tcp_transport.hpp>
#include <coin/time.hpp>
#include <coin/utility.hpp>

using namespace coin;

tcp_connection_manager::tcp_connection_manager(
    boost::asio::io_service & ios, stack_impl & owner
    )
    : io_service_(ios)
    , resolver_(ios)
    , strand_(ios)
    , stack_impl_(owner)
    , timer_(ios)
{
    // ...
}

void tcp_connection_manager::start()
{
    std::vector<boost::asio::ip::tcp::resolver::query> queries;
    
    /**
     * Get the bootstrap nodes and ready them for DNS lookup.
     */
    auto bootstrap_nodes = stack_impl_.get_configuration().bootstrap_nodes();
    
    for (auto & i : bootstrap_nodes)
    {
        boost::asio::ip::tcp::resolver::query q(
            i.first, std::to_string(i.second)
        );
        
        queries.push_back(q);
    }

    /**
     * Randomize the host names.
     */
    std::random_shuffle(queries.begin(), queries.end());
    
    /**
     * Start host name resolution.
     */
    do_resolve(queries);

    /**
     * Start the timer.
     */
    auto self(shared_from_this());
    
    timer_.expires_from_now(std::chrono::seconds(1));
    timer_.async_wait(globals::instance().strand().wrap(
        std::bind(&tcp_connection_manager::tick, self,
        std::placeholders::_1))
    );
}

void tcp_connection_manager::stop()
{
    resolver_.cancel();
    timer_.cancel();
    
    std::lock_guard<std::recursive_mutex> l1(mutex_tcp_connections_);
    
    for (auto & i : m_tcp_connections)
    {
        if (auto connection = i.second.lock())
        {
            connection->stop();
        }
    }
    
    m_tcp_connections.clear();
}

void tcp_connection_manager::handle_accept(
    std::shared_ptr<tcp_transport> transport
    )
{
    std::lock_guard<std::recursive_mutex> l1(mutex_tcp_connections_);
    
    /**
     * We only allow one incoming connection per unique IP address.
     */
    bool is_duplicate = false;
    
    for (auto & i : m_tcp_connections)
    {
        try
        {
            if (
                i.first.address() ==
                transport->socket().remote_endpoint().address()
                )
            {
                is_duplicate = true;
                
                break;
            }
        }
        catch (...)
        {
            // ...
        }
    }
    
    if (is_duplicate)
    {
        log_error(
            "TCP connection manager is dropping duplicate IP connection "
            "from " << transport->socket().remote_endpoint() << "."
        );
        
        /**
         * Stop the transport.
         */
        transport->stop();
    }
    else if (
        network::instance().is_address_banned(
        transport->socket().remote_endpoint().address().to_string())
        )
    {
        log_info(
            "TCP connection manager is dropping banned connection from " <<
            transport->socket().remote_endpoint() << ", limit reached."
        );
        
        /**
         * Stop the transport.
         */
        transport->stop();
    }
    else if (
        m_tcp_connections.size() >=
        stack_impl_.get_configuration().network_tcp_inbound_maximum()
        )
    {
        log_error(
            "TCP connection manager is dropping connection from " <<
            transport->socket().remote_endpoint() << ", limit reached."
        );
        
        /**
         * Stop the transport.
         */
        transport->stop();
    }
    else
    {
        log_debug(
            "TCP connection manager accepted new tcp connection from " <<
            transport->socket().remote_endpoint() << ", " <<
            m_tcp_connections.size() << " connected peers."
        );

        /**
         * Allocate the tcp_connection.
         */
        auto connection = std::make_shared<tcp_connection> (
            io_service_, stack_impl_, tcp_connection::direction_incoming,
            transport
        );

        /**
         * Retain the connection.
         */
        m_tcp_connections[transport->socket().remote_endpoint()] = connection;
        
        /**
         * Start the tcp_connection.
         */
        connection->start();
    }
}

void tcp_connection_manager::broadcast(
    const char * buf, const std::size_t & len
    )
{
    std::lock_guard<std::recursive_mutex> l1(mutex_tcp_connections_);
    
    for (auto & i : m_tcp_connections)
    {
        if (auto j = i.second.lock())
        {
            j->send(buf, len);
        }
    }
}

std::map< boost::asio::ip::tcp::endpoint, std::weak_ptr<tcp_connection> > &
    tcp_connection_manager::tcp_connections()
{
    std::lock_guard<std::recursive_mutex> l1(mutex_tcp_connections_);
    
    return m_tcp_connections;
}

bool tcp_connection_manager::connect(const boost::asio::ip::tcp::endpoint & ep)
{
    std::lock_guard<std::recursive_mutex> l1(mutex_tcp_connections_);
    
    if (network::instance().is_address_banned(ep.address().to_string()))
    {
        log_info(
            "TCP connection manager tried to connect to a banned address " <<
            ep << "."
        );
        
        return false;
    }
    else if (m_tcp_connections.find(ep) == m_tcp_connections.end())
    {
        /**
         * Inform the address_manager.
         */
        stack_impl_.get_address_manager()->on_connection_attempt(
            protocol::network_address_t::from_endpoint(ep)
        );
        
        /**
         * Allocate tcp_transport.
         */
        auto transport = std::make_shared<tcp_transport>(io_service_, strand_);
        
        /**
         * Allocate the tcp_connection.
         */
        auto connection = std::make_shared<tcp_connection> (
            io_service_, stack_impl_, tcp_connection::direction_outgoing,
            transport
        );
        
        /**
         * Retain the connection.
         */
        m_tcp_connections[ep] = connection;
        
        /**
         * Start the tcp_connection.
         */
        connection->start(ep);
        
        return true;
    }
    else
    {
        // ...
    }
    
    return false;
}

void tcp_connection_manager::tick(const boost::system::error_code & ec)
{
    if (ec)
    {
        // ...
    }
    else
    {
        std::lock_guard<std::recursive_mutex> l1(mutex_tcp_connections_);
        
        auto it = m_tcp_connections.begin();
        
        while (it != m_tcp_connections.end())
        {
            if (auto connection = it->second.lock())
            {
                if (connection->is_transport_valid())
                {
                    ++it;
                }
                else
                {
                    connection->stop();
                    
                    it = m_tcp_connections.erase(it);
                }
            }
            else
            {
                it = m_tcp_connections.erase(it);
            }
        }
        
        /**
         * Maintain at least minimum_tcp_connections tcp connections.
         */
        if (m_tcp_connections.size() < minimum_tcp_connections + 1)
        {
            for (
                auto i = 0; i <
                minimum_tcp_connections - m_tcp_connections.size(); i++
                )
            {
                /**
                 * Get a network address from the address_manager.
                 */
                auto addr = stack_impl_.get_address_manager()->select(
                    10 + std::min(m_tcp_connections.size(),
                    static_cast<std::size_t> (8)) * 10
                );
            
                /**
                 * Only connect to one peer per group.
                 */
                bool is_in_same_group = false;

                for (auto & i : m_tcp_connections)
                {
                    if (auto j = i.second.lock())
                    {
                        if (auto k = j->get_tcp_transport().lock())
                        {
                            try
                            {
                                auto addr_tmp =
                                    protocol::network_address_t::from_endpoint(
                                    k->socket().remote_endpoint()
                                );
                                
                                if (addr.group() == addr_tmp.group())
                                {
                                    is_in_same_group = true;
                                }
                            }
                            catch (std::exception & e)
                            {
                                // ...
                            }
                        }
                    }
                }

                if (
                    addr.is_valid() == false || addr.is_local() ||
                    is_in_same_group
                    )
                {
                    // ...
                }
                else
                {
                    /**
                     * Do not retry connections to the same network address more
                     * often than every 10 minutes.
                     */
                    if (time::instance().get_adjusted() - addr.last_try < 600)
                    {
                        log_debug(
                            "TCP connection manager attempted to "
                            "connect to " << addr.ipv4_mapped_address() <<
                            ":" << addr.port << " too soon, last try = " <<
                            (time::instance().get_adjusted() - addr.last_try) <<
                            " seconds."
                        );
                    }
                    else
                    {
                        /**
                         * Connect to the endpoint.
                         */
                        if (connect(
                            boost::asio::ip::tcp::endpoint(
                            addr.ipv4_mapped_address(), addr.port))
                            )
                        {
                            log_debug(
                                "TCP connection manager is connecting to " <<
                                addr.ipv4_mapped_address() << ":" << addr.port <<
                                ", last seen = " <<
                                (time::instance().get_adjusted() -
                                addr.timestamp) / 60 << " mins, " <<
                                m_tcp_connections.size() << " connected peers."
                            );
                        }
                    }
                }
            }
            
            auto self(shared_from_this());
            
            timer_.expires_from_now(std::chrono::seconds(8));
            timer_.async_wait(globals::instance().strand().wrap(
                std::bind(&tcp_connection_manager::tick, self,
                std::placeholders::_1))
            );
        }
        else
        {
            auto self(shared_from_this());
            
            timer_.expires_from_now(std::chrono::seconds(8));
            timer_.async_wait(globals::instance().strand().wrap(
                std::bind(&tcp_connection_manager::tick, self,
                std::placeholders::_1))
            );
        }

        /**
         * Allocate the status.
         */
        std::map<std::string, std::string> status;
        
        /**
         * Set the status message.
         */
        status["type"] = "network";
        status["value"] =
            m_tcp_connections.size() > 0 ? "Connected" : "Connecting"
        ;
        status["network.tcp.connections"] = std::to_string(
            m_tcp_connections.size()
        );
        
        /**
         * Callback status.
         */
        stack_impl_.get_status_manager()->insert(status);
    }
}

void tcp_connection_manager::do_resolve(
    const std::vector<boost::asio::ip::tcp::resolver::query> & queries
    )
{
    /**
     * Sanity check.
     */
    assert(queries.size() <= 100);
    
    /**
     * Resolve the first entry.
     */
    resolver_.async_resolve(queries.front(),
        strand_.wrap([this, queries](
            const boost::system::error_code & ec,
            const boost::asio::ip::tcp::resolver::iterator & it
            )
            {
                if (ec)
                {
                    // ...
                }
                else
                {
                    log_debug(
                        "TCP connection manager resolved " << it->endpoint() <<
                        "."
                    );
                    
                    /**
                     * Create the network address.
                     */
                    protocol::network_address_t addr =
                        protocol::network_address_t::from_endpoint(
                        it->endpoint()
                    );
                    
                    /**
                     * Add to the address manager.
                     */
                    stack_impl_.get_address_manager()->add(
                        addr, protocol::network_address_t::from_endpoint(
                        boost::asio::ip::tcp::endpoint(
                        boost::asio::ip::address::from_string("127.0.0.1"), 0))
                    );
                }
                
                if (queries.size() > 0)
                {
                    auto tmp = queries;
                    
                    /**
                     * Remove the first entry.
                     */
                    tmp.erase(tmp.begin());
                    
                    if (tmp.size() > 0)
                    {
                        /**
                         * Keep resolving as long as we have entries.
                         */
                        do_resolve(tmp);
                    }
                }
            }
        )
    );
}
