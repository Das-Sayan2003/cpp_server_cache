#include "proxy_parse.h"
#include <time.h>
#include <string>
#include <pthread.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/wait.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <iostream>

class CacheElement
{
public:
    std::string data;
    int length;
    std::string url;
    time_t lru_time;
    CacheElement *next;
};

CacheElement *find(std::string &url);
int addCacheElement(std::string &data, int size, std::string &url);
void removeCacheElement();

const int MAX_ELEMENT_SIZE = 10 * (1 << 9);
const int MAX_SIZE = 100 * (1 << 10);

const size_t BUFFER_SIZE = 2048;
int PORT_NUMBER = 8000;
const unsigned int MAX_CLIENTS = 5;

int proxy_socket_id = 0;
int reuse = 1;
int connected_client_count = 0;

int connected_socket_id[MAX_CLIENTS] = {0};

pthread_t tid[MAX_CLIENTS];
sem_t semaphore;
pthread_mutex_t lock;

CacheElement *head;
int cache_size = 0;

int connectRemoteServer(const char *host_addr, int port_num)
{
    int remote_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (remote_socket < 0)
    {
        std::cerr << "Error in creating soccket!" << std::endl;
        return -1;
    }

    auto host = gethostbyname(host_addr);

    if (host == NULL)
    {
        std::cerr << "Host not resolved!" << std::endl;
        return -1;
    }

    sockaddr_in server_addr;

    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);
    bcopy((char *)&host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);

    if (connect(remote_socket, (struct sockaddr *)&server_addr, (size_t)sizeof(server_addr) < 0))
    {
        std::cerr << "Error in conneection!" << std::endl;
        return -1;
    }
    return remote_socket;
}

int handle_request(int client_socket_id, ParsedRequest *request, std::string &buff)
{
    char *buffer = (char *)malloc(sizeof(char) * BUFFER_SIZE);
    strcpy(buffer, "GET ");
    strcat(buffer, request->path);
    strcat(buffer, " ");
    strcat(buffer, request->version);
    strcat(buffer, "\r\n");

    size_t len = strlen(buffer);

    if (ParsedHeader_set(request, "Connection", "close") < 0)
    {
        std::cerr << "Error in setting Connection parameter" << std::endl;
    }

    if (ParsedHeader_get(request, "Host") == NULL)
    {
        if (ParsedHeader_set(request, "Host", request->host) < 0)
        {
            std::cerr << "Failed to set request host value!" << std::endl;
        }
    }

    if (ParsedRequest_unparse_headers(request, buffer + len, (size_t)BUFFER_SIZE - len) < 0)
    {
        std::cerr << "Failed to unparse buffer" << std::endl;
    }

    int server_port = 80;

    if (request->port != NULL)
    {
        server_port = atoi(request->port);
    }
    int remote_socket_id = connectRemoteServer(request->host, server_port);

    if (remote_socket_id < 0)
    {
        return -1;
    }

    int bytes_sent = send(remote_socket_id, buffer, strlen(buffer), 0);
    bzero(buffer, BUFFER_SIZE);

    bytes_sent = recv(remote_socket_id, buffer, BUFFER_SIZE - 1, 0);
    std::string temp_buffer = "";

    while (bytes_sent > 0)
    {
        bytes_sent = send(client_socket_id, buffer, bytes_sent, 0);
        for (int i = 0; i < bytes_sent / sizeof(char); i++)
        {
            temp_buffer += buffer[i];
        }
        if (bytes_sent < 0)
        {
            std::cerr << "Error sending data to client!" << std::endl;
            break;
        }
        bzero(buffer, BUFFER_SIZE);
        bytes_sent = recv(remote_socket_id, buffer, BUFFER_SIZE - 1, 0);
    }
    free(buffer);
    addCacheElement(temp_buffer, temp_buffer.size(), buff);
    close(remote_socket_id);
    return 0;
}

int checkHTTPVersion(char *str)
{
    int version = -1;
    if (strncmp(str, "HTTP/1.0", 8) == 0)
    {
        version = 1;
    }
    else if (strncmp(str, "HTTP/1.1", 8) == 0)
    {
        version = 1;
    }
    return version;
}

