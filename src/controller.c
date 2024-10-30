#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "airport.h"
#include "uthash.h"

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

typedef struct {
    int airport_fd;    // File descriptor for the airport
    int client_fd;     // File descriptor for the client
    UT_hash_handle hh; // Makes this structure hashable
} AirportClientMap;

AirportClientMap *airport_client_map = NULL;  // Hash table pointer

void add_airport_client_fd(int airport_fd, int client_fd) {
    AirportClientMap *entry = malloc(sizeof(AirportClientMap));
    entry->airport_fd = airport_fd;
    entry->client_fd = client_fd;
    HASH_ADD_INT(airport_client_map, airport_fd, entry);
}

bool check_if_airport(int fd) {
    AirportClientMap *entry;
    HASH_FIND_INT(airport_client_map, &fd, entry);
    return (entry != NULL);
}

int get_max_fd(fd_set *set, int current_max) {
    int max_fd = -1;
    for (int fd = 0; fd < current_max; fd++) {
        if (FD_ISSET(fd, set)) {
            max_fd = fd > max_fd ? fd : max_fd;
        }
    }
    return max_fd;
}

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
  // char buffer[256];       // Buffer to hold received data
  int i, n;

  FD_ZERO(&all_fds);           // Initialize the master set
  FD_SET(listenfd, &all_fds);  // Add listenfd to the set
  while (1)
  {
    read_fds = all_fds;  // Copy the master set to the read set

    // Use select() to wait for activity on any of the file descriptors
    printf("Waiting on select: \n");
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

    // Check all connected clients for incoming data
    for (i = 0; i <= max_fd; i++)
    {
      if (i != listenfd && FD_ISSET(i, &read_fds))
      {
        // Receive data from the client (Airplane / Airport)
        char buffer[MAXLINE];
        memset(buffer, 0, MAXLINE);

        rio_t rio;

        // Initialise the rio_t structure
        rio_readinitb(&rio, i);

        ssize_t read_size;
        char response[] = "HELLO\n";
        rio_writen(i, &response, strlen(response) + 1);
        continue;

        // Check if the client is an airport
        bool is_airport = check_if_airport(i);

        if (!is_airport) // a request from client
        {
          read_size = rio_readlineb(&rio, buffer, MAXLINE); // buffer is a command now
          // In the client disconnection handling code
          if (read_size == 0) // END OF FILE
          {
              printf("Closing connection with the client: %d\n", i);

              // Remove from select set and close fd
              FD_CLR(i, &all_fds);
              if (i == max_fd)
              {
                  max_fd = get_max_fd(&all_fds, max_fd);
              }
              close(i);

              // Now, search for any airport_fd mapped to this client_fd
              AirportClientMap *entry, *tmp;
              HASH_ITER(hh, airport_client_map, entry, tmp) {
                  if (entry->client_fd == i) {
                      // Close airport_fd
                      close(entry->airport_fd);
                      FD_CLR(entry->airport_fd, &all_fds);
                      if (entry->airport_fd == max_fd)
                          max_fd = get_max_fd(&all_fds, max_fd);

                      // Remove mapping
                      HASH_DEL(airport_client_map, entry);
                      free(entry);
                  }
              }

              continue;
          }
          if (read_size < 0)
          {
            perror("Rio read error");
          }
          //Check one of the three cmds only
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

          request_t req; //req being sent to the airport
          if (strncasecmp(first_word, "SCHEDULE", 8) == 0)
          {
            req.type = SCHEDULE_REQUEST;
            int plane_id, earliest_time, duration, fuel;
            // Use sscanf to check if the request matches the expected format
            int matched = sscanf(buffer, "%s %d %d %d %d %d", first_word, &airport_num, &plane_id, &earliest_time, &duration, &fuel);
            if (matched == 6)
            {
              req.data.schedule.plane_id = plane_id;
              req.data.schedule.earliest_time = earliest_time;
              req.data.schedule.duration = duration;
              req.data.schedule.fuel = fuel;
            } else
            {
              char message[] = "Error: Invalid request provided\n";
              rio_writen(i, message, strlen(message) + 1);
              continue;
            }

            if (airport_num >= ATC_INFO.num_airports || airport_num < 0)
            {
              char response[100];
              sprintf(response, "Airport %d does not exist\n", airport_num);
              rio_writen(i, response, strlen(response) + 1);
              continue;
            }
            
          } else if (strncasecmp(first_word, "PLANE_STATUS", 12) == 0)
          {
            req.type = PLANE_STATUS_REQUEST;
            int plane_id;
            int matched = sscanf(buffer, "%s %d %d", first_word, &airport_num, &plane_id);
            if (matched == 3)
            {
              req.data.plane_status.plane_id = plane_id;
            } else
            {
              char message[] = "Error: Invalid request provided\n";
              rio_writen(i, message, strlen(message) + 1);
              continue;
            }
            if (airport_num >= ATC_INFO.num_airports || airport_num < 0)
            {
              char response[100];
              sprintf(response, "Airport %d does not exist\n", airport_num);
              rio_writen(i, response, strlen(response) + 1);
              continue;
            }

          } else if (strncasecmp(first_word, "TIME_STATUS", 11) == 0)
          {
            req.type = TIME_STATUS_REQUEST;
            int gate_num, start_idx, duration;
            int matched = sscanf(buffer, "%s %d %d %d %d", first_word, &airport_num, &gate_num, &start_idx, &duration);
            if (matched == 5)
            {
              req.data.time_status.gate_num = gate_num;
              req.data.time_status.start_idx = start_idx;
              req.data.time_status.duration = duration;
            } else
            {
              char message[] = "Error: Invalid request provided\n";
              rio_writen(i, message, strlen(message) + 1);
              continue;
            }
            if (airport_num >= ATC_INFO.num_airports || airport_num < 0)
            {
              char response[100];
              sprintf(response, "Airport %d does not exist\n", airport_num);
              rio_writen(i, response, strlen(response) + 1);
              continue;
            }

          } else 
          {
            char message[] = "Error: Invalid request provided\n";
            rio_writen(i, message, strlen(message) + 1);
            continue;
          }

          // Forward the above received request to the specific airport
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

            // ADD THIS AIRPORT TO THE HASHTABLE OF ACTIVE AIRPORTS
            // add_airport_fd(airport_id, airport_fd);
            add_airport_client_fd(airport_fd, i);  // 'i' is the client_fd
            FD_SET(airport_fd, &all_fds);
            if (airport_fd > max_fd)
            {
              max_fd = airport_fd;  // Update max_fd if necessary
            }

            // Send data to airport using rio
            int write_num = rio_writen(airport_fd, &req, sizeof(request_t));
            if (write_num == -1)
            {
              perror("send");
            }
            break;
          }

        } else
        {
          // It is an airport responding back
          printf("Airport is trynna respond back\n");
          response_t response;
          rio_t rio;
          rio_readinitb(&rio, i);
          int nread = rio_readnb(&rio, &response, sizeof(response));

          if (nread < 0) {
              // if (nread == 0) {
              //     printf("Airport fd %d closed connection\n", i);
              // } else {
              //     perror("rio_readnb");
              // }

              // Close airport_fd and remove from select set
              close(i);
              FD_CLR(i, &all_fds);
              if (i == max_fd)
                  max_fd = get_max_fd(&all_fds, max_fd);

              // Remove mapping if any
              AirportClientMap *entry;
              HASH_FIND_INT(airport_client_map, &i, entry); // 'i' is the airport_fd
              if (entry) {
                  HASH_DEL(airport_client_map, entry);
                  free(entry);
              }

              continue;
          }

          char str_response[MAXLINE];
          bool success;
          int pl_id, air_num, gate_num, start_time, end_time, num_idxs;
          air_num = response.airport_id;

          switch (response.type)
          {
            case SCHEDULE_RESPONSE:
              success = response.data.schedule.success;
              pl_id = response.data.schedule.plane_id;
              if (response.data.schedule.success)
              {
                gate_num = response.data.schedule.gate_num;
                start_time = response.data.schedule.start_time;
                end_time = response.data.schedule.end_time;
                sprintf(str_response, "SCHEDULED %d at GATE %d: %02d:%02d-%02d:%02d\n", pl_id, gate_num,
                        IDX_TO_HOUR(start_time), IDX_TO_MINS(start_time), IDX_TO_HOUR(end_time), IDX_TO_MINS(end_time));
              } else
              {
                sprintf(str_response, "Error: Cannot schedule %d\n", pl_id);
              }
              break;
            case PLANE_STATUS_RESPONSE:
              success = response.data.plane_status.scheduled;

              if (success)
              {
                gate_num = response.data.plane_status.gate_num;
                start_time = response.data.plane_status.start_time;
                end_time = response.data.plane_status.end_time;
                sprintf(str_response, "PLANE %d scheduled at GATE %d: %02d:%02d-%02d:%02d\n", pl_id, gate_num,
                        IDX_TO_HOUR(start_time), IDX_TO_MINS(start_time), IDX_TO_HOUR(end_time), IDX_TO_MINS(end_time));
              } else
              {
                sprintf(str_response, "PLANE %d not scheduled at airport %d\n", pl_id, air_num);
              }
              break;
            case TIME_STATUS_RESPONSE:
              // str_response[MAXLINE];
              num_idxs = response.data.time_status.num_idxs;
              int *idxs = response.data.time_status.idxs;
              char *statuses = response.data.time_status.statuses;
              int *pl_ids = response.data.time_status.pl_ids;
              memset(str_response, 0, sizeof(str_response)); //initialise the
              for (int i=0; i < num_idxs; i++)
              {
                sprintf(str_response + strlen(str_response), "AIRPORT %d GATE %d %02d:%02d: %c - %d\n",
                        air_num, idxs[i], IDX_TO_HOUR(idxs[i]), IDX_TO_MINS(idxs[i]), statuses[i], pl_ids[i]);
              }
              break;
          }

          AirportClientMap *entry_a_c_map;
          HASH_FIND_INT(airport_client_map, &i, entry_a_c_map); // 'i' is the airport_fd
          if (entry_a_c_map) {
              int client_fd = entry_a_c_map->client_fd;

              // Send response to the client
              rio_writen(client_fd, str_response, strlen(str_response) + 1);

              // Remove the mapping and clean up
              HASH_DEL(airport_client_map, entry_a_c_map);
              free(entry_a_c_map);
              close(i);
              FD_CLR(i, &all_fds);
              if (i == max_fd) {
                  max_fd = get_max_fd(&all_fds, max_fd);
              }
          } else {
              // Handle error: mapping not found
              fprintf(stderr, "Error: No client mapping found for airport_fd %d\n", i);
          }
        }
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
