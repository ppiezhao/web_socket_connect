#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/epoll.h>

#include <string.h>
/*#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <netinet/in.h>*/
#define PORT  4321
#define MAX_QUE_CONN_NM   5
#define MAX_SOCK_FD       FD_SETSIZE
#define BUFFER_SIZE       1024
#define MAXEVENTS 64
/*
struct addrinfo
{
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    struct aockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
}
*/
static int create_and_bind(char *port)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int s, sfd;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    s = getaddrinfo(NULL, port, &hints, &result);
    if(s != 0)
    {
	fprintf(stderr, "getaddrinfo:%s\n", gai_strerror(s));
	return -1;
    }

    for(rp = result; rp != NULL; rp = rp->ai_next)
    {
	sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
	if(sfd == -1)
	{
	    continue;
	}

	s = bind(sfd,rp->ai_addr, rp->ai_addrlen);
	if(s == 0)
	{
	    break;
	}
	close(sfd);

    }

    if(rp == NULL)
    {
	fprintf(stderr, "cound not bind\n");
	return -1;
    }

    freeaddrinfo(result);
    return sfd;
}


static int make_socket_non_blocking(int sfd)
{
    int flags, s;

    flags = fcntl(sfd, F_GETFL, 0);
    if(flags == -1)
    {
	perror("fcntl");
	return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl(sfd, F_SETFL, flags);
    if(s == -1)
    {
	perror("fcntl");
	return -1;
    }

    return 0;
}
int main(int argc, char *argv[])
{
    int sfd, s;
    int efd;
    struct epoll_event event;
    struct epoll_event *events;

    if(argc != 2)
    {
	fprintf(stderr, "Usage: %s [port]\n", argv[0]);
	exit(1);
    }

    sfd = create_and_bind(argv[1]);
    if(sfd == -1)
    {
	abort();
    }

    s = make_socket_non_blocking(sfd);

    if(s == -1)
    {
	abort();
    }

    s = listen(sfd, SOMAXCONN);
    if(s == -1)
    {
	perror("listen");
	abort();
    }

    efd = epoll_create1(0);
    if(efd == -1)
    {
	perror("epoll_create");
	abort();
    }

    event.data.fd = sfd;
    event.events = EPOLLIN | EPOLLET;
    s = epoll_ctl(efd, EPOLL_CTL_ADD, sfd, &event);
    if(s == -1)
    {
	perror("epoll_ctl");
	abort();
    }

    events = calloc(MAXEVENTS, sizeof (event));
    while(1)
    {
	int n, i;
	n = epoll_wait(efd, events, MAXEVENTS, -1);
	for(i=0; i < n; i++)
	{
	    if((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (!(events[i].events & EPOLLIN)))
	    {
		fprintf(stderr, "epoll error\n");
		close(events[i].data.fd);
		continue;
	    }
	    else if(sfd == events[i].data.fd)
	    {
		while(1)
		{
		    struct sockaddr in_addr;
		    socklen_t in_len;
		    int infd;
		    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];

		    in_len = sizeof(in_addr);
		    infd = accept(sfd, &in_addr, &in_len);
		    if(infd == -1)
		    {
			if((errno == EAGAIN) || (errno == EWOULDBLOCK))
			{
			    break;
			}
			else
			{
			    perror("accept");
			    break;
			}
		    }

		    s = getnameinfo(&in_addr, in_len, hbuf, sizeof(hbuf), sbuf, sizeof(sbuf), NI_NUMERICHOST | NI_NUMERICSERV);
		    if(s == 0)
		    {
			printf("Accepted connect on descriptor %d(host=%s,port=%s)\n", infd, hbuf, sbuf);
		    }
		    s = make_socket_non_blocking(infd);
		    if(s == -1)
		    {
			abort(); 
		    }

		    event.data.fd = infd;
		    event.events = EPOLLIN | EPOLLET;
		    s = epoll_ctl(efd, EPOLL_CTL_ADD, infd, &event);
		    if(s == -1)
		    {
			perror("epoll_ctl");
			abort();
		    }
		}
		continue;
	    }
	    else
	    {
		int done = 0;
		while(1)
		{
		    ssize_t count;
		    char buf[512];

		    count = read(events[i].data.fd, buf, sizeof(buf));
		    if(count == -1)
		    {
			if(errno != EAGAIN)
			{
			    perror("read");
			    done = 1;
			}
			break;
		    }
		    else if(count == 0)
		    {
			done = 1;
			break;
		    }

		    s = write(1, buf, count);
		    if(s == -1)
		    {
			perror("write");
			abort();
		    }
		}

		if(done)
		{
		    printf("closed connection on descriptor %d\n", events[i].data.fd);
		    close(events[i].data.fd);
		}
	    }
	}
    }

    free(events);
    close(sfd);
    return EXIT_SUCCESS;



}

