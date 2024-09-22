#include "chatServer.h"
#include <signal.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <netinet/in.h>
#include<stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#define MAX_PORT_SIZE 65535
static int end_server = 0;
int readFromClientSocket(conn_pool_t*,int);
void updateMaxFd(conn_pool_t*,int);
void deleteConnMessages(conn_t*);
void toUpperCase(int,char[]);
void intHandler(int SIG_INT)
{
    /* use a flag to end_server to break the main loop */
    end_server=1;
}
int main (int argc, char *argv[])
{
    if(argc!=2)
    {
        printf("Usage: server <port>");
        exit(EXIT_FAILURE);
    }
    long long port;
    port= atoi(argv[1]);
    if(port<1 || port>MAX_PORT_SIZE)
    {
        printf("Usage: server <port>");
        exit(EXIT_FAILURE);
    }
    signal(SIGINT, intHandler);
    conn_pool_t* pool = malloc(sizeof(conn_pool_t));
    initPool(pool);
    /*************************************************************/
    /* Create an AF_INET stream socket to receive incoming      */
    /* connections on                                            */
    /*************************************************************/
    int welcome_socket=socket(AF_INET,SOCK_STREAM,0);
    if(welcome_socket<0)
    {
        free(pool);
        perror("error: socket\n");
        exit(EXIT_FAILURE);
    }
    /*************************************************************/
    /* Set socket to be nonblocking. All of the sockets for      */
    /* the incoming connections will also be nonblocking since   */
    /* they will inherit that state from the listening socket.   */
    /*************************************************************/
    int on=1;
    int rc=ioctl(welcome_socket,(int)FIONBIO,(char*)&on);
    if(rc==-1)
    {
        free(pool);
        close(welcome_socket);
        perror("error: ioctl\n");
        exit(EXIT_FAILURE);
    }
    /*************************************************************/
    /* Bind the socket                                           */
    /*************************************************************/
    struct sockaddr_in server;
    server.sin_addr.s_addr= htonl(INADDR_ANY);
    server.sin_port= htons((in_port_t)port);
    server.sin_family=AF_INET;
    if(bind(welcome_socket, (struct sockaddr*)&server, sizeof(server))<0)
    {
        free(pool);
        close(welcome_socket);
        perror("error: bind\n");
        exit(EXIT_FAILURE);
    }
    /*************************************************************/
    /* Set the listen back log                                   */
    /*************************************************************/
    int listenRes=listen(welcome_socket, 5);
    if(listenRes<0)
    {
        close(welcome_socket);
        free(pool);
        perror("error: listen\n");
        exit(EXIT_FAILURE);
    }
    /*************************************************************/
    /* Initialize fd_sets  			                             */
    /*************************************************************/
    FD_SET(welcome_socket,&pool->ready_read_set);// the Master read set.
    pool->maxfd=welcome_socket;
    /*************************************************************/
    /* Loop waiting for incoming connects, for incoming data or  */
    /* to write data, on any of the connected sockets.           */
    /*************************************************************/
//    struct timeval timeout;
//    timeout.tv_sec=5;
//    timeout.tv_usec=0;
    do
    {
        /**********************************************************/
        /* Copy the master fd_set over to the working fd_set.     */
        /**********************************************************/
        pool->read_set=pool->ready_read_set;
        pool->write_set=pool->ready_write_set;
        /**********************************************************/
        /* Call select() 										  */
        /**********************************************************/
        printf("Waiting on select()...\nMaxFd %d\n", pool->maxfd);
        pool->nready=select(pool->maxfd+1,&pool->read_set,&pool->write_set,NULL,NULL);
        if(pool->nready<0)
        {
            continue;
        }
        /**********************************************************/
        /* One or more descriptors are readable or writable.      */
        /* Need to determine which ones they are.                 */
        /**********************************************************/
        /* check all descriptors, stop when checked all valid fds */
        int maxSd=pool->maxfd+1;
        int counter=0;
        for (int socketNumber=3; socketNumber<maxSd; socketNumber++)
        {
            if(counter>pool->nready) break;
            /* Each time a ready descriptor is found, one less has  */
            /* to be looked for.  This is being done so that we     */
            /* can stop looking at the working set once we have     */
            /* found all the descriptors that were ready            */

            /*******************************************************/
            /* Check to see if this descriptor is ready for read   */
            /*******************************************************/
            if (FD_ISSET(socketNumber,&pool->read_set))
            {
                counter++;
                /***************************************************/
                /* A descriptor was found that was readable		   */
                /* if this is the listening socket, accept one      */
                /* incoming connection that is queued up on the     */
                /*  listening socket before we loop back and call   */
                /* select again. 						            */
                /****************************************************/
                if(socketNumber==welcome_socket)
                {
                    int clientSocket=accept(welcome_socket,NULL,NULL);
                    if(clientSocket>0)
                    {
                        if(addConn(clientSocket,pool)==-1)
                        {
                            close(clientSocket);
                        }
                        else
                        {
                            printf("New incoming connection on sd %d\n", clientSocket);
                            FD_SET(clientSocket,&pool->ready_read_set);// puts the new sd in the pool
                        }
                    }
                }
                    /****************************************************/
                    /* If this is not the listening socket, an 			*/
                    /* existing connection must be readable				*/
                    /* Receive incoming data his socket                 */
                    /****************************************************/
                    /* If the connection has been closed by client 		*/
                    /* remove the connection (removeConn(...))    		*/
                    /****************************************************/
                    /* Data was received, add msg to all other          */
                    /* connectios					  			        */
                    /****************************************************/
                else if(readFromClientSocket(pool, socketNumber) == -1)
                {
                    printf("Connection closed for sd %d\n",socketNumber);
                    removeConn(socketNumber,pool);
                    updateMaxFd(pool,welcome_socket);
                    close(socketNumber);
                }
            } /* End of if (FD_ISSET()) */
            /*******************************************************/
            /* Check to see if this descriptor is ready for write  */
            /*******************************************************/
            if (FD_ISSET(socketNumber,&pool->write_set))
            {
                /* try to write all msgs in queue to sd */
                if(writeToClient(socketNumber,pool)==-1)
                {
                    removeConn(socketNumber,pool);
                    updateMaxFd(pool,welcome_socket);
                    close(socketNumber);
                }
            }
            /*******************************************************/
        } /* End of for loop through selectable descriptors */
    } while (end_server == 0);
    /*************************************************************/
    /* If we are here, Control-C was typed,						 */
    /* clean up all open connections					         */
    /*************************************************************/
    if(pool->nr_conns>0)
    {
        int len=(int)pool->nr_conns;
        for (int i = 0; i < len; i++)
        {
            removeConn(pool->conn_head->fd, pool);
        }
    }
    close(welcome_socket);
    free(pool);
    return 0;
}
//initialized all fields
int initPool(conn_pool_t* pool)
{
    pool->maxfd=-1;
    pool->nready=0;
    FD_ZERO(&(pool->read_set));
    FD_ZERO(&(pool->ready_read_set));
    FD_ZERO(&(pool->write_set));
    FD_ZERO(&(pool->ready_write_set));
    pool->conn_head=NULL;
    pool->nr_conns=0;
    return 0;
}
/*
    * 1. allocate connection and init fields
    * 2. add connection to pool
*/
int addConn(int sd, conn_pool_t* pool)
{
    conn_t* newConnection=(conn_t*)malloc(sizeof(conn_t));
    if(newConnection==NULL)
    {
        return -1;
    }
    newConnection->fd=sd;
    newConnection->write_msg_head=NULL;
    newConnection->write_msg_tail=NULL;
    if(pool->nr_conns==0)
    {
        pool->conn_head=newConnection;
        newConnection->next=NULL;
        newConnection->prev=NULL;
    }
    else
    {
        conn_t* temp=pool->conn_head;
        conn_t* last=temp;
        int len=(int)pool->nr_conns;
        for (int i = 0; i< len; i++)
        {
            last=temp;
            temp=temp->next;
        }
        last->next=newConnection;
        newConnection->prev=last;
    }
    pool->nr_conns+=1;// increase the connections counter by one
    if(sd>pool->maxfd)
    {
        pool->maxfd=sd;
    }
    return 0;
}
/*
   * 1. remove connection from pool
   * 2. deallocate connection
   * 3. remove from sets
   * 4. update max_fd if needed
*/
int removeConn(int sd, conn_pool_t* pool)
{
    conn_t* temp=pool->conn_head;
    if(temp==NULL)
    {
        return -1;
    }
    if(temp->fd==sd)
    {
        conn_t* newHead=NULL;
        if(pool->nr_conns > 1)
        {
            newHead=temp->next;
            if(newHead==NULL) newHead=NULL;
            else newHead->prev=NULL;
        }
        pool->conn_head=newHead;
    }
    else
    {
        int len=(int)pool->nr_conns;
        for (int i = 1; i <len; i++)
        {
            temp=temp->next;
            if(temp->fd==sd)
            {
                conn_t* prev=temp->prev;
                conn_t* next=temp->next;
                prev->next=next;
                if(next!=NULL)
                {
                    next->prev=prev;
                }
                break;
            }
        }
    }
    deleteConnMessages(temp);
    printf("removing connection with sd %d \n", sd);
    free(temp);
    close(sd);
    pool->nr_conns-=1;
    FD_CLR(sd,&pool->ready_read_set);
    FD_CLR(sd,&pool->ready_write_set);
    return 0;
}
/*
   * 1. add msg_t to write queue of all other connections
   * 2. set each fd to check if ready to write
*/
int addMsg(int sd,char* buffer,int len,conn_pool_t* pool)
{
    conn_t* temp=pool->conn_head;
    int loopLen=(int)pool->nr_conns;
    for (int i = 0; i <loopLen ; i++)
    {
        if(temp->fd!=sd)
        {
            msg_t* newMsg=(msg_t*)malloc(sizeof(msg_t));
            if(newMsg==NULL) return -1;
            newMsg->size=len;
            newMsg->message= strdup(buffer);
            newMsg->next=NULL;
            newMsg->prev=NULL;
            msg_t* msgTemp=temp->write_msg_head;
            //when the list is empty:
            if(msgTemp==NULL)
            {
                temp->write_msg_head=newMsg;
                temp->write_msg_tail=newMsg;
            }
            else
            {
                //when there is just one node in the msg:
                if(temp->write_msg_head->next==NULL)
                {
                    temp->write_msg_head->next=newMsg;
                    temp->write_msg_tail=newMsg;
                    newMsg->prev=temp->write_msg_head;
                }
                else
                {
                    temp->write_msg_tail->next=newMsg;
                    newMsg->prev=temp->write_msg_tail;
                    temp->write_msg_tail=newMsg;
                }
            }
            FD_SET(temp->fd,&pool->ready_write_set);
        }
        temp=temp->next;
    }
    return 0;
}
/*
   * 1. write all msgs in queue
   * 2. deallocate each writen msg
   * 3. if all msgs were writen successfully, there is nothing else to write to this fd...
*/
int writeToClient(int sd,conn_pool_t* pool)
{
    conn_t* currentConn=pool->conn_head;
    int len=(int)pool->nr_conns;
    for (int i = 0; i <len; i++)
    {
        if(currentConn->fd == sd) break;
        else currentConn=currentConn->next;
    }
    msg_t* temp;
    msg_t* currentMsg=currentConn->write_msg_head;
    while(currentMsg!=NULL)
    {
        size_t bytesNum=write(sd,currentMsg->message,currentMsg->size);
        if(bytesNum<=0)
        {
            return -1;
        }
        //successful write:
        temp=currentMsg;
        currentMsg=currentMsg->next;
        free(temp->message);
        free(temp);
    }
    currentConn->write_msg_head=NULL;
    currentConn->write_msg_tail=NULL;
    FD_CLR(sd,&pool->ready_write_set);
    return 0;
}
int readFromClientSocket(conn_pool_t* pool,int socket)
{
    printf("Descriptor %d is readable\n", socket);
    char buffer[BUFFER_SIZE+1];
    ssize_t bytesNum;
    bytesNum=read(socket,buffer,(size_t)BUFFER_SIZE);
    if(bytesNum<0 || bytesNum==0)
    {
        return -1;
    }
    buffer[bytesNum]='\0';
    printf("%d bytes received from sd %d\n",(int)bytesNum,socket);
    toUpperCase((int)bytesNum,buffer);
    addMsg(socket,buffer,(int)bytesNum,pool);
    return 0;
}
void updateMaxFd(conn_pool_t* pool,int welcomeSocket)
{
    if(pool==NULL) return;
    int currentMax=welcomeSocket;
    conn_t* temp=pool->conn_head;
    if(temp==NULL)
    {
        pool->maxfd=welcomeSocket;
        return;
    }
    int len=(int)pool->nr_conns;
    for (int i = 0; i <len; i++)
    {
        if(temp->fd>currentMax)
        {
            currentMax=temp->fd;
        }
        temp=temp->next;
    }
    pool->maxfd=currentMax;
}
void deleteConnMessages(conn_t* connect)
{
    msg_t* temp=connect->write_msg_head;
    if(temp==NULL) return;
    while(temp!=NULL)
    {
        msg_t* toFree=temp;
        temp=temp->next;
        free(toFree->message);
        free(toFree);
    }
}
void toUpperCase(int len, char buffer[])
{
    for (int i = 0; i < len; i++)
    {
        buffer[i] = (char)toupper((unsigned char)buffer[i]);
    }
}
