#include "airport.h"

#define AIRPORT_THREAD_POOL_SIZE 10

/** This is the main file in which you should implement the airport server code.
 *  There are many functions here which are pre-written for you. You should read
 *  the comments in the corresponding `airport.h` header file to understand what
 *  each function does, the arguments they accept and how they are intended to
 *  be used.
 *
 *  You are encouraged to implement your own helper functions to handle requests
 *  in airport nodes in this file. You are also permitted to modify the
 *  functions you have been given if needed.
 */

// Queue to hold incoming requests
typedef struct request_queue
{
  node_t *head;
  node_t *tail;
  pthread_mutex_t lock;
  pthread_cond_t cond;
} request_queue_t;

request_queue_t request_queue;

/** Initialises the queue
*/
void initialize_request_queue()
{
  request_queue.head = NULL;
  request_queue.tail = NULL;
  pthread_mutex_init(&request_queue.lock, NULL);
  pthread_cond_init(&request_queue.cond, NULL);
}

// Thread pool for the airport node
pthread_t airport_thread_pool[AIRPORT_THREAD_POOL_SIZE];

/* This will be set by the `initialise_node` function. */
static int AIRPORT_ID = -1;

/* This will be set by the `initialise_node` function. */
static airport_t *AIRPORT_DATA = NULL;

gate_t *get_gate_by_idx(int gate_idx) {
  if ((gate_idx) < 0 || (gate_idx > AIRPORT_DATA->num_gates))
    return NULL;
  else
    return &AIRPORT_DATA->gates[gate_idx];
}

time_slot_t *get_time_slot_by_idx(gate_t *gate, int slot_idx) {
  if ((slot_idx < 0) || (slot_idx >= NUM_TIME_SLOTS))
    return NULL;
  else
    return &gate->time_slots[slot_idx];
}

int check_time_slots_free(gate_t *gate, int start_idx, int end_idx) {
  time_slot_t *ts;
  int idx;
  for (idx = start_idx; idx <= end_idx; idx++) {
    ts = get_time_slot_by_idx(gate, idx);
    if (ts->status == 1)
      return 0;
  }
  return 1;
}

int set_time_slot(time_slot_t *ts, int plane_id, int start_idx, int end_idx) {
  if (ts->status == 1)
    return -1;
  ts->status = 1; /* Set to be occupied */
  ts->plane_id = plane_id;
  ts->start_time = start_idx;
  ts->end_time = end_idx;
  return 0;
}

int add_plane_to_slots(gate_t *gate, int plane_id, int start, int count) {
  int ret = 0, end = start + count;
  time_slot_t *ts = NULL;
  for (int idx = start; idx <= end; idx++) {
    ts = get_time_slot_by_idx(gate, idx);
    ret = set_time_slot(ts, plane_id, start, end);
    if (ret < 0) break;
  }
  return ret;
}

int search_gate(gate_t *gate, int plane_id) {
  int idx, next_idx;
  time_slot_t *ts = NULL;
  for (idx = 0; idx < NUM_TIME_SLOTS; idx = next_idx) {
    ts = get_time_slot_by_idx(gate, idx);
    if (ts->status == 0) {
      next_idx = idx + 1;
    } else if (ts->plane_id == plane_id) {
      return idx;
    } else {
      next_idx = ts->end_time + 1;
    }
  }
  return -1;
}

time_info_t lookup_plane_in_airport(int plane_id) {
  time_info_t result = {-1, -1, -1};
  int gate_idx, slot_idx;
  gate_t *gate;
  for (gate_idx = 0; gate_idx < AIRPORT_DATA->num_gates; gate_idx++) {
    gate = get_gate_by_idx(gate_idx);
    if ((slot_idx = search_gate(gate, plane_id)) >= 0) {
      result.start_time = slot_idx;
      result.gate_number = gate_idx;
      result.end_time = get_time_slot_by_idx(gate, slot_idx)->end_time;
      break;
    }
  }
  return result;
}

int assign_in_gate(gate_t *gate, int plane_id, int start, int duration, int fuel) {
  int idx, end = start + duration;
  for (idx = start; idx <= (start + fuel) && (end < NUM_TIME_SLOTS); idx++) {
    if (check_time_slots_free(gate, idx, end)) {
      add_plane_to_slots(gate, plane_id, idx, duration);
      return idx;
    }
    end++;
  }
  return -1;
}

