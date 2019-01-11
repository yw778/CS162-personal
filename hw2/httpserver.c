#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unistd.h>

#include "libhttp.h"
#include "wq.h"

#define BUFFER_SIZE 1024

/*
 * Global configuration variables.
 * You need to use these in your implementation of handle_files_request and
 * handle_proxy_request. Their values are set up in main() using the
 * command line arguments (already implemented for you).
 */
wq_t work_queue;
int num_threads;
int server_port;
char *server_files_directory;
char *server_proxy_hostname;
int server_proxy_port;

typedef struct arg_pair {
    int from;
    int to;
} arg_t;

void not_found_res(int fd) {
    http_start_response(fd, 404);
    http_send_header(fd, "Content-Type", "text/html");
    http_send_header(fd, "Server", "httpserver/1.0");
    http_end_headers(fd);
    http_send_string(fd,
                     "<center>"
                     "<h1>File or Directory Not Found</h1>"
                     "</center>");
}

void not_found_index_file(int fd) {
    http_start_response(fd, 404);
    http_send_header(fd, "Content-Type", "text/html");
    http_send_header(fd, "Server", "httpserver/1.0");
    http_end_headers(fd);
    http_send_string(fd,
                     "<center>"
                     "<h1>Not Found Index.html in the Directory</h1>"
                     "</center>");
}

void internal_error_res(int fd) {
    http_start_response(fd, 500);
    http_send_header(fd, "Content-Type", "text/html");
    http_send_header(fd, "Server", "httpserver/1.0");
    http_end_headers(fd);
    http_send_string(fd,
                     "<center>"
                     "<h1>Internal Error</h1>"
                     "</center>");
}

void response_file(int fd, char* file_path) {
    char file_buffer[BUFFER_SIZE];
    char file_size_str[BUFFER_SIZE];
    int file_size;

    memset(file_buffer, '\0', BUFFER_SIZE);
    memset(file_size_str, '\0', BUFFER_SIZE);

    FILE* fp = fopen(file_path, "r");

    if (!fp) {
        return not_found_res(fd);
    }

    fseek(fp, 0, SEEK_END);
    file_size = ftell(fp);
    rewind(fp);
    sprintf(file_size_str, "%d", file_size);

    http_start_response(fd, 200);
    printf("%s\n", http_get_mime_type(file_path));
    http_send_header(fd, "Content-Type", http_get_mime_type(file_path));
    http_send_header(fd, "Content-Length", file_size_str);
    http_send_header(fd, "Server", "httpserver/1.0");
    http_end_headers(fd);
    while(!feof(fp)) {
        memset(file_buffer, '\0', BUFFER_SIZE);
        fread(file_buffer, 1, BUFFER_SIZE, fp);
        http_send_string(fd, file_buffer);
    }
    close(fd);
}

void list_response(int fd, char* dir_path, char* request_path) {
    DIR* dp;
    struct dirent *ep;
    char res_buff[BUFFER_SIZE];
    memset(res_buff, '\0', BUFFER_SIZE);
    if ((dp = opendir(dir_path)) == NULL) {
        return not_found_res(fd);
    }
    strcpy(res_buff, "<html><body><ul>");
    while((ep = readdir(dp)) != NULL) {
        char link_buffer[BUFFER_SIZE];
        char html_buffer[BUFFER_SIZE];
        memset(link_buffer, '\0', BUFFER_SIZE);
        memset(html_buffer, '\0', BUFFER_SIZE);
        strcpy(link_buffer, request_path);
        strcat(link_buffer, ep->d_name);
        sprintf(html_buffer, "<li><a href=\"%s\">%s</a></li>", link_buffer, ep->d_name);
        strcat(res_buff, html_buffer);
    }
    strcat(res_buff, "</ul></body></html>");

    http_start_response(fd, 200);
    http_send_header(fd, "Content-Type", "text/html");
    http_send_header(fd, "Server", "httpserver/1.0");
    http_end_headers(fd);
    http_send_string(fd, res_buff);
}

void* proxy_child_worker(void *arg) {
    printf("Served by proxy child thread_id %i\n", (unsigned int)(pthread_self() % 100));
    arg_t* pair = (arg_t*) arg;
    char buffer[BUFFER_SIZE];
    int size;
    while((size = read(pair->from, buffer, BUFFER_SIZE)) > 0) {
        write(pair->to, buffer, size);
        printf("from %d to %d: %d bytes \n", pair->from, pair->to, size);
    }
    printf("thread_id %i finish\n", (unsigned int)(pthread_self() % 100));
    return NULL;
}


/*
 * Reads an HTTP request from stream (fd), and writes an HTTP response
 * containing:
 *
 *   1) If user requested an existing file, respond with the file
 *   2) If user requested a directory and index.html exists in the directory,
 *      send the index.html file.
 *   3) If user requested a directory and index.html doesn't exist, send a list
 *      of files in the directory with links to each.
 *   4) Send a 404 Not Found response.
 */
