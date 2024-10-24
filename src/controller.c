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

/** @brief The main server loop of the controller.
 *
 *  @todo  Implement this function!
 */
void controller_server_loop(void) {
  int listenfd = ATC_INFO.listenfd;
  fd_set all_fds, read_fds;
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  // To keep track of the highest file descriptor
  int max_fd = listenfd;
  int newfd;              // New socket descriptor for accepted connection
  int nready;             // Number of file descriptors ready for reading
  char buffer[256];       // Buffer to hold received data
  int i, n;

  FD_ZERO(&all_fds);           // Initialize the master set
  FD_SET(listenfd, &all_fds);  // Add listenfd to the set
  while (1) {
    read_fds = all_fds;  // Copy the master set to the read set

    // Use select() to wait for activity on any of the file descriptors
    nready = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
    if (nready == -1)
    {
      perror("select");
      exit(1);
    }

    // Check if there is an incoming connection on the listening socket
    if (FD_ISSET(listenfd, &read_fds))
    {
      // Accept new connection
      newfd = accept(listenfd, (struct sockaddr *)&client_addr, &client_len);
      if (newfd == -1)
      {
        perror("accept");
        continue;
      }

      // Add the new socket to the set of all file descriptors
      FD_SET(newfd, &all_fds);

      if (newfd > max_fd)
      {
        max_fd = newfd;  // Update max_fd if necessary
      }

      // Log the connection (you can print client info or airport node ID)
      printf("Controller: Accepted new connection on fd %d\n", newfd);
    }

    // Check all connected clients (airports) for incoming data
    for (i = 0; i <= max_fd; i++) {
      if (i != listenfd && FD_ISSET(i, &read_fds)) {
        // Receive data from the client (Airplane / Airport)
        char buffer[MAXLINE];
        rio_t rio;

        // Initialise the rio_t structure
        rio_readinitb(&rio, i);

        ssize_t read_size;
        read_size = rio_readlineb(&rio, buffer, MAXLINE);
        if (read_size < 0)
        {
          perror("Rio read error");
        }

        // buffer[read_size] = '\0';  // Null-terminate the string

        //Check if it is a command
        char first_word[30];

        int airport_num;
        // Use sscanf to read the first word from the buffer without modifying it
        int matched = sscanf(buffer, "%29s %d", first_word, &airport_num);
        if (matched != 2)
        {
          char message[] = "Error: Invalid request provided\n";
          rio_writen(i, message, strlen(message) + 1);
          continue;
        }
        if (strncasecmp(first_word, "SCHEDULE", 8) == 0
            || strncasecmp(first_word, "PLANE_STATUS", 12) == 0
            || strncasecmp(first_word, "TIME_STATUS", 11) == 0)
        {
          printf("Request from airplane (maybe valid or invalid).\n");
          if (strncasecmp(first_word, "SCHEDULE", 8) == 0)
          {
            int plane_id, earliest_time, duration, fuel;
            // Use sscanf to check if the request matches the expected format
            int matched = sscanf(buffer, "%s %d %d %d %d %d", first_word, &airport_num, &plane_id, &earliest_time, &duration, &fuel);
            if (airport_num >= ATC_INFO.num_airports || airport_num < 0)
            {
              char response[100];
              sprintf(response, "Airport %d does not exist\n", airport_num);
              rio_writen(i, response, strlen(response) + 1);
              continue;
            }
            if (matched == 6)
            {
              // Process the command
              sprintf(buffer, "%s %d %d %d %d\n", first_word, plane_id, earliest_time, duration, fuel);
            } else
            {
              char message[] = "Error: Invalid request provided\n";
              rio_writen(i, message, strlen(message) + 1);
              continue;
            }
          } else if (strncasecmp(first_word, "PLANE_STATUS", 12) == 0)
          {
            int plane_id;
            // Use sscanf to check if the request matches the expected format
            int matched = sscanf(buffer, "%s %d %d", first_word, &airport_num, &plane_id);
            if (airport_num >= ATC_INFO.num_airports || airport_num < 0)
            {
              char response[100];
              sprintf(response, "Airport %d does not exist\n", airport_num);
              rio_writen(i, response, strlen(response) + 1);
              continue;
            }
            if (matched == 3)
            {
              // Process the command
              sprintf(buffer, "%s %d\n", first_word, plane_id);
            } else
            {
              char message[] = "Error: Invalid request provided\n";
              rio_writen(i, message, strlen(message) + 1);
              continue;
            }
          } else if (strncasecmp(first_word, "TIME_STATUS", 11) == 0)
          {
            int gate_num, start_idx, duration;
            // Use sscanf to check if the request matches the expected format
            int matched = sscanf(buffer, "%s %d %d %d %d", first_word, &airport_num, &gate_num, &start_idx, &duration);
            if (airport_num >= ATC_INFO.num_airports || airport_num < 0)
            {
              char response[100];
              sprintf(response, "Airport %d does not exist\n", airport_num);
              rio_writen(i, response, strlen(response) + 1);
              continue;
            }
            if (matched == 5)
            {
              // Process the command
              sprintf(buffer, "%s %d %d %d\n", first_word, gate_num, start_idx, duration);
            } else
            {
              char message[] = "Error: Invalid request provided\n";
              rio_writen(i, message, strlen(message) + 1);
              continue;
            }
          }
        } else if (strncasecmp(first_word, "SCHEDULED", 9) == 0
                    || strncasecmp(first_word, "PLANE", 5) == 0
                    || strncasecmp(first_word, "AIRPORT", 7) == 0)
        {
          printf("Response from an airport (maybe valid or invalid).\n");
        } else
        {
          char message[] = "Error: Invalid request provided\n";
          rio_writen(i, message, strlen(message) + 1);
          continue;
        }

        printf("Controller: Received message: %s", buffer);

        //Forward this buffer to all the airports for the time being
        //first get info for all the airports
        node_info_t *airport_nodes = ATC_INFO.airport_nodes;
        int total_air = ATC_INFO.num_airports;

        for (int j = 0; j < total_air; j++)
        {
          int airport_id = airport_nodes[j].id;
          if (airport_id != airport_num)
          {
            continue;
          }
          int airport_port = airport_nodes[j].port;
          int airport_fd = socket(AF_INET, SOCK_STREAM, 0);
          if (airport_fd < 0)
          {
            perror("socket");
            continue;
          }

          struct sockaddr_in airport_addr;
          memset(&airport_addr, 0, sizeof(airport_addr));
          airport_addr.sin_family = AF_INET;
          airport_addr.sin_port = htons(airport_port);
          airport_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); //Assuming local host

          if (connect(airport_fd, (struct sockaddr *)&airport_addr, sizeof(airport_addr)) < 0)
          {
            perror("connect");
            close(airport_fd);
            continue;
          }

          // Send data to airport using rio
          int write_num = rio_writen(airport_fd, buffer, strlen(buffer) + 1);
          if (write_num == -1)
          {
            perror("send");
          }
          close(airport_fd); // Close after sending to avoid too many open sockets
          printf("The data was received by airport: %i, confirming, %i\n", airport_num, airport_id);
          break;//after the correct airport received data
        }

          //LOGIC ADD HERE
          send(i, "Acknowledged\n", strlen("Acknowledged\n"), 0);
        // }
      }
    }  
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
  if (parse_args(argc, argv) < 0)
    return 1;
  initialise_network();
  controller_server_loop();
  return 0;
}
