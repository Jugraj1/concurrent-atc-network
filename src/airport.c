#include "airport.h"

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
  airport_node_loop(listenfd);
}

int handle_client(int client_fd) {
  rio_t rio;
  rio_readinitb(&rio, client_fd);
  request_t req;
  rio_readnb(&rio, &req, sizeof(request_t));  // Read the entire structure
  response_t response;
  response.airport_id = AIRPORT_ID; // TODO : RETURN THE AIRPORT FD

  switch (req.type)
  {
    case SCHEDULE_REQUEST:
      response.type = SCHEDULE_RESPONSE;

      int pl_id = req.data.schedule.plane_id;
      int er_time = req.data.schedule.earliest_time;
      int dur = req.data.schedule.duration;
      int fuel = req.data.schedule.fuel;

      time_info_t schedule = schedule_plane(pl_id, er_time, dur, fuel);
      if (schedule.gate_number != -1 && schedule.start_time != -1 && schedule.end_time != -1)
      {
        response.data.schedule.success = true;
        response.data.schedule.plane_id = pl_id;
        response.data.schedule.gate_num = schedule.gate_number;
        response.data.schedule.start_time = schedule.start_time;
        response.data.schedule.end_time = schedule.end_time;
      } else
      {
        response.data.schedule.success = false;
      }
      break;
    case PLANE_STATUS_REQUEST:
      response.type = PLANE_STATUS_RESPONSE;

      pl_id = req.data.plane_status.plane_id;
      time_info_t status = lookup_plane_in_airport(pl_id);
      if (status.gate_number != -1 && status.start_time != -1 && status.end_time != -1)
      {
        response.data.plane_status.scheduled = true;
        response.data.plane_status.plane_id = pl_id;
        response.data.plane_status.gate_num = status.gate_number;
        response.data.plane_status.start_time = status.start_time;
        response.data.plane_status.end_time = status.end_time;
      } else
      {
        response.data.plane_status.scheduled = false;
      }
      break;
    case TIME_STATUS_REQUEST:
      response.type = TIME_STATUS_RESPONSE;
      int gate_num = req.data.time_status.gate_num;
      int start_idx = req.data.time_status.start_idx;
      dur = req.data.time_status.duration;
      gate_t *gate = get_gate_by_idx(gate_num);

      response.data.time_status.num_idxs = dur + 1;
      for (int i = start_idx; i <= (start_idx + dur); i++)
      {
        time_slot_t *slot = get_time_slot_by_idx(gate, i);
        (response.data.time_status.idxs)[i - start_idx] = i;

        int status = check_time_slots_free(gate, i, i);
        response.data.time_status.statuses[i - start_idx] = status ? 'F' : 'A';

        response.data.time_status.pl_ids[i - start_idx] = status ? 0 : slot->plane_id;
      }
      break;
  }
  rio_writen(client_fd, &response, sizeof(response));
}

void airport_node_loop(int listenfd) {
  struct sockaddr_in client_addr;
  socklen_t client_len = sizeof(client_addr);
  int client_fd;
  while (1)
  {
    if ((client_fd = accept(listenfd, (SA *)&client_addr, &client_len)) < 0)
    {
      perror("accept");
      exit(1);
    }

    // Check if the connecting client is the controller
    // if (client_addr.sin_addr.s_addr != htonl(INADDR_LOOPBACK))
    // {
    //   // Not from localhost, send error and close
    //   char error_msg[] = "No direct communication to airport allowed.\n";
    //   rio_writen(client_fd, error_msg, strlen(error_msg) + 1);
    //   close(client_fd);
    //   continue;
    // }

    // Handle the request from the client
    
    handle_client(client_fd);

    //close
    close(client_fd);
  }
}
