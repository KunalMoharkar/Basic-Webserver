#include "io_helper.h"
#include "request.h"
#define MAXBUF (8192)
#define MAX_REQUEST_BUFFER_SIZE (10000)

//pthread mutex lock
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
//2 condition variables for producer consumer problem
pthread_cond_t full = PTHREAD_COND_INITIALIZER;
pthread_cond_t empty = PTHREAD_COND_INITIALIZER;

//
//	TODO: add code to create and manage the buffer
//

//struct to hold the request object
//int fd - file descripter  
//string query is reuqested uri (filename) 
//int reqsize holds size of requested file (for sff)
typedef struct http_request
{
  int fd;
  char *query;
  int reqsize;

} httpreq;

//create a global buffer queue of httprequest to hold incoming requests
httpreq req_buf[MAX_REQUEST_BUFFER_SIZE];

int front=-1; //points to front of queue
int rear=0;   //points to rear of queue
int size = 0; //number of requests in the buffer

/*
SUPPORTS INSERTION AND DELETION IN CONSTANT TIME IN FIFO
SUPPORTS INSERTION IN O(N) AND DELETION IN CONSTANT TIME IN SFF
*/

//outputs the contents of the queue
void show()
{
  int i;
  
  printf("\n");
  printf("request_queue: ");
  //if queue not empty show its contents
  if(size!=0)                   
  {
    for(i=front;i<=rear;i++)
    { 
      if(i!=-1)
      {
        printf("fd--%d(%d)--",req_buf[i].fd,req_buf[i].reqsize);   //print the descripter of requested file
      }
    }

  }
  else   //if queue is empty
  {
    printf(" empty");
  }
  
  printf("\n\n");
}

//inserts a request into buffer queue (put routine)
void push(httpreq req)
{
  //queue is not full
  if(size!=buffer_max_size)
  {
    //fifo
    if(scheduling_algo==0)
    { 
        if(size==0)               //insert at the last
        {  
          req_buf[rear] = req;
          front =0;
        }
        else
        {
          rear++;
          req_buf[rear] = req;
        } 
    }
    //sff  
    else
    { 

      if(size==0)
      {
        req_buf[rear] = req;
        front =0;
      }
      else
      {
        int new_req_size = req.reqsize;
        int i = rear;
        
        while(i>=front&&req_buf[i].reqsize > new_req_size) //while new request size                                               
        {
          req_buf[i+1] = req_buf[i];    //shift the elements one position
          i--;
        }

        req_buf[i+1] = req;             //insert at its appropriate position
        rear++;
        
      }
      
      
    } 
    //increment the current size by 1 
    size++;
  }
}

//removes the request from the front of the queue
httpreq pop()
{ 
  //if queue is not empty
  if(size!=0)
  {
    httpreq req = req_buf[front]; //get the first request 
    front++;                      //move to front by 1 pos
    size--;                       //decrement current size od buffer

    if(size==0)
    {
      rear = 0;
      front = -1;
    }
    return req;                  //return the removed request
  }
}

//check if request is valid i.e int the server root dir subtree 
int validate_request(char *path)
{    
    /*if in the requested path number of '..' 
      exceed number of directory names reject request*/ 
    
    int count = 0;
    char *string,*found;
    string = strdup(path);
    while( (found = strsep(&string,"/")) != NULL ) //seperate on '/'
    {
      if(strcmp(found,"..")==0)
      {
        count--;
      }
      else
      {
        count++;
      }
      
    }
    if(count > 1)  //valid return 1
    {
      return 1;
    }
        
    return 0;
}

//
// Sends out HTTP response in case of errors
//
void request_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXBUF], body[MAXBUF];
    
    // Create the body of error message first (have to know its length for header)
    sprintf(body, ""
	    "<!doctype html>\r\n"
	    "<head>\r\n"
	    "  <title>OSTEP WebServer Error</title>\r\n"
	    "</head>\r\n"
	    "<body>\r\n"
	    "  <h2>%s: %s</h2>\r\n" 
	    "  <p>%s: %s</p>\r\n"
	    "</body>\r\n"
	    "</html>\r\n", errnum, shortmsg, longmsg, cause);
    
    // Write out the header information for this response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Type: text/html\r\n");
    write_or_die(fd, buf, strlen(buf));
    
    sprintf(buf, "Content-Length: %lu\r\n\r\n", strlen(body));
    write_or_die(fd, buf, strlen(buf));
    
    // Write out the body last
    write_or_die(fd, body, strlen(body));
    
    // close the socket connection
    close_or_die(fd);
}

//
// Reads and discards everything up to an empty text line
//
void request_read_headers(int fd) {
    char buf[MAXBUF];
    
    readline_or_die(fd, buf, MAXBUF);
    while (strcmp(buf, "\r\n")) {
		readline_or_die(fd, buf, MAXBUF);
    }
    return;
}