int sendErrorMessage(int socket, int status_code)
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);

    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    switch (status_code)
    {
    case 400:
        snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
        std::cerr << "400 Bad Request" << std::endl;
        send(socket, str, strlen(str), 0);
        break;

    case 403:
        snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
        std::cerr << "403 Forbidden" << std::endl;
        send(socket, str, strlen(str), 0);
        break;

    case 404:
        snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
        std::cerr << "404 Not Found" << std::endl;
        send(socket, str, strlen(str), 0);
        break;

    case 500:
        snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
        std::cerr << "500 Internal Server Error" << std::endl;
        send(socket, str, strlen(str), 0);
        break;

    case 501:
        snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
        std::cerr << "501 Not Implemented" << std::endl;
        send(socket, str, strlen(str), 0);
        break;

    case 505:
        snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
        std::cerr << "505 HTTP Version Not Supported" << std::endl;
        send(socket, str, strlen(str), 0);
        break;

    default:
        return -1;
    }
    return 1;
}

void *thread_fn(void *sckt)
{
    sem_wait(&semaphore);
    int sem_value;
    sem_getvalue(&semaphore, &sem_value);
    std::cout << "Current Semaphore value is: " << sem_value << std::endl;

    int socket = *((int *)sckt);
    int client_bytes_sent, length;

    std::string response = "";
    char *buffer = (char *)calloc(BUFFER_SIZE, sizeof(char));
    client_bytes_sent = recv(socket, buffer, BUFFER_SIZE, 0);

    while (client_bytes_sent > 0)
    {
        length = strlen(buffer);
        response += buffer;
        strcpy(buffer, "");
    }
    strcpy(buffer, "");

    CacheElement *temp = find(response);

    if (temp != NULL)
    {
        send(socket, temp->data.c_str(), temp->data.size(), 0);
        std::cout << "Data retrieved from cache!" << std::endl;
    }
    else if (client_bytes_sent > 0)
    {
        ParsedRequest *request = ParsedRequest_create();
        if (ParsedRequest_parse(request, response.c_str(), response.size()) < 0)
        {
            std::cerr << "Server response failed to parse!" << std::endl;
        }
        else
        {
            bzero(buffer, BUFFER_SIZE);
            if (!strcmp(request->method, "GET"))
            {
                if (request->host && request->path && (checkHTTPVersion(request->version) == 1))
                {
                    client_bytes_sent = handle_request(socket, request, response);
                    if (client_bytes_sent == -1)
                    {
                        sendErrorMessage(socket, 500);
                    }
                }
                else
                {
                    sendErrorMessage(socket, 500);
                }
            }
            else
            {
                std::cout << "Only geet request are served!" << std::endl;
            }
        }
        ParsedRequest_destroy(request);
    }
    else if (client_bytes_sent == 0)
    {
        std::cout << "Client disconnected!" << std::endl;
    }
    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buffer);
    sem_post(&semaphore);
    sem_getvalue(&semaphore, &sem_value);
    std::cout << "Semaphore value is: " << sem_value << std::endl;
}

