#include "httpd.h"
#include "../common/tcp.h"

#define SERVER_STRING   "Server: nerdobd ajax server |0.9.4\r\n"
#define SERVER_CON      "Connection: close\r\n"
#define HTTP_OK         "HTTP/1.0 200 OK\r\n"
#define HTTP_ERROR      "HTTP/1.0 404 Not Found\r\n"

#define HEADER_PLAIN    SERVER_STRING SERVER_CON "Content-Type: text/plain\r\n\r\n"
#define HEADER_HTML     SERVER_STRING SERVER_CON "Content-Type: text/html\r\n\r\n"
#define HEADER_PNG      SERVER_STRING SERVER_CON "Content-Type: image/png\r\n\r\n"
#define HEADER_CSS      SERVER_STRING SERVER_CON "Content-Type: text/css\r\n\r\n"
#define HEADER_JS       SERVER_STRING SERVER_CON "Content-Type: application/x-javascript\r\n\r\n"
#define HEADER_ICON     SERVER_STRING SERVER_CON "Content-Type: image/x-icon\r\n\r\n"


int
send_error(int fd, char *message)
{
    char out[LEN_BUFFER];

    snprintf(out, sizeof(out), HTTP_ERROR
             "Content-Length: %d\r\n", strlen(message));
    
    if (write(fd, out, strlen(out)) <= 0)
        return -1;
    
    if (write(fd, HEADER_PLAIN, strlen(HEADER_PLAIN)) <= 0)
        return -1;
    
    if (write(fd, message, strlen(message)) <= 0)
        return -1;    
    
    return 0;
}


int
send_file(int fd, char *filename)
{
    int file_fd;
    int r;
    char *p;
    char out[LEN_BUFFER];
    struct stat stats;
    char path[LEN];
    
    
    // terminate arguments after ?
    if ( (p = strchr(filename, '?')) != NULL) 
        *p = '\0';

    // search for last dot in filename
    if ( (p = strrchr(filename, '.')) == NULL)
    {
        printf("no . found in filename!\n");
        return -1;
    }
    
    // merge filename with docroot path
    snprintf(path, sizeof(path), "%s%s", DOCROOT, filename);

    if (stat(path, &stats) == -1)
    {
#ifdef DEBUG_AJAX        
        printf("file with 0bytes: %s\n", path);
#endif        
        return -1;
    }
    
    
    // send content length
    snprintf(out, sizeof(out), HTTP_OK
             "Content-Length: %jd\r\n", (intmax_t) stats.st_size);
    if (write(fd, out, strlen(out)) <= 0)
        return -1;
        
    
#ifdef DEBUG_AJAX
    printf("sending file: %s with %9jd length\n", path, (intmax_t) stats.st_size);
#endif
    
    // is file type known?
    if ( !strcmp(p, ".html") ||  !strcmp(p, ".htm") )
    {
        if (write(fd, HEADER_HTML, strlen(HEADER_HTML)) <= 0)
            return -1;
    }
    else if (!strcmp(p, ".png") ) 
    {
        if (write(fd, HEADER_PNG, strlen(HEADER_PNG)) <= 0)
            return -1;
    }
    else if (!strcmp(p, ".txt") )
    {
        if (write(fd, HEADER_PLAIN, strlen(HEADER_PLAIN)) <= 0)
            return -1;
    }
    else if (!strcmp(p, ".js") )
    {
        if (write(fd, HEADER_JS, strlen(HEADER_JS)) <= 0)
            return -1;
    }
    else if (!strcmp(p, ".css") )
    {
        if (write(fd, HEADER_CSS, strlen(HEADER_CSS)) <= 0)
            return -1;
    }
    else if (!strcmp(p, ".ico") )
    {
        if (write(fd, HEADER_ICON, strlen(HEADER_ICON)) <= 0)
            return -1;
    }
    else
    {
        printf("extention not found\n");
        return -1;
    }
    
    // open and send file
    if(( file_fd = open(path, O_RDONLY)) == -1)
    {
        perror("open()");
        return -1;
    }
    
    while ( (r = read(file_fd, out, sizeof(out))) > 0 )
        if (write(fd, out, r) <= 0)
            return -1;
    
    close(file_fd);
    return 0;
}


