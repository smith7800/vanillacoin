
/*
 * Copyright (c) 2008-2014 John Connor (BM-NC49AxAjcqVcF5jNPu85Rb8MJ2d9JqZt)
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

#include <stdexcept>

#include <database/stack.hpp>
#include <database/stack_impl.hpp>

#include <database/tcp_acceptor.hpp>
#include <database/tcp_transport.hpp>

using namespace database;

stack::stack()
    : stack_impl_(0)
{
    // ...
}

void stack::start(const configuration & config)
{
    if (stack_impl_)
    {
        throw std::runtime_error("Stack is already allocated");
    }
    else
    {
        /**
         * Allocate the stack implementation.
         */
        stack_impl_ = new stack_impl(*this);
        
        /**
         * Start the stack implementation.
         */
        stack_impl_->start(config);
    }
}

void stack::stop()
{
    if (stack_impl_)
    {
        /**
         * Stop the stack implementation.
         */
        stack_impl_->stop();
        
        /**
         * Deallocate the stack implementation.
         */
        delete stack_impl_, stack_impl_ = 0;
    }
    else
    {
        throw std::runtime_error("Stack is not allocated");
    }
}

void stack::join(
    const std::vector< std::pair<std::string, unsigned short> > & contacts
    )
{
    if (stack_impl_)
    {
        stack_impl_->join(contacts);
    }
}

void stack::leave()
{
    if (stack_impl_)
    {
        stack_impl_->leave();
    }
}

std::uint16_t stack::store(const std::string & query)
{
    if (stack_impl_)
    {
        return stack_impl_->store(query);
    }
    
    return 0;
}

std::uint16_t stack::find(
    const std::string & query, const std::size_t & max_results
    )
{
    if (stack_impl_)
    {
        return stack_impl_->find(query, max_results);
    }
    
    return 0;
}

std::uint16_t stack::proxy(
    const char * addr, const std::uint16_t & port, const char * buf,
    const std::size_t & len
    )
{
    if (stack_impl_)
    {
        return stack_impl_->proxy(addr, port, buf, len);
    }
    
    return 0;
}

std::list< std::pair<std::string, std::uint16_t> > stack::endpoints()
{
    if (stack_impl_)
    {
        return stack_impl_->endpoints();
    }
    
    return std::list< std::pair<std::string, std::uint16_t> > ();
}

void stack::on_connected(const char * addr, const std::uint16_t & port)
{
    printf("%s is not overloaded.\n", __FUNCTION__);
}

void stack::on_disconnected(const char * addr, const std::uint16_t & port)
{
    printf("%s is not overloaded.\n", __FUNCTION__);
}

void stack::on_find(
    const std::uint16_t & transaction_id, const std::string & query
    )
{
    printf("%s is not overloaded.\n", __FUNCTION__);
}

void stack::on_proxy(
    const std::uint16_t & tid, const char * addr, const std::uint16_t & port,
    const std::string & value
    )
{
    printf("%s is not overloaded.\n", __FUNCTION__);
}

void stack::on_udp_receive(
    const char * addr, const std::uint16_t & port, const char * buf,
    const std::size_t & len
    )
{
    printf("%s is not overloaded.\n", __FUNCTION__);
}