void handle_files_request(int fd) {

  /*
   * TODO: Your solution for Task 1 goes here! Feel free to delete/modify *
   * any existing code.
   */

  struct http_request *request = http_request_parse(fd);

  if (request == NULL) {
    return internal_error_res(fd);
  }

  char file_path[BUFFER_SIZE];
  memset(file_path, '\0', BUFFER_SIZE);
  strcpy(file_path, server_files_directory);
  strcat(file_path, request->path);
  printf("file path is %s \n", file_path);

  struct stat path_stat;
  int status;
  if ((status = stat(file_path, &path_stat)) == -1) {
      return not_found_res(fd);
  }
  if (S_ISREG(path_stat.st_mode)) {
    return response_file(fd, file_path);
  } else if (S_ISDIR(path_stat.st_mode)) {
    // Default return index.html as all http servers.
    char dir_path[BUFFER_SIZE];
    int len = strlen(file_path);
    if (file_path[len - 1] != '/') {
      strcat(file_path, "/");
    }
    strcpy(dir_path, file_path);
    strcat(file_path, "index.html");
    FILE* fp = fopen(file_path, "r");

    if (!fp) {
      char request_path[BUFFER_SIZE];
      strcpy(request_path, request->path);
      int len = strlen(request_path);
      if (request_path[len - 1] != '/') {
        strcat(request_path, "/");
      }
      return list_response(fd, dir_path, request_path);
    }
    return response_file(fd, file_path);
  }
  return not_found_res(fd);
}


/*
 * Opens a connection to the proxy target (hostname=server_proxy_hostname and
 * port=server_proxy_port) and relays traffic to/from the stream fd and the
 * proxy target. HTTP requests from the client (fd) should be sent to the
 * proxy target, and HTTP responses from the proxy target should be sent to
 * the client (fd).
 *
 *   +--------+     +------------+     +--------------+
 *   | client | <-> | httpserver | <-> | proxy target |
 *   +--------+     +------------+     +--------------+
 */
void handle_proxy_request(int fd) {

  /*
  * The code below does a DNS lookup of server_proxy_hostname and 
  * opens a connection to it. Please do not modify.
  */

  struct sockaddr_in target_address;
  memset(&target_address, 0, sizeof(target_address));
  target_address.sin_family = AF_INET;
  target_address.sin_port = htons(server_proxy_port);

  struct hostent *target_dns_entry = gethostbyname2(server_proxy_hostname, AF_INET);

  int client_socket_fd = socket(PF_INET, SOCK_STREAM, 0);
  if (client_socket_fd == -1) {
    fprintf(stderr, "Failed to create a new socket: error %d: %s\n", errno, strerror(errno));
    exit(errno);
  }

  if (target_dns_entry == NULL) {
    fprintf(stderr, "Cannot find host: %s\n", server_proxy_hostname);
    exit(ENXIO);
  }

  char *dns_address = target_dns_entry->h_addr_list[0];

  memcpy(&target_address.sin_addr, dns_address, sizeof(target_address.sin_addr));
  int connection_status = connect(client_socket_fd, (struct sockaddr*) &target_address,
      sizeof(target_address));

  if (connection_status < 0) {
    /* Dummy request parsing, just to be compliant. */
    http_request_parse(fd);

    http_start_response(fd, 502);
    http_send_header(fd, "Content-Type", "text/html");
    http_end_headers(fd);
    http_send_string(fd, "<center><h1>502 Bad Gateway</h1><hr></center>");
    return;

  }

  pthread_t proxy_client;
  pthread_t proxy_server;
  pthread_create(&proxy_client, NULL, proxy_child_worker, &(arg_t) {.from = fd, .to = client_socket_fd});
  pthread_create(&proxy_server, NULL, proxy_child_worker, &(arg_t) {.from = client_socket_fd, .to = fd});
  pthread_join(proxy_client, NULL);
  pthread_join(proxy_server, NULL);
  printf("Finish for one connection \n");
  close(fd);
  close(client_socket_fd);
  /* 
  * TODO: Your solution for task 3 belongs here! 
  */
}

void* worker(void* arg) {
    void (*request_handler)(int) = arg;
    pthread_mutex_lock(&work_queue.lock);
    while(1) {
        while(work_queue.size == 0) {
            pthread_cond_wait(&work_queue.cond, &work_queue.lock);
        }
        printf("Served by thread_id %i \n", (unsigned int)(pthread_self() % 100));
        int fd = wq_pop(&work_queue);
        request_handler(fd);
        close(fd);
    }
    return NULL;

}

void init_thread_pool(int num_threads, void (*request_handler)(int)) {
  /*
   * TODO: Part of your solution for Task 2 goes here!
   */
  for (size_t i = 0; i < num_threads; ++i) {
      pthread_t* ptr = malloc(sizeof(pthread_t));
      pthread_create(ptr, NULL, worker, request_handler);
  }
}

/*
 * Opens a TCP stream socket on all interfaces with port number PORTNO. Saves
 * the fd number of the server socket in *socket_number. For each accepted
 * connection, calls request_handler with the accepted fd number.
 */
