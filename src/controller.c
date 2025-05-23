#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "airport.h"

#define PORT_STRLEN 6
#define DEFAULT_PORTNUM 1024
#define MIN_PORTNUM 1024
#define MAX_PORTNUM 65535

#define THREAD_POOL_SIZE 10 
pthread_t thread_pool[THREAD_POOL_SIZE];

/** Struct that contains information associated with each airport node. */
typedef struct airport_node_info {
  int id;    /* Airport identifier */
  int port;  /* Port num associated with this airport's listening socket */
  pid_t pid; /* PID of the child process for this airport. */
} node_info_t;

/** Struct that contains parameters for the controller node and ATC network as
 *  a whole. */
typedef struct controller_params_t {
  int listenfd;               /* file descriptor of the controller listening socket */
  int portnum;                /* port number used to connect to the controller */
  int num_airports;           /* number of airports to create */
  int *gate_counts;           /* array containing the number of gates in each airport */
  node_info_t *airport_nodes; /* array of info associated with each airport */
} controller_params_t;

controller_params_t ATC_INFO;

// Queue to hold client connections
typedef struct connection_queue {
  node_t *head;             // first node in the queue
  node_t *tail;             // last node in the queue (for inserting new clients)
  pthread_mutex_t lock;
  pthread_cond_t cond;      // condition var to notify worker threads
} connection_queue_t;

connection_queue_t connection_queue;

/** Initialises the queue
*/
void initialize_connection_queue() {
  connection_queue.head = NULL;
  connection_queue.tail = NULL;
  pthread_mutex_init(&connection_queue.lock, NULL);
  pthread_cond_init(&connection_queue.cond, NULL);
}

/** Adds a new connection to the queue
*/
void add_connection_fd(int connfd)
{
  node_t *new_node = malloc(sizeof(node_t));
  new_node->connfd = connfd;
  new_node->next = NULL;

  // acquire lock to modify shared
  pthread_mutex_lock(&connection_queue.lock); 
  // if the queue is initially empty
  if (connection_queue.tail == NULL)
  {
    connection_queue.head = new_node;
    connection_queue.tail = new_node;
  } else
  {
    connection_queue.tail->next = new_node;
    connection_queue.tail = new_node;
  }

  //signal worker threads that a new connection is available
  pthread_cond_signal(&connection_queue.cond);
  pthread_mutex_unlock(&connection_queue.lock);
}

/** Retrieves a connection from the queue
*/
int remove_head_fd()
{
  pthread_mutex_lock(&connection_queue.lock);
  // Wait for a connection if the queue is empty
  // while loop used (reason as stated in ostep book)
  while (connection_queue.head == NULL)
  {
    pthread_cond_wait(&connection_queue.cond, &connection_queue.lock);
  }

  node_t *node = connection_queue.head;
  int connfd = node->connfd;
  connection_queue.head = node->next;
  if (connection_queue.head == NULL)
  {
    connection_queue.tail = NULL;
  }
  free(node);
  pthread_mutex_unlock(&connection_queue.lock);
  return connfd;
}

