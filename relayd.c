#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>     /* definition of OPEN_MAX */
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>

#define max(x,y) (x>y ? x:y)
#define IP_ADDRESS "192.168.249.103"

int Local_port = 80; //Listen on this port.
char *Host = "10.0.1.102"; //Relay all traffic to this host
int Port = 80; //and to this remote port

int run = 1;

/*Become a background daemon process*/
void reassociate(char *tty_name)
{
int fd;
	if (fork())
		_exit(0);                       /* kill parent */
#ifdef __APPLE__
	setpgrp();
#else
	setpgrp(0,0);
#endif
	close(0);
#ifndef POSIX
#ifdef TIOCNOTTY
	if((fd = open( "/dev/tty", O_RDWR, 0)) >= 0)
	{	
		(void)ioctl(fd, TIOCNOTTY, (caddr_t)0);
		close(fd);
	}
#endif /*TIOCNOTTY*/
#else  /*POSIX*/
	(void) setsid();
#endif /*POSIX*/
#ifdef XXX
	close(1);
	close(2);
#endif
	if(tty_name)
		(void)open(tty_name, O_RDWR, 0);
	else
		(void)open("/dev/null",O_RDWR,0);
	(void)dup2(0,1);
	(void)dup2(0,2); 
}

/*Prepare to listen on given port*/
int open_listener(short port_number)
{
struct sockaddr_in record;
int s;
int  on = 1;

    if((s = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {   printf("socket creation failed %d\n",errno);
        exit(0);
    }
    else
    	printf("Socket opened %d\n", s);
    	
    if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(int)) == -1)
	    printf("setsockopt failed: ignoring this\n");

    record.sin_family = AF_INET;
    record.sin_addr.s_addr = inet_addr(IP_ADDRESS); //INADDR_ANY;
    record.sin_port = htons(port_number);
    if(bind(s, (struct sockaddr *)&record, (socklen_t) sizeof(record)) == -1)
    {   printf("bind failed %d\n",errno);
        exit(0);
    }
    if(listen(s,5) == -1)
    {   printf("listen failed %d\n",errno);
        exit(0);
    }
    return s;
}