void serve_forever(int *socket_number, void (*request_handler)(int)) {

  struct sockaddr_in server_address, client_address;
  size_t client_address_length = sizeof(client_address);
  int client_socket_number;

  *socket_number = socket(PF_INET, SOCK_STREAM, 0);
  if (*socket_number == -1) {
    perror("Failed to create a new socket");
    exit(errno);
  }

  int socket_option = 1;
  if (setsockopt(*socket_number, SOL_SOCKET, SO_REUSEADDR, &socket_option,
        sizeof(socket_option)) == -1) {
    perror("Failed to set socket options");
    exit(errno);
  }

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = INADDR_ANY;
  server_address.sin_port = htons(server_port);

  if (bind(*socket_number, (struct sockaddr *) &server_address,
        sizeof(server_address)) == -1) {
    perror("Failed to bind on socket");
    exit(errno);
  }

  if (listen(*socket_number, 1024) == -1) {
    perror("Failed to listen on socket");
    exit(errno);
  }

  printf("Listening on port %d...\n", server_port);

  init_thread_pool(num_threads, request_handler);

  while (1) {
    client_socket_number = accept(*socket_number,
        (struct sockaddr *) &client_address,
        (socklen_t *) &client_address_length);
    if (client_socket_number < 0) {
      perror("Error accepting socket");
      continue;
    }

    printf("Accepted connection from %s on port %d\n",
        inet_ntoa(client_address.sin_addr),
        client_address.sin_port);

    if (num_threads == 0) {
        request_handler(client_socket_number);
        close(client_socket_number);
    } else {
        pthread_mutex_lock(&work_queue.lock);
        wq_push(&work_queue, client_socket_number);
        pthread_cond_signal(&work_queue.cond);
        pthread_mutex_unlock(&work_queue.lock);
    }
  }
  shutdown(*socket_number, SHUT_RDWR);
  close(*socket_number);
}

int server_fd;
void signal_callback_handler(int signum) {
  printf("Caught signal %d: %s\n", signum, strsignal(signum));
  printf("Closing socket %d\n", server_fd);
  if (close(server_fd) < 0) perror("Failed to close server_fd (ignoring)\n");
  exit(0);
}

char *USAGE =
  "Usage: ./httpserver --files www_directory/ --port 8000 [--num-threads 5]\n"
  "       ./httpserver --proxy inst.eecs.berkeley.edu:80 --port 8000 [--num-threads 5]\n";

void exit_with_usage() {
  fprintf(stderr, "%s", USAGE);
  exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
  signal(SIGINT, signal_callback_handler);

  /* Default settings */
  server_port = 8000;
  void (*request_handler)(int) = NULL;

  int i;
  for (i = 1; i < argc; i++) {
    if (strcmp("--files", argv[i]) == 0) {
      request_handler = handle_files_request;
      free(server_files_directory);
      server_files_directory = argv[++i];
      if (!server_files_directory) {
        fprintf(stderr, "Expected argument after --files\n");
        exit_with_usage();
      }
    } else if (strcmp("--proxy", argv[i]) == 0) {
      request_handler = handle_proxy_request;

      char *proxy_target = argv[++i];
      if (!proxy_target) {
        fprintf(stderr, "Expected argument after --proxy\n");
        exit_with_usage();
      }

      char *colon_pointer = strchr(proxy_target, ':');
      if (colon_pointer != NULL) {
        *colon_pointer = '\0';
        server_proxy_hostname = proxy_target;
        server_proxy_port = atoi(colon_pointer + 1);
      } else {
        server_proxy_hostname = proxy_target;
        server_proxy_port = 80;
      }
    } else if (strcmp("--port", argv[i]) == 0) {
      char *server_port_string = argv[++i];
      if (!server_port_string) {
        fprintf(stderr, "Expected argument after --port\n");
        exit_with_usage();
      }
      server_port = atoi(server_port_string);
    } else if (strcmp("--num-threads", argv[i]) == 0) {
      char *num_threads_str = argv[++i];
      if (!num_threads_str || (num_threads = atoi(num_threads_str)) < 1) {
        fprintf(stderr, "Expected positive integer after --num-threads\n");
        exit_with_usage();
      }
    } else if (strcmp("--help", argv[i]) == 0) {
      exit_with_usage();
    } else {
      fprintf(stderr, "Unrecognized option: %s\n", argv[i]);
      exit_with_usage();
    }
  }

  if (server_files_directory == NULL && server_proxy_hostname == NULL) {
    fprintf(stderr, "Please specify either \"--files [DIRECTORY]\" or \n"
                    "                      \"--proxy [HOSTNAME:PORT]\"\n");
    exit_with_usage();
  }

  if (server_proxy_hostname) {
      // We use three thread to serve one connection.
      num_threads /= 3;
  }

  printf("Thread number is %d \n", num_threads);

  serve_forever(&server_fd, request_handler);

  return EXIT_SUCCESS;
}