time_info_t schedule_plane(int plane_id, int start, int duration, int fuel) {
  time_info_t result = {-1, -1, -1};
  gate_t *gate;
  int gate_idx, slot;
  for (gate_idx = 0; gate_idx < AIRPORT_DATA->num_gates; gate_idx++) {
    gate = get_gate_by_idx(gate_idx);
    if ((slot = assign_in_gate(gate, plane_id, start, duration, fuel)) >= 0) {
      result.start_time = slot;
      result.gate_number = gate_idx;
      result.end_time = slot + duration;
      break;
    }
  }
  return result;
}

airport_t *create_airport(int num_gates) {
  airport_t *data = NULL;
  size_t memsize = 0;
  if (num_gates > 0) {
    memsize = sizeof(airport_t) + (sizeof(gate_t) * (unsigned)num_gates);
    data = calloc(1, memsize);
  }
  if (data)
    data->num_gates = num_gates;
  return data;
}

void initialise_node(int airport_id, int num_gates, int listenfd) {
  AIRPORT_ID = airport_id;
  AIRPORT_DATA = create_airport(num_gates);
  if (AIRPORT_DATA == NULL)
    exit(1);
  initialize_request_queue();
  airport_node_loop(listenfd);
}

/** Add a new request to the queue
*/
void add_request_fd(int connfd)
{
  node_t *new_node = malloc(sizeof(node_t));
  new_node->connfd = connfd;
  new_node->next = NULL;

  pthread_mutex_lock(&request_queue.lock);
  if (request_queue.tail == NULL)
  {
    request_queue.head = new_node;
    request_queue.tail = new_node;
  } else
  {
    request_queue.tail->next = new_node;
    request_queue.tail = new_node;
  }
  pthread_cond_signal(&request_queue.cond);  // Notify worker threads
  pthread_mutex_unlock(&request_queue.lock);
}

/** returns a request fd from the queue waiting to be accomplished
*/
int remove_head_request_fd()
{
  pthread_mutex_lock(&request_queue.lock);
  while (request_queue.head == NULL)
  {  // Wait if the queue is empty
    pthread_cond_wait(&request_queue.cond, &request_queue.lock);
  }

  node_t *node = request_queue.head;
  int connfd = node->connfd;
  request_queue.head = node->next;
  if (request_queue.head == NULL)
  {
    request_queue.tail = NULL;
  }
  free(node);
  pthread_mutex_unlock(&request_queue.lock);
  return connfd;
}