int
send_json_data(int fd, char *args)
{  
    char       *p;
    char        out[LEN_BUFFER];
    long        consumption_index = 0;
    long        consumption_timespan = 300;
    long        speed_index = 0;
    long        speed_timespan = 300;
    const char *json;
    
    // parse arguments
    if (strtok(args, "?") != NULL)
    {
        p = strtok(NULL, "=");
        while (p != NULL)
        {
            if (!strcmp(p, "consumption_index"))
                consumption_index = atoi(strtok(NULL, "&"));
            else if (!strcmp(p, "consumption_timespan"))
                consumption_timespan = atoi(strtok(NULL, "&"));
            else if (!strcmp(p, "speed_index"))
                speed_index = atoi(strtok(NULL, "&"));
            else if (!strcmp(p, "speed_timespan"))
                speed_timespan = atoi(strtok(NULL, "&"));
            
            p = strtok(NULL, "=");
        }
    }
    
    json = json_generate(consumption_index, consumption_timespan, speed_index, speed_timespan);
    
#ifdef DEBUG_AJAX 
    // printf("serving json:\n%s\n", json);
#endif
    
    // send content length
    snprintf(out, sizeof(out), HTTP_OK
             "Content-Length: %d\r\n", strlen(json));
    
    if (write(fd, out, strlen(out)) <= 0)
        return -1;
    
    if (write(fd, HEADER_PLAIN, strlen(HEADER_PLAIN)) <= 0)
        return -1;
    
    if (write(fd, json, strlen(json)) <= 0)
        return -1;
    
    return 0;
}


void
handle_client(int fd)
{
    int r;
    int i;
    char *p;
    static char buffer[LEN_BUFFER];
    
    
    // read socket
    if ( (r = read (fd, buffer, sizeof(buffer))) < 0)
    {
        printf("read() failed\n");
        return;
    }
    
    // terminate received string
    if ( r > 0 && r < sizeof(buffer))
        buffer[r] = '\0';
    else
        buffer[0] = '\0';
    
    printf("buffer: %s\n", buffer);

    // filter requests we don't support
    if (strncmp(buffer,"GET ", 4) && strncmp(buffer,"POST ", 5) )
    {
        send_error(fd, "not supported (only GET and POST)");
        return;
    }
    
    // look for second space (or newline) and terminate string (skip headers)
    if ( (p = strchr(buffer, ' ')) != NULL)
    {
        for ( ; ; )
        {
            p++;
            if(*p == '\r' || *p == '\n' || *p == ' ')
            {
                *p = '\0';
                break;
            }
        }
    }
    else
    {
        send_error(fd, "invalid request.\n");
        return;
    }
    
    // convert / to index.html
    if (!strncmp(buffer, "GET /\0", 6)) 
        strncpy(buffer, "GET /index.html", sizeof(buffer));
    
    
    // check for illegal parent directory requests   
    for (i = 0; ; i++)
    {
        if (buffer[i] == '\0' || buffer[i + 1] == '\0')
            break;
        
        if(buffer[i] == '.' && buffer[i + 1] == '.')
        {
            send_error(fd, ".. detected.\n");
            return;
        }
    }

    // point p to filename
    if ( (p = strchr(buffer, '/')) == NULL)
        return;
    
    
    // send json data
    if (!strncmp(p, "/data.json", 10) )
        send_json_data(fd, buffer);
 
    // send file
    else
        if (send_file(fd, p) != 0)
            send_error(fd, "could not send file.\n");
    
    return;
}


int
main(int argc, char **argv)
{
    int s;
    
    if (open_db() == -1)
        return -1;
    
    if ( (s = tcp_listen(HTTPD_PORT)) == -1)
        return -1;
    
    tcp_loop_accept(s, &handle_client);
    
    // should never be reached
    return 0;
}