int main(int argc, char *argv[])
{
    int client_socket_id, client_length;
    sockaddr_in server_addr, client_addr;
    sem_init(&semaphore, 0, MAX_CLIENTS);

    pthread_mutex_init(&lock, NULL);

    if (argc == 2)
    {
        PORT_NUMBER = atoi(argv[1]);
    }
    std::cout << "Starting server at port: " << PORT_NUMBER << std::endl;

    proxy_socket_id = socket(AF_INET, SOCK_STREAM, 0);

    if (proxy_socket_id < 0)
    {
        std::cout << "Failed to create a socket!!" << std::endl;
        exit(1);
    }

    if (setsockopt(proxy_socket_id, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuse, sizeof(reuse)) < 0)
    {
        std::cerr << "Set Socket Opt failed!" << std::endl;
    }

    bzero((void *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT_NUMBER);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(proxy_socket_id, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        std::cerr << "Port binding failed!!" << std::endl;
        exit(1);
    }
    std::cout << "Port binding successsful! Port: " << PORT_NUMBER << std::endl;

    int listen_status = listen(proxy_socket_id, MAX_CLIENTS);

    if (listen_status < 0)
    {
        std::cerr << "Failed to listen!" << std::endl;
        exit(1);
    }

    while (1)
    {
        bzero((void *)&client_addr, sizeof(client_addr));
        client_length = sizeof(client_addr);
        client_socket_id = accept(proxy_socket_id, (sockaddr *)&client_addr, (socklen_t *)&client_length);
        if (client_socket_id < 0)
        {
            std::cerr << "Client connection failed" << std::endl;
            exit(1);
        }
        else
        {
            connected_socket_id[connected_client_count] = client_socket_id;
        }

        struct sockaddr_in *client_pt = (sockaddr_in *)&client_addr;
        struct in_addr ip_address = client_pt->sin_addr;
        char str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &ip_address, str, INET_ADDRSTRLEN);
        std::cout << "Client connected to port: " << ntohs(client_addr.sin_port) << " Ip: " << client_addr.sin_addr.s_addr << std::endl;

        pthread_create(&tid[connected_client_count], NULL, thread_fn, (void *)&connected_socket_id[connected_client_count]);
        connected_client_count++;
    }

    close(proxy_socket_id);
    return 0;
}

void remove_cache_element()
{
    CacheElement *p;
    CacheElement *q;
    CacheElement *temp;
    int temp_lock_val = pthread_mutex_lock(&lock);
    std::cout << "Cache Lock Acquired" << temp_lock_val << std::endl;
    if (head != NULL)
    {
        for (q = head, p = head, temp = head; q->next != NULL; q = q->next)
        {
            if (((q->next)->lru_time < (temp->lru_time)))
            {
                temp = q->next;
                p = q;
            }
        }
        if (temp == head)
        {
            head = head->next;
        }
        else
        {
            p->next = temp->next;
        }
        cache_size -= sizeof(temp);
        delete temp;
    }
    temp_lock_val = pthread_mutex_unlock(&lock);
    std::cout << "Cache Lock Removed" << temp_lock_val << std::endl;
}

CacheElement *find(std::string &url)
{
    CacheElement *site = NULL;
    int temp_lock_val = pthread_mutex_lock(&lock);
    std::cout << "Lock  acquired!" << std::endl;
    if (head != NULL)
    {
        site = head;
        while (site != NULL)
        {
            if (!strcmp(site->url.c_str(), url.c_str()))
            {
                std::cout << "Cache time track before:" << site->lru_time << std::endl;
                std::cout << "Urrl found in cache!" << std::endl;
                site->lru_time = time(NULL);
                std::cout << "Cache time  track  after: " << site->lru_time << std::endl;
                break;
            }
            else
            {
                site = site->next;
            }
        }
    }
    else
    {
        std::cout << "Url not found!" << std::endl;
    }
    temp_lock_val = pthread_mutex_unlock(&lock);
    std::cout << "Mutex lock removed!" << std::endl;
}

int addCacheElement(std::string &data, int size, std::string &url)
{
    int temp_lock_val = pthread_mutex_lock(&lock);
    std::cout << "Lock  acquired!" << std::endl;
    int element_size = size + 1 + url.size() + sizeof(CacheElement);
    if (element_size > MAX_ELEMENT_SIZE)
    {
        temp_lock_val = pthread_mutex_unlock(&lock);
        std::cout << "Cache element size exceeded!" << std::endl;
        std::cout << "Mutex lock removed!" << std::endl;

        return 0;
    }
    else
    {
        while (cache_size + element_size > MAX_SIZE)
        {
            remove_cache_element();
        }
        CacheElement *element = (CacheElement *)malloc(sizeof(CacheElement));
        element->data = (char *)malloc(size + 1);
        element->data = data;
        element->url = url;
        element->lru_time = time(NULL);
        element->next = head;
        element->length = size;
        head = element;
        cache_size += element_size;
        temp_lock_val = pthread_mutex_unlock(&lock);
        std::cout << "Add Cache Lock Unlocked:" << temp_lock_val << std::endl;
        // sem_post(&cache_lock);
        //  free(data);
        //  printf("--\n");
        //  free(url);
        return 1;
    }
    return 0;
}