void handle_airport_request(int connfd)
{
  char buffer[MAXLINE];
  memset(buffer, '\0', sizeof(buffer));

  rio_t rio;
  rio_readinitb(&rio, connfd);
  ssize_t read_size = rio_readlineb(&rio, buffer, sizeof(buffer));
  if (read_size < 0)
  {
    perror("Rio read error");
  } else if (read_size == 0)
  {
    return;
  }

  ///Check one of the three cmds only
  char first_word[30];
  int airport_num, plane_id, start_idx, duration, fuel;
  int gate_num;
  int matched = sscanf(buffer, "%29s %d", first_word, &airport_num);
  if (strncasecmp(first_word, "SCHEDULE", 8) == 0)
  {
    if (sscanf(buffer + strlen("SCHEDULE"), "%d %d %d %d %d", &airport_num, &plane_id, &start_idx, &duration, &fuel) == 5)
    {
      if (start_idx < 0 || start_idx >= NUM_TIME_SLOTS)
      {
        snprintf(buffer, sizeof(buffer), "Error: Invalid 'earliest' time (%d)\n", start_idx);
        rio_writen(connfd, buffer, strlen(buffer));
        return; // Stop further processing if st_id is invalid
      }
      if (duration < 0 || duration > NUM_TIME_SLOTS - start_idx)
      {
        snprintf(buffer, sizeof(buffer), "Error: Invalid 'duration' value (%d)\n", duration);
        rio_writen(connfd, buffer, strlen(buffer));
        return; // Stop further processing if duration is invalid
      }
      time_info_t result = schedule_plane(plane_id, start_idx, duration, fuel);
      if (result.start_time != -1)
      {
        snprintf(buffer, sizeof(buffer), "SCHEDULED %d at GATE %d: %02d:%02d-%02d:%02d\n",
                plane_id, result.gate_number, IDX_TO_HOUR(result.start_time), IDX_TO_MINS(result.start_time),
                IDX_TO_HOUR(result.end_time), IDX_TO_MINS(result.end_time));
      } else
      {
        snprintf(buffer, sizeof(buffer), "Error: Cannot schedule %d\n", plane_id);
      }
        rio_writen(connfd, buffer, strlen(buffer));
    } else
    {
      snprintf(buffer, sizeof(buffer), "Error: Invalid request provided\n");
      rio_writen(connfd, buffer, strlen(buffer));
    }
  } else if (strncasecmp(first_word, "PLANE_STATUS", 12) == 0)
  {
    if (sscanf(buffer + strlen("PLANE_STATUS"), "%d %d", &airport_num, &plane_id) == 2)
    {
      time_info_t result = lookup_plane_in_airport(plane_id);
      if (result.start_time != -1)
      {
        snprintf(buffer, sizeof(buffer), "PLANE %d scheduled at GATE %d: %02d:%02d-%02d:%02d\n",
                plane_id, result.gate_number, IDX_TO_HOUR(result.start_time), IDX_TO_MINS(result.start_time),
                IDX_TO_HOUR(result.end_time), IDX_TO_MINS(result.end_time));
      } else
      {
        snprintf(buffer, sizeof(buffer), "PLANE %d not scheduled at airport %d\n", plane_id, airport_num);
      }
      rio_writen(connfd, buffer, strlen(buffer));
    } else
    {
      snprintf(buffer, sizeof(buffer), "Error: Invalid request provided\n");
      rio_writen(connfd, buffer, strlen(buffer));
    }
  } else if (strncasecmp(first_word, "TIME_STATUS", 11) == 0)
  {
    if (sscanf(buffer + strlen("TIME_STATUS"), "%d %d %d %d", &airport_num, &gate_num, &start_idx, &duration) == 4)
    {
      if (duration < 0 || duration > NUM_TIME_SLOTS - start_idx)
      {
        snprintf(buffer, sizeof(buffer), "Error: Invalid 'duration' value (%d)\n", duration);
        rio_writen(connfd, buffer, strlen(buffer));
        return; // Stop further processing if duration is invalid
      }
      for (int i = start_idx; i <= start_idx + duration; i++)
      {
        time_slot_t *ts = get_time_slot_by_idx(get_gate_by_idx(gate_num), i);
        if (ts == NULL)
        {
          snprintf(buffer, sizeof(buffer), "Error: Invalid time slot %d\n", i);
          rio_writen(connfd, buffer, strlen(buffer));
          continue;
        }
        snprintf(buffer, sizeof(buffer), "AIRPORT %d GATE %d %02d:%02d: %c - %d\n",
                airport_num, gate_num, IDX_TO_HOUR(i), IDX_TO_MINS(i),
                ts->status ? 'A' : 'F', ts->status ? ts->plane_id : 0);
        rio_writen(connfd, buffer, strlen(buffer));
      }
    } else
    {
      snprintf(buffer, sizeof(buffer), "Error: Invalid request provided\n");
      rio_writen(connfd, buffer, strlen(buffer));
    }
  } else
  {
    snprintf(buffer, sizeof(buffer), "Error: Invalid request provided\n");
    rio_writen(connfd, buffer, strlen(buffer));
  }
}

void *airport_worker_thread(void *arg)
{
  while (1)
  {
    // process a request
    int connfd = remove_head_request_fd();
    if (connfd < 0)
    {
      perror("connfd");
      continue;
    }

    handle_airport_request(connfd);

    // Close connection after processing the request
    close(connfd);
  }
}

void airport_node_loop(int listenfd)
{
  // Initialize the thread pool
  for (int i = 0; i < AIRPORT_THREAD_POOL_SIZE; i++)
  {
    pthread_create(&airport_thread_pool[i], NULL, airport_worker_thread, NULL);
    pthread_detach(airport_thread_pool[i]);  // Detach threads to avoid memory leaks
  }
  while (1)
  {
    int connfd = accept(listenfd, NULL, NULL);  // Accept a new connection
    if (connfd < 0)
    {
      perror("accept");
      continue;
    }

    add_request_fd(connfd);  // Add the connection to the request queue for processing
  }
}