//
// Return 1 if static, 0 if dynamic content (executable file)
// Calculates filename (and cgiargs, for dynamic) from uri
//
int request_parse_uri(char *uri, char *filename, char *cgiargs) {
    char *ptr;
    
    if (!strstr(uri, "cgi")) { 
	// static
	strcpy(cgiargs, "");
	sprintf(filename, ".%s", uri);
	if (uri[strlen(uri)-1] == '/') {
	    strcat(filename, "index.html");
	}
	return 1;
    } else { 
	// dynamic
	ptr = index(uri, '?');
	if (ptr) {
	    strcpy(cgiargs, ptr+1);
	    *ptr = '\0';
	} else {
	    strcpy(cgiargs, "");
	}
	sprintf(filename, ".%s", uri);
	return 0;
    }
}

//
// Fills in the filetype given the filename
//
void request_get_filetype(char *filename, char *filetype) {
    if (strstr(filename, ".html")) 
		strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) 
		strcpy(filetype, "image/gif");
    else if (strstr(filename, ".jpg")) 
		strcpy(filetype, "image/jpeg");
    else 
		strcpy(filetype, "text/plain");
}

//
// Handles requests for static content
//
void request_serve_static(int fd, char *filename, int filesize) {
    int srcfd;
    char *srcp, filetype[MAXBUF], buf[MAXBUF];
    
    request_get_filetype(filename, filetype);
    srcfd = open_or_die(filename, O_RDONLY, 0);
    
    // Rather than call read() to read the file into memory, 
    // which would require that we allocate a buffer, we memory-map the file
    srcp = mmap_or_die(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    close_or_die(srcfd);
    
    // put together response
    sprintf(buf, ""
	    "HTTP/1.0 200 OK\r\n"
	    "Server: OSTEP WebServer\r\n"
	    "Content-Length: %d\r\n"
	    "Content-Type: %s\r\n\r\n", 
	    filesize, filetype);
       
    write_or_die(fd, buf, strlen(buf));
    
    //  Writes out to the client socket the memory-mapped file 
    write_or_die(fd, srcp, filesize);
    munmap_or_die(srcp, filesize);
}

//
// Fetches the requests from the buffer and handles them (thread locic)
//
void* thread_request_serve_static(void* arg)
{
	// TODO: write code to actualy respond to HTTP requests

  while (1) //keep consuming the requests
  {
    pthread_mutex_lock(&lock); //hold lock for critical operation
    while(size==0)              //wait if the buffer is empty releasing the lock
    {
      pthread_cond_wait(&full,&lock);
    }
    httpreq req = pop();     //remove request for the queue
    printf("\nRequest for %s with fd %d is removed from the buffer.", req.query, req.fd);
    show();                       //show buffer contents
    pthread_cond_signal(&empty); //signal the producer to wake up 
    pthread_mutex_unlock(&lock);  //release the lock
    request_serve_static(req.fd,req.query,req.reqsize); //serve the request
  }
}

//
// Initial handling of the request
//
void request_handle(int fd) {
    int is_static;
    struct stat sbuf;
    char buf[MAXBUF], method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char filename[MAXBUF], cgiargs[MAXBUF];
    
	// get the request type, file path and HTTP version
    readline_or_die(fd, buf, MAXBUF);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("method:%s uri:%s version:%s\n", method, uri, version);

	  // verify if the request type is GET is not
    if (strcasecmp(method, "GET")) {
		request_error(fd, method, "501", "Not Implemented", "server does not implement this method");
		return;
    }
    request_read_headers(fd);


    //verify if request in valid
    int is_valid_path = validate_request(uri);

    //if request is not secure
    if(is_valid_path==0)
    {
      request_error(fd, method, "403", "Forbidden", "Traversing up in filesystem is not allowed");
	  	return;
    }
    
	  // check requested content type (static/dynamic)

    is_static = request_parse_uri(uri, filename, cgiargs);
    
	  // get some data regarding the requested file, also check if requested file is present on server
  
    if (stat(filename, &sbuf) < 0) {
		request_error(fd, filename, "404", "Not found", "server could not find this file");
		return;
    }
    
	  // verify if requested content is static
    if (is_static) {
		if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
			request_error(fd, filename, "403", "Forbidden", "server could not read this file");
			return;
		}

   

		
		// TODO: write code to add HTTP requests in the buffer based on the scheduling policy
    
    //create a new request object
    httpreq req;
    req.fd = fd;                  //descriptor
    req.query = strdup(filename); //filename
    req.reqsize = sbuf.st_size;   //filesize


    //hold the lock as critical operation on buffer
    pthread_mutex_lock(&lock); 
    while(size == buffer_max_size) //wait if buffer already full and release the lock
    {
        pthread_cond_wait(&empty,&lock);
    }
    push(req);   //insert new request in buffer
    printf("\nRequest for %s with fd %d is added to the buffer", req.query, req.fd);
    show();      //show buffer content
    pthread_cond_signal(&full);   //signal to sleeping consumer threads
    pthread_mutex_unlock(&lock); //release the lock

    } else {
		request_error(fd, filename, "501", "Not Implemented", "server does not serve dynamic content request");
    }
}