void request_handler(int connfd)
{
  char buffer[MAXLINE];
  memset(buffer, '\0', sizeof(buffer));

  // printf("Client connected on connfd: %d\n", connfd);

  rio_t rio;
  rio_readinitb(&rio, connfd);
  while (1)
  {
    // printf("Waiting to read request.\n");
    ssize_t read_size = rio_readlineb(&rio, buffer, sizeof(buffer));
    // printf("Received request: %s\n", buffer);
    if (read_size < 0)
    {
      perror("Rio read error");
    } else if (read_size == 0)
    {
      break; //EOF
    }
      
    //Check one of the three cmds only
    char first_word[30];
    int airport_num;


    // Check if the request format is valid and retrieve the airport number
    // Use sscanf to read the first word from the buffer without modifying it
    int matched = sscanf(buffer, "%29s %d", first_word, &airport_num);

    if (matched != 2)
    {
      snprintf(buffer, sizeof(buffer), "Error: Invalid request provided\n");
      rio_writen(connfd, buffer, strlen(buffer));
      continue;
    }

    int param1, param2, param3, param4, num_parsed;
    if (strncasecmp(first_word, "SCHEDULE", 8) == 0)
    {
      num_parsed = sscanf(buffer, "%s %d %d %d %d", first_word, &param1, &param2, &param3, &param4);
      if (num_parsed != 5)
      {
        snprintf(buffer, sizeof(buffer), "Error: Invalid request provided\n");
        rio_writen(connfd, buffer, strlen(buffer));
        continue;
      }
    } else if (strncasecmp(first_word, "PLANE_STATUS", 12) == 0)
    {
      num_parsed = sscanf(buffer, "%s %d %d", first_word, &param1, &param2);
      if (num_parsed != 3)
      {
        snprintf(buffer, sizeof(buffer), "Error: Invalid request provided\n");
        rio_writen(connfd, buffer, strlen(buffer));
        continue;
      }
    } else if (strncasecmp(first_word, "TIME_STATUS", 11) == 0)
    {
      num_parsed = sscanf(buffer, "%s %d %d %d", first_word, &param1, &param2, &param3);
      if (num_parsed != 4)
      {
        snprintf(buffer, sizeof(buffer), "Error: Invalid request provided\n");
        rio_writen(connfd, buffer, strlen(buffer));
        continue;
      }
    }
      
    // Validate airport number
    if (airport_num < 0 || airport_num >= ATC_INFO.num_airports)
    {
      snprintf(buffer, sizeof(buffer), "Error: Airport %d does not exist\n", airport_num);
      rio_writen(connfd, buffer, strlen(buffer));
      printf("Invalid airport number %d, sent error response\n", airport_num);
      continue;
    }

    // Retrieve the port for the target airport node
    int airport_port = ATC_INFO.airport_nodes[airport_num].port;
    char port_str[PORT_STRLEN];
    snprintf(port_str, sizeof(port_str), "%d", airport_port);

    // Forward the request to the appropriate airport node
    // printf("Connecting to airport node %d on port %s...\n", airport_num, port_str);
    int airport_connfd = open_clientfd("localhost", port_str);
    if (airport_connfd < 0)
    {
      snprintf(buffer, sizeof(buffer), "Error: Unable to connect to airport %d\n", airport_num);
      rio_writen(connfd, buffer, strlen(buffer));
      perror("Error connecting to airport node");
      return;
    }

    // Forward the request to the airport node
    // printf("Forwarding request to airport node %d\n", airport_num);
    rio_writen(airport_connfd, buffer, strlen(buffer));


    // Receive the response from the airport node and send it back to the client
    rio_t rio_air;
    rio_readinitb(&rio_air, airport_connfd);
    while ((read_size = rio_readlineb(&rio_air, buffer, sizeof(buffer))) > 0)
    {
      // printf("Received response from airport node: %s", buffer);
      rio_writen(connfd, buffer, read_size);  // Write the response back to the client
    }

    close(airport_connfd);  // Close the connection to the airport node
    // printf("Closed connection to airport node %d\n", airport_num);
  }
  close(connfd);
  // printf("Client disconnected from connfd: %d\n", connfd);
}


void *worker_thread(void *arg) {
    while (1)
    {
        // get a connection if available
        int connfd = remove_head_fd();
        if (connfd < 0) continue;

        // Handle the client request 
        request_handler(connfd);
    }
    return NULL;
}


/** @brief The main server loop of the controller.
 *
 *  @todo  Implement this function!
 */
void controller_server_loop(void)
{
  int listenfd = ATC_INFO.listenfd;

  // initialise the cond var and lk.
  initialize_connection_queue();

  // initialize thread pool
  for (int i = 0; i < THREAD_POOL_SIZE; i++) {
    pthread_create(&thread_pool[i], NULL, worker_thread, NULL);
    // Detach threads
    pthread_detach(thread_pool[i]);
  }
  // Main server loop
  while (1)
  {
    int connfd = accept(listenfd, NULL, NULL);  // Accept a new client connection
    if (connfd < 0)
    {
      perror("accept");
      continue;
    }

    // Add the connection to the queue for workers to process
    add_connection_fd(connfd);
  }
}

/** @brief A handler for reaping child processes (individual airport nodes).
 *         It may be helpful to set a breakpoint here when trying to debug
 *         issues that cause your airport nodes to crash.
 */
void sigchld_handler(int sig) {
  while (waitpid(-1, 0, WNOHANG) > 0)
    ;
  return;
}

/** You should not modify any of the functions below this point, nor should you
 *  call these functions from anywhere else in your code. These functions are
 *  used to handle the initial setup of the Air Traffic Control system.
 */

/** @brief This function spawns child processes for each airport node, and
 *         opens a listening socket for the controller to u.
 */
