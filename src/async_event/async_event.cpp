﻿/**
 *  @file   async_select.cpp
 *  @author ichenq@gmail.com
 *  @date   Oct 19, 2011
 *  @brief  a simple echo server implemented by WSAEventSelect()
 *			
 */

#include "../common/utility.h"
#include <stdio.h>
#include <map>


namespace {

    // event handle for each socket
    std::map<SOCKET, WSAEVENT>  g_event_list;

    // event handle as key
    std::map<WSAEVENT, SOCKET>  g_socket_list;
}

int make_event_array(WSAEVENT* array, int max_count)
{
    int count = 0;
    for (std::map<SOCKET, WSAEVENT>::const_iterator iter = g_event_list.begin();
        iter != g_event_list.end() && max_count-- > 0; ++iter)
    {
        array[count++] = iter->second;
    }
    return count;
}

// Create acceptor
SOCKET create_listen_socket(const char* host, int port)
{
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(host);
    addr.sin_port = htons((short)port);

    SOCKET sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd == INVALID_SOCKET)
    {
        fprintf(stderr, ("socket() failed, %s"), LAST_ERROR_MSG);
        return INVALID_SOCKET;
    }

    if (bind(sockfd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        fprintf(stderr, ("bind() failed, %s"), LAST_ERROR_MSG);
        closesocket(sockfd);
        return INVALID_SOCKET;
    }

    if (listen(sockfd, SOMAXCONN) == SOCKET_ERROR)
    {
        fprintf(stderr, ("listen() failed, %s"), LAST_ERROR_MSG);
        closesocket(sockfd);
        return INVALID_SOCKET;
    }

    // set to non-blocking
    ULONG nonblock = 1;
    if (ioctlsocket(sockfd, FIONBIO, &nonblock) == SOCKET_ERROR)
    {
        fprintf(stderr, ("ioctlsocket() failed, %s"), LAST_ERROR_MSG);
        closesocket(sockfd);
        return INVALID_SOCKET;
    }
    fprintf(stdout, ("server start listen [%s:%d] at %s.\n"), host, port, Now().data());

    return sockfd;
}

bool on_close(SOCKET sockfd, int error)
{
    WSAEVENT hEvent = g_event_list[sockfd];
    WSAEventSelect(sockfd, NULL, 0); 
    WSACloseEvent(hEvent);
    closesocket(sockfd);

    g_event_list.erase(sockfd);
    g_socket_list.erase(hEvent);

    fprintf(stderr, ("socket %d closed at %s.\n"), sockfd, Now().data());
    return true;
}


bool on_recv(SOCKET sockfd, int error)
{
    char databuf[kDefaultBufferSize];
    int bytes = recv(sockfd, databuf, kDefaultBufferSize, 0);
    if (bytes == SOCKET_ERROR && bytes == 0)
    {
        return on_close(sockfd, 0);
    }

    // send back
    bytes = send(sockfd, databuf, bytes, 0);
    if (bytes == 0)
    {
        return on_close(sockfd, 0);
    }

    return true;
}

bool on_write(SOCKET sockfd, int error)
{    
    return true;
}

// New connection arrival
bool on_accept(SOCKET sockfd)
{
    WSAEVENT hEvent = WSACreateEvent();
    if (hEvent == WSA_INVALID_EVENT)
    {
        fprintf(stderr, ("WSACreateEvent() failed, %s"), LAST_ERROR_MSG);
        return false;
    }

    // Associate event handle
    if (WSAEventSelect(sockfd, hEvent, FD_READ | FD_WRITE | FD_CLOSE) == SOCKET_ERROR)
    {
        WSACloseEvent(hEvent);
        fprintf(stderr, ("WSAEventSelect() failed, %s"), LAST_ERROR_MSG);
        return false;
    }

    if (g_event_list.size() == WSA_MAXIMUM_WAIT_EVENTS)
    {
        WSAEventSelect(sockfd, hEvent, 0);
        WSACloseEvent(hEvent);
        fprintf(stderr, "Got 64 limit.\n");
        return false;
    }
    
    g_event_list[sockfd] = hEvent;
    g_socket_list[hEvent] = sockfd;

    fprintf(stdout, ("socket %d connected at %s.\n"), sockfd, Now().data());
    return true;
}


int  handle_event(SOCKET sockfd, const WSANETWORKEVENTS* events_struct)
{
    const int* errorlist = events_struct->iErrorCode;
    int events = events_struct->lNetworkEvents;
    if (events & FD_READ)
    {
        on_recv(sockfd, errorlist[FD_READ_BIT]);
    }
    if (events & FD_WRITE)
    {
        on_write(sockfd, errorlist[FD_WRITE_BIT]);
    }
    if (events & FD_CLOSE)
    {
        on_close(sockfd, errorlist[FD_CLOSE_BIT]);
    }
    return 1;
}


bool event_loop()
{
    if (g_event_list.empty())
    {
        ::Sleep(100);
        return true;
    }

    WSAEVENT eventlist[WSA_MAXIMUM_WAIT_EVENTS] = {}; 
    int count = make_event_array(eventlist, WSA_MAXIMUM_WAIT_EVENTS);
    
    size_t nready = WSAWaitForMultipleEvents(count, eventlist, FALSE, 100, FALSE);            
    if (nready == WSA_WAIT_FAILED)
    {
        fprintf(stderr, ("WSAWaitForMultipleEvents() failed, %s"), LAST_ERROR_MSG);
        return false;
    }
    else if (nready == WSA_WAIT_TIMEOUT)
    {
    }
    else if (nready == WSA_WAIT_IO_COMPLETION)
    {
    }
    else
    {
        size_t index = WSA_WAIT_EVENT_0 + nready;
        if (index >= WSA_MAXIMUM_WAIT_EVENTS)
        {
            fprintf(stderr, "invalid event index: %d.\n", index);
            return true;
        }

        WSAEVENT hEvent = eventlist[index];            
        std::map<WSAEVENT, SOCKET>::const_iterator iter = g_socket_list.find(hEvent);
        if (iter == g_socket_list.end())
        {
            fprintf(stderr, "invalid event object %p.\n", &hEvent);
            return true;
        }
        SOCKET sockfd = iter->second;

        WSANETWORKEVENTS event_struct = {};
        if (WSAEnumNetworkEvents(sockfd, hEvent, &event_struct) == SOCKET_ERROR)
        {
            fprintf(stderr, ("WSAEnumNetworkEvents() failed, %s"), LAST_ERROR_MSG);
            on_close(sockfd, 0);
            return true;
        }

        handle_event(sockfd, &event_struct);
    }
    return true;
}

// main entry
int main(int argc, const char* argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, ("Usage: AsyncEvent [host] [port]\n"));
        return 1;
    }

    WinsockInit init;
    SOCKET sockfd = create_listen_socket(argv[1], atoi(argv[2]));
    if (sockfd == INVALID_SOCKET)
    {
        return 1;
    }

    for (;;)
    {
        SOCKET socknew = accept(sockfd, NULL, NULL);
        if (socknew != INVALID_SOCKET)
        {
            if (!on_accept(socknew))
            {
                closesocket(socknew);
            }
        }
        else
        {
            if (GetLastError() == WSAEWOULDBLOCK)
            {
                if (event_loop())
                {
                    continue;
                }
            }
            fprintf(stderr, ("accept() failed, %s"), LAST_ERROR_MSG);
            break;
        }
    }

    return 0;
}