/*Open connection to host we are relaying to*/
int open_remote_connection(char *hostname, short port_number)
{
int s;
int i, j;
struct sockaddr_in record;
register struct hostent *hp = gethostbyname(hostname);
int c;

    if (hp == NULL)
    {
  		printf("unknown host");
  		exit(0);
    }
    memcpy((char *) &record.sin_addr, hp->h_addr, hp->h_length);


    if((s = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    { printf("socket creation failed %d\n",errno);
	    exit(0);
    }

    record.sin_family = AF_INET;
    record.sin_port = htons(port_number);
    if(connect(s,(struct sockaddr *) &record, (socklen_t) sizeof(record)) == -1)
    {   printf("connection to port %d failed err %d\n",port_number,errno);
	      perror("connect");
	      exit(0);
    }
	
   return s;
}

/*what comes in on one socket is sent to the other socket*/
int relay(int in_sd, int out_sd)
{ // actually, in and out or totally interchangable.
char out_buffer[2048]; //This is bigger than an ethernet packet, so shouldn't fragment packets
char in_buffer[2048]; //This is bigger than an ethernet packet, so shouldn't fragment packets
int in_buffer_bytes = 0; 
int out_buffer_bytes = 0;
fd_set fdsRead;
fd_set fdsWrite;
struct timeval tv;
int nfds = max(in_sd,out_sd) + 1;
int nSelect = 0;
int in_eof = 0;
int out_eof = 0;

  tv.tv_sec = 60;
  tv.tv_usec = 0;
  
  for(;;)
  {
    if((in_eof || out_eof) && out_buffer_bytes == 0 && in_buffer_bytes == 0)
      return 0;
      
    //Start fresh, by clearing all FDs
    FD_ZERO(&fdsRead); 
    FD_ZERO(&fdsWrite);
    
    if(in_buffer_bytes <= 0)
    {
      if(in_eof == 0)
        FD_SET(in_sd , &fdsRead ); //We have no data to write, so check read stream.
    }
    else
      FD_SET(out_sd , &fdsWrite ); //We have data in our buffer, so check write stream.
      
    if(out_buffer_bytes <= 0)
    { if(out_eof == 0)
        FD_SET(out_sd , &fdsRead ); //We have no data to write, so check read stream.
    }
    else
      FD_SET(in_sd , &fdsWrite ); //We have data in our buffer, so check write stream.
    
      
    if( ( nSelect = select( nfds, &fdsRead, &fdsWrite, NULL, &tv) ) < 0 ) 
    {
      if( errno == EINTR )
        continue; //An interrupt is non-fatal.
      else
        return nSelect; //consider this a terminal offense.
    }
    else if(nSelect == 0)
    {
      return -1;//timed out.
    }
    
    //Data in the system inward streams read buffer.
    if( FD_ISSET( in_sd, &fdsRead ) )
    {
      while( (in_buffer_bytes = read(in_sd, in_buffer, sizeof(in_buffer)) ) == -1)
      {
        if(errno == EINTR)
          continue;
        else
          return errno;
      }
      if(in_buffer_bytes == 0)
        in_eof = 1;
        
      //printf("Read %d from inbuffer\n", in_buffer_bytes);
    }
    
    //Data in the system outward streams read buffer.
    if( FD_ISSET( out_sd, &fdsRead ) )
    {
      while( (out_buffer_bytes = read(out_sd, out_buffer, sizeof(out_buffer)) ) == -1)
      {
        if(errno == EINTR)
          continue;
        else
          return errno;
      }
      if(out_buffer_bytes == 0)
        out_eof = 1;
      //printf("Read %d from outbuffer\n", out_buffer_bytes);
    }
    
    //Inward Write stream available, and we have something to write.
    if( FD_ISSET( in_sd, &fdsWrite)  && out_buffer_bytes > 0)
    {
      int bytes_written;
      while( (bytes_written = write(in_sd, out_buffer, out_buffer_bytes) ) == -1) 
      { if(errno == EINTR)
          continue;
        else
          return errno;
      }        
      out_buffer_bytes -= bytes_written;
      //printf("Wrote %d from outbuffer, to in_sd\n", bytes_written);
    }
    
    //Outward Write stream available, and we have something to write.
    if( FD_ISSET( out_sd, &fdsWrite) && in_buffer_bytes > 0)
    {
      int bytes_written;
      while( (bytes_written = write(out_sd, in_buffer, in_buffer_bytes) ) == -1) 
      { if(errno == EINTR)
          continue;
        else
          return errno;
      }        
      in_buffer_bytes -= bytes_written;
      //printf("Wrote %d from inbuffer, to out_sd\n", bytes_written);
    }
  }
  
}

/*connect to remote host and relay packets until incoming connection closes*/
void process_connection(int sd)
{
  int c_sd;
  
  if((c_sd = open_remote_connection(Host, Port)) == -1)
  {
    exit(-1);
  }
  relay(sd, c_sd);
  shutdown(c_sd,0);
  close(c_sd);
}

/*Be a good citizen and don't leave zombie children wandering the system*/
static void catch_children( int sig)
{
int status;
int ThePid;

    /*SIGCHLD signals are blocked  by the system when entering this function*/
    while((ThePid = wait3(&status,WNOHANG,0)) != 0 && ThePid != -1);
    /*SIGCHLD signals are unblocked  by the system when leaving this function*/
}

/*Next check in loop will result in an exit*/
static void catch_term( int sig)
{
  run = 0;
}

/*listen for a connection, fork, connect to remote host, and run packet copy*/
int main(int argc, char **argv, char **envp)
{
int listen_sd;
int sd;
int result = 0;
struct sockaddr_in address;
socklen_t addr_len;

	reassociate(NULL);

	signal(SIGPIPE, SIG_IGN); //ignore broken connections
	signal(SIGCHLD, catch_children); //clean up after the kids.
	signal(SIGTERM, catch_term); //catch a terminate signal, so we can close the socket properly.
	
	
  listen_sd = open_listener(Local_port);
  for(;run == 1;)
  {
    addr_len = sizeof(address);
		
   	if((sd = accept(listen_sd, (struct sockaddr *)&address, &addr_len)) == -1)
   	{   	//perror("accept failed");
		if(run == 0) exit(0);
       		continue;
   	}

		if((result = fork()) == -1)
		{ //Parent had an error forking, so we go back to accepting connections
			close(sd);
			if(run == 0) exit(0);
			continue;
		} 
		else if(result == 0)
		{ //Child, so we are now in another process.
			close(listen_sd); /*close the listening socket in the child process*/
  	  		signal(SIGCHLD, SIG_DFL); //Don't have children to worry about.
			signal(SIGPIPE, SIG_DFL); //let broken pipes terminate the process.
    			signal(SIGTERM, SIG_DFL); //Don't need to worry about clean exit.
      			process_connection(sd);
      			shutdown(sd,0);
      			close(sd);
			exit(0);	
		}
		else //Parent. so close the accept discriptor, and go back to accepting
			close(sd);
  }
    
  shutdown(listen_sd,0);
  close(listen_sd);
}