void initialise_network(void) {
  char port_str[PORT_STRLEN];
  int num_airports = ATC_INFO.num_airports;
  int lfd, idx, port_num = ATC_INFO.portnum;
  node_info_t *node;
  pid_t pid;

  snprintf(port_str, PORT_STRLEN, "%d", port_num);
  if ((ATC_INFO.listenfd = open_listenfd(port_str)) < 0) {
    perror("[Controller] open_listenfd");
    exit(1);
  }

  for (idx = 0; idx < num_airports; idx++) {
    node = &ATC_INFO.airport_nodes[idx];
    node->id = idx;
    node->port = ++port_num;
    snprintf(port_str, PORT_STRLEN, "%d", port_num);
    if ((lfd = open_listenfd(port_str)) < 0) {
      perror("open_listenfd");
      continue;
    }
    if ((pid = fork()) == 0) {
      close(ATC_INFO.listenfd);
      initialise_node(idx, ATC_INFO.gate_counts[idx], lfd);
      exit(0);
    } else if (pid < 0) {
      perror("fork");
    } else {
      node->pid = pid;
      fprintf(stderr, "[Controller] Airport %d assigned port %s\n", idx, port_str);
      close(lfd);
    }
  }

  signal(SIGCHLD, sigchld_handler);
  controller_server_loop();
  exit(0);
}

/** @brief Prints usage information for the program and then exits. */
void print_usage(char *program_name) {
  printf("Usage: %s [-n N] [-p P] -- [gate count list]\n", program_name);
  printf("  -n: Number of airports to create.\n");
  printf("  -p: Port number to use for controller.\n");
  printf("  -h: Print this help message and exit.\n");
  exit(0);
}

/** @brief   Parses the gate counts provided for each airport given as the final
 *           argument to the program.
 *
 *  @param list_arg argument string containing the integer list
 *  @param expected expected number of integer values to read from the list.
 *
 *
 *  @returns An allocated array of gate counts for each airport, or `NULL` if
 *           there was an issue in parsing the gate counts.
 *
 *  @warning If a list of *more* than `expected` integers is given as an argument,
 *           then all integers after the nth are silently ignored.
 */
int *parse_gate_counts(char *list_arg, int expected) {
  int *arr, n = 0, idx = 0;
  char *end, *buff = list_arg;
  if (!list_arg) {
    fprintf(stderr, "Expected gate counts for %d airport nodes.\n", expected);
    return NULL;
  }
  end = list_arg + strlen(list_arg);
  arr = calloc(1, sizeof(int) * (unsigned)expected);
  if (arr == NULL)
    return NULL;

  while (buff < end && idx < expected) {
    if (sscanf(buff, "%d%n%*c%n", &arr[idx++], &n, &n) != 1) {
      break;
    } else {
      buff += n;
    }
  }

  if (idx < expected) {
    fprintf(stderr, "Expected %d gate counts, got %d instead.\n", expected, idx);
    free(arr);
    arr = NULL;
  }

  return arr;
}

/** @brief Parses and validates the arguments used to create the Air Traffic
 *         Control Network. If successful, the `ATC_INFO` variable will be
 *         initialised.
 */
int parse_args(int argc, char *argv[]) {
  int c, ret = 0, *gate_counts = NULL;
  int atc_portnum = DEFAULT_PORTNUM;
  int num_airports = 0;
  int max_portnum = MAX_PORTNUM;

  while ((c = getopt(argc, argv, "n:p:h")) != -1) {
    switch (c) {
    case 'n':
      sscanf(optarg, "%d", &num_airports);
      max_portnum -= num_airports;
      break;
    case 'p':
      sscanf(optarg, "%d", &atc_portnum);
      break;
    case 'h':
      print_usage(argv[0]);
      break;
    case '?':
      fprintf(stderr, "Unknown Option provided: %c\n", optopt);
      ret = -1;
    default:
      break;
    }
  }

  if (num_airports <= 0) {
    fprintf(stderr, "-n must be greater than 0.\n");
    ret = -1;
  }
  if (atc_portnum < MIN_PORTNUM || atc_portnum >= max_portnum) {
    fprintf(stderr, "-p must be between %d-%d.\n", MIN_PORTNUM, max_portnum);
    ret = -1;
  }

  if (ret >= 0) {
    if ((gate_counts = parse_gate_counts(argv[optind], num_airports)) == NULL)
      return -1;
    ATC_INFO.num_airports = num_airports;
    ATC_INFO.gate_counts = gate_counts;
    ATC_INFO.portnum = atc_portnum;
    ATC_INFO.airport_nodes = calloc((unsigned)num_airports, sizeof(node_info_t));
  }

  return ret;
}

int main(int argc, char *argv[]) {
  // useful for seeing stdout in file
  // setvbuf(stdout, NULL, _IONBF, 0); // set unbuffered
  if (parse_args(argc, argv) < 0)
    return 1;
  initialise_network();
  controller_server_loop();
  return 0;
}